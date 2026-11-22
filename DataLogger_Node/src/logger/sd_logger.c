/*******************************************************
 * @file sd_logger.c
 * @brief The SD card logger (Zephyr). See sd_logger.h. The decisions that can be wrong without hardware are in logger_core.c and
 *        are tested on the host; this file is the glue to the file system, the queue and the LED.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "sd_logger.h"

#include <ff.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/atomic.h>

#include "datalogger.h"
#include "logger_core.h"

LOG_MODULE_REGISTER(sd_logger, LOG_LEVEL_INF);

/*******************************************************
 * Macros
 *******************************************************/
#define DISK_NAME "SD"
#define MOUNT_POINT "/SD:"
#define THREAD_STACK_SIZE 4096
#define THREAD_PRIORITY 10 /* below the CAN thread (7): logging never delays the bus */
#define QUEUE_LEN 64
#define BUF_SIZE 2048
#define WRITE_BYTES 1024
#define WRITE_AGE_MS 250
#define SYNC_MS 1000 /* the most that a power cut can lose */
#define TICK_MS 50
#define RETRY_MS 2000
#define STATUS_LOG_MS 10000

/*******************************************************
 * Type Definitions
 *******************************************************/
struct rec
{
	uint32_t t_ms;
	uint16_t id;
	uint8_t tx;
	uint8_t dlc;
	uint8_t data[8];
};

/*******************************************************
 * Static Variables
 *******************************************************/
static const struct gpio_dt_spec l_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

K_MSGQ_DEFINE(l_queue, sizeof(struct rec), QUEUE_LEN, 4);

static FATFS l_fat;
static struct fs_mount_t l_mount = {
	.type = FS_FATFS,
	.fs_data = &l_fat,
	.mnt_point = MOUNT_POINT,
};
static struct fs_file_t l_file;

static char l_buf[BUF_SIZE];
static size_t l_len;
static logger_policy_t l_policy;

static atomic_t l_dropped;
static atomic_t l_state = ATOMIC_INIT(SD_LOGGER_STARTING);
static uint32_t l_lines;
static uint32_t l_bytes;

K_THREAD_STACK_DEFINE(l_stack, THREAD_STACK_SIZE);
static struct k_thread l_thread;

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void set_state(sd_logger_state_t s)
{
	if ((sd_logger_state_t)atomic_get(&l_state) != s)
	{
		static const char *const names[] = { "starting", "no card", "file system error", "logging" };

		LOG_INF("SD logger: %s", names[s]);
		atomic_set(&l_state, s);
	}
}

/* The status LED: slow blink = logging, fast blink = no card, steady on = error. */
static void led_update(uint32_t now_ms)
{
	bool on;

	switch ((sd_logger_state_t)atomic_get(&l_state))
	{
	case SD_LOGGER_LOGGING:
		on = ((now_ms / 500u) & 1u) == 0u;
		break;
	case SD_LOGGER_FS_ERROR:
		on = true;
		break;
	default:
		on = ((now_ms / 100u) & 1u) == 0u;
		break;
	}
	(void)gpio_pin_set_dt(&l_led, on ? 1 : 0);
}

/* Waits ms while keeping the LED going. */
static void wait_ms(uint32_t ms)
{
	for (uint32_t waited = 0; waited < ms; waited += TICK_MS)
	{
		led_update(k_uptime_get_32());
		k_msleep(TICK_MS);
	}
}

/* The CAN tap: runs in the CAN thread, so it only queues. A full queue counts the frame as dropped, it never blocks. */
static void tap(bool tx, const tcan_frame_t *f)
{
	struct rec r = {
		.t_ms = k_uptime_get_32(),
		.id = (uint16_t)f->id,
		.tx = tx ? 1u : 0u,
		.dlc = f->dlc,
	};

	memcpy(r.data, f->data, sizeof(r.data));
	if (k_msgq_put(&l_queue, &r, K_NO_WAIT) != 0)
	{
		(void)atomic_inc(&l_dropped);
	}
}

/* Appends a formatted line to the buffer, writing the buffer out first if the line would not fit. Returns 0 or a negative error. */
static int flush_buffer(void)
{
	ssize_t w;

	if (l_len == 0)
	{
		return 0;
	}
	w = fs_write(&l_file, l_buf, l_len);
	if (w < 0 || (size_t)w != l_len)
	{
		LOG_ERR("write failed (%d)", (int)w);
		return w < 0 ? (int)w : -EIO;
	}
	l_bytes += (uint32_t)l_len;
	l_len = 0;
	logger_policy_written(&l_policy);
	return 0;
}

static int append(const char *line, size_t n, uint32_t now_ms)
{
	if (l_len + n > sizeof(l_buf))
	{
		int ret = flush_buffer();

		if (ret < 0)
		{
			return ret;
		}
	}
	memcpy(&l_buf[l_len], line, n);
	l_len += n;
	logger_policy_added(&l_policy, (uint32_t)n, now_ms);
	return 0;
}

/* Scans the card for the highest LOGnnnn.CSV, one entry at a time. */
static int next_log_number(unsigned *number)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	unsigned highest = 0;
	int ret;

	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, MOUNT_POINT);
	if (ret < 0)
	{
		return ret;
	}
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0')
	{
		if (ent.type == FS_DIR_ENTRY_FILE)
		{
			logger_note_file_name(&highest, ent.name);
		}
	}
	(void)fs_closedir(&dir);
	*number = logger_next_number(highest);
	return 0;
}

/* Read-only diagnostic after a mount failure: sector count and the first sector's OEM name and signature, so the log says whether
 * the card is exFAT ("EXFAT"), FAT ("MSDOS", "mkfs.fat") or unformatted (zeros), without ever writing to it. */
static void log_card_identity(void)
{
	static uint8_t sector[512];
	uint32_t count = 0;
	char oem[9] = {0};

	(void)disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &count);
	if (disk_access_read(DISK_NAME, sector, 0, 1) != 0) {
		LOG_ERR("card: %u sectors, sector 0 unreadable", (unsigned)count);
		return;
	}
	for (int i = 0; i < 8; i++) {
		oem[i] = (sector[3 + i] >= 32 && sector[3 + i] < 127) ? (char)sector[3 + i] : '.';
	}
	for (int i = 0; i < 4; i++) { /* MBR partition table: type byte and start LBA of each entry (type ee = GPT protective MBR) */
		const uint8_t *e = &sector[446 + 16 * i];
		LOG_ERR("card: partition %d type %02x start %u", i, e[4], (unsigned)(e[8] | e[9] << 8 | e[10] << 16 | (uint32_t)e[11] << 24));
	}
	LOG_ERR("card: %u sectors (%u MB), OEM \"%s\", sig %02x%02x, first bytes %02x %02x %02x", (unsigned)count,
			(unsigned)(count / 2048u), oem, sector[510], sector[511], sector[0], sector[1], sector[2]);
}

/* Mounts the card and opens a new log file with the header written. Returns 0, or sets the state and returns an error. */
static int open_log(void)
{
	char name[LOGGER_FILE_NAME_LEN];
	char path[sizeof(MOUNT_POINT) + LOGGER_FILE_NAME_LEN + 1];
	unsigned number;
	const char *header;
	int ret;

	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_CTRL_INIT, NULL);
	if (ret != 0)
	{
		set_state(SD_LOGGER_NO_CARD); /* the card did not answer: not inserted, or dead */
		return ret;
	}
	ret = fs_mount(&l_mount);
	if (ret < 0)
	{
		LOG_ERR("mount failed (%d): is the card FAT32?",
				ret); /* exFAT and unformatted cards end up here: never formatted */
		log_card_identity();
		(void)disk_access_ioctl(DISK_NAME, DISK_IOCTL_CTRL_DEINIT, NULL);
		set_state(SD_LOGGER_FS_ERROR);
		return ret;
	}
	ret = next_log_number(&number);
	if (ret < 0 || number == 0 || logger_file_name(name, sizeof(name), number) != 0)
	{
		LOG_ERR("no free log number (%d): nothing is written rather than overwriting", ret);
		(void)fs_unmount(&l_mount);
		set_state(SD_LOGGER_FS_ERROR);
		return ret < 0 ? ret : -ENOSPC;
	}
	(void)snprintf(path, sizeof(path), MOUNT_POINT "/%s", name);

	fs_file_t_init(&l_file);
	ret = fs_open(&l_file, path, FS_O_CREATE | FS_O_WRITE);
	if (ret < 0)
	{
		LOG_ERR("cannot create %s (%d)", path, ret);
		(void)fs_unmount(&l_mount);
		set_state(SD_LOGGER_FS_ERROR);
		return ret;
	}
	l_len = 0;
	l_lines = 0;
	l_bytes = 0;
	logger_policy_init(&l_policy, WRITE_BYTES, WRITE_AGE_MS, SYNC_MS, k_uptime_get_32());

	header = logger_csv_header();
	if (append(header, strlen(header), k_uptime_get_32()) < 0 || flush_buffer() < 0 || fs_sync(&l_file) < 0)
	{
		LOG_ERR("cannot write the header of %s", path);
		(void)fs_close(&l_file);
		(void)fs_unmount(&l_mount);
		set_state(SD_LOGGER_FS_ERROR);
		return -EIO;
	}
	logger_policy_synced(&l_policy, k_uptime_get_32());
	LOG_INF("logging to %s", path);
	set_state(SD_LOGGER_LOGGING);
	return 0;
}

static void close_log(void)
{
	(void)fs_close(&l_file);
	(void)fs_unmount(&l_mount);
	(void)disk_access_ioctl(DISK_NAME, DISK_IOCTL_CTRL_DEINIT, NULL);
}

/* Writes frames until something fails. Returns the negative error. */
static int run_logging(void)
{
	char line[LOGGER_LINE_MAX];
	atomic_val_t dropped_noted = 0;
	uint32_t last_note_ms = k_uptime_get_32();
	uint32_t last_status_ms = last_note_ms;
	int ret = 0;
	size_t n;

	n = logger_format_note(line, sizeof(line), last_note_ms, "start");
	ret = n > 0 ? append(line, n, last_note_ms) : 0;

	while (ret == 0)
	{
		struct rec r;
		uint32_t now;

		if (k_msgq_get(&l_queue, &r, K_MSEC(TICK_MS)) == 0)
		{
			n = logger_format_frame(
				line, sizeof(line), r.t_ms, r.tx ? LOGGER_DIR_TX : LOGGER_DIR_RX, r.id, r.dlc, r.data);
			if (n > 0)
			{
				ret = append(line, n, k_uptime_get_32());
				l_lines++;
			}
		}
		now = k_uptime_get_32();
		led_update(now);

		/* Say in the file when frames were lost because the queue was full. */
		if ((uint32_t)(now - last_note_ms) >= 1000u)
		{
			atomic_val_t d = atomic_get(&l_dropped);

			if (d != dropped_noted)
			{
				char text[32];

				(void)snprintf(text, sizeof(text), "dropped=%ld", (long)d);
				n = logger_format_note(line, sizeof(line), now, text);
				if (n > 0 && ret == 0)
				{
					ret = append(line, n, now);
				}
				dropped_noted = d;
			}
			last_note_ms = now;
		}
		if (ret == 0 && logger_policy_should_write(&l_policy, now))
		{
			ret = flush_buffer();
		}
		if (ret == 0 && logger_policy_should_sync(&l_policy, now))
		{
			ret = fs_sync(&l_file);
			if (ret < 0)
			{
				LOG_ERR("sync failed (%d)", ret);
			}
			else
			{
				logger_policy_synced(&l_policy, now);
			}
		}
		if ((uint32_t)(now - last_status_ms) >= STATUS_LOG_MS)
		{
			LOG_INF("logged %u lines, %u bytes, %ld dropped",
					(unsigned)l_lines,
					(unsigned)l_bytes,
					(long)atomic_get(&l_dropped));
			last_status_ms = now;
		}
	}
	return ret;
}

static void logger_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!gpio_is_ready_dt(&l_led) || gpio_pin_configure_dt(&l_led, GPIO_OUTPUT_INACTIVE) != 0)
	{
		LOG_WRN("status LED not available");
	}

	for (;;)
	{
		if (open_log() == 0)
		{
			int ret = run_logging();

			LOG_ERR("logging stopped (%d): the card was removed or failed", ret);
			close_log();
			set_state(SD_LOGGER_NO_CARD);
		}
		wait_ms(RETRY_MS); /* a card that is missing, or comes back, is tried again */
	}
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void sd_logger_start(void)
{
	datalogger_can_set_tap(tap);
	k_thread_create(&l_thread,
					l_stack,
					K_THREAD_STACK_SIZEOF(l_stack),
					logger_thread,
					NULL,
					NULL,
					NULL,
					THREAD_PRIORITY,
					0,
					K_NO_WAIT);
	k_thread_name_set(&l_thread, "sd_logger");
}

sd_logger_state_t sd_logger_state(void)
{
	return (sd_logger_state_t)atomic_get(&l_state);
}
