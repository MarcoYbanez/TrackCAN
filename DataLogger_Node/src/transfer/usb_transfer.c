/*******************************************************
 * @file usb_transfer.c
 * @brief Copies the SD logs to the Mac over the USB serial port. See usb_transfer.h and logger_xfer.h.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

#include "logger_xfer.h"
#include "sd_logger.h"
#include "usb_transfer.h"

LOG_MODULE_REGISTER(usb_transfer, LOG_LEVEL_INF);

/*******************************************************
 * Macros
 *******************************************************/
#define USB_SERIAL DT_NODELABEL(usb_serial)
#define THREAD_STACK_SIZE 4096
#define THREAD_PRIORITY 12 /* below the SD logger (10) and the CAN thread (7) */
#define RX_RING_SIZE 128
#define TX_RING_SIZE 4096
#define FILL_MAX 64
#define CHUNK_SIZE 4096 /* 512-byte fills, 32 KB reads and quieter SD logging were all measured and made no difference: the card read is the limit */
#define TX_STALL_MS 3000 /* the host stopped reading (port closed, cable out): give up the transfer */

/*******************************************************
 * Static Variables
 *******************************************************/
static const struct device *const l_uart = DEVICE_DT_GET(USB_SERIAL);
RING_BUF_DECLARE(l_rx_rb, RX_RING_SIZE);
RING_BUF_DECLARE(l_tx_rb, TX_RING_SIZE);
static K_SEM_DEFINE(l_rx_sem, 0, 1);
static K_SEM_DEFINE(l_tx_space, 0, 1);

K_THREAD_STACK_DEFINE(l_xfer_stack, THREAD_STACK_SIZE);
static struct k_thread l_thread;

static uint8_t l_chunk[CHUNK_SIZE];
static struct fs_file_t l_file;

/*******************************************************
 * Static Function Definitions
 *******************************************************/
/* Runs in the USB interrupt context: only moves bytes between the port and the two ring buffers. */
static void uart_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (true)
	{
		uart_irq_update(dev);
		if (uart_irq_is_pending(dev) <= 0)
		{
			break;
		}
		if (uart_irq_rx_ready(dev))
		{
			uint8_t buf[64];
			int n = uart_fifo_read(dev, buf, sizeof(buf));

			if (n > 0)
			{
				(void)ring_buf_put(&l_rx_rb, buf, (uint32_t)n); /* a full ring drops input: a command is short, the host waits for the reply */
				k_sem_give(&l_rx_sem);
			}
		}
		if (uart_irq_tx_ready(dev))
		{
			uint8_t *p;
			uint32_t len = ring_buf_get_claim(&l_tx_rb, &p, FILL_MAX);

			if (len == 0)
			{
				uart_irq_tx_disable(dev);
			}
			else
			{
				int sent = uart_fifo_fill(dev, p, (int)len);

				(void)ring_buf_get_finish(&l_tx_rb, sent > 0 ? (uint32_t)sent : 0u);
				k_sem_give(&l_tx_space);
			}
		}
	}
}

/* Queues bytes for the host, waiting while the ring is full. Returns 0, or -ETIMEDOUT if the host stopped reading (the ring is emptied). */
static int send_all(const uint8_t *data, size_t len)
{
	while (len > 0)
	{
		uint32_t put = ring_buf_put(&l_tx_rb, data, (uint32_t)len);

		data += put;
		len -= put;
		uart_irq_tx_enable(l_uart);
		if (len > 0 && k_sem_take(&l_tx_space, K_MSEC(TX_STALL_MS)) != 0)
		{
			uart_irq_tx_disable(l_uart);
			ring_buf_reset(&l_tx_rb);
			LOG_WRN("host stopped reading: transfer abandoned");
			return -ETIMEDOUT;
		}
	}
	return 0;
}

static void send_text(size_t n, const char *buf)
{
	if (n > 0)
	{
		(void)send_all((const uint8_t *)buf, n);
	}
}

static void send_err(const char *word)
{
	char b[24];

	send_text(xfer_format_err(b, sizeof(b), word), b);
}

static void do_list(void)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	char b[40];
	char open_name[XFER_NAME_MAX];
	bool has_open = sd_logger_open_file(open_name, sizeof(open_name));

	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, SD_LOGGER_MOUNT_POINT) < 0)
	{
		send_err("io");
		return;
	}
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0')
	{
		char name[XFER_NAME_MAX];

		/* Only LOGnnnn.CSV, Dnnnnnnn.CSV and Mnnnnnnn.CSV (the same strict test as a GET); never the one being written. */
		if (ent.type != FS_DIR_ENTRY_FILE || !xfer_name_valid(ent.name, name) || (has_open && strcmp(name, open_name) == 0))
		{
			continue;
		}
		send_text(xfer_format_list_entry(b, sizeof(b), name, (uint32_t)ent.size), b);
	}
	(void)fs_closedir(&dir);
	send_text(xfer_format_end(b, sizeof(b)), b);
}

/* Sends one file. The caller holds the serving lock, so the ring cannot delete it meanwhile. */
static void get_file(const char *name)
{
	char path[sizeof(SD_LOGGER_MOUNT_POINT) + XFER_NAME_MAX];
	char b[40];
	char open_name[XFER_NAME_MAX];
	struct fs_dirent st;
	uint32_t crc = 0;
	uint32_t sent = 0;
	uint32_t read_us = 0;
	uint32_t send_us = 0;
	int n;

	if (sd_logger_open_file(open_name, sizeof(open_name)) && strcmp(open_name, name) == 0)
	{
		send_err("open"); /* the file being written is never served */
		return;
	}
	(void)snprintf(path, sizeof(path), SD_LOGGER_MOUNT_POINT "/%s", name);
	n = fs_stat(path, &st);
	if (n < 0 || st.type != FS_DIR_ENTRY_FILE)
	{
		LOG_WRN("GET %s: not found (%d)", name, n);
		send_err("notfound");
		return;
	}
	fs_file_t_init(&l_file);
	n = fs_open(&l_file, path, FS_O_READ);
	if (n < 0)
	{
		LOG_ERR("GET %s: cannot open for reading (%d)", name, n); /* so a failure like this is never silent */
		send_err("open");
		return;
	}
	send_text(xfer_format_ok(b, sizeof(b), (uint32_t)st.size), b);
	while (sent < (uint32_t)st.size)
	{
		uint32_t t0 = k_cycle_get_32();

		n = (int)fs_read(&l_file, l_chunk, sizeof(l_chunk));
		read_us += k_cyc_to_us_floor32(k_cycle_get_32() - t0);
		if (n <= 0)
		{
			break;
		}
		if ((uint32_t)n > (uint32_t)st.size - sent)
		{
			n = (int)((uint32_t)st.size - sent); /* never more than the size that was announced */
		}
		t0 = k_cycle_get_32();
		crc = xfer_crc32_update(crc, l_chunk, (size_t)n);
		if (send_all(l_chunk, (size_t)n) < 0)
		{
			(void)fs_close(&l_file);
			return;
		}
		send_us += k_cyc_to_us_floor32(k_cycle_get_32() - t0);
		sent += (uint32_t)n;
	}
	LOG_INF("GET %s: %u bytes, reading the card %u ms, CRC and USB sending %u ms", name, (unsigned)sent, (unsigned)(read_us / 1000u), (unsigned)(send_us / 1000u));
	(void)fs_close(&l_file);
	if (sent != (uint32_t)st.size)
	{
		send_err("io"); /* the host sees fewer bytes than announced, or a wrong CRC */
		return;
	}
	send_text(xfer_format_crc(b, sizeof(b), crc), b);
}

static void do_get(const char *name)
{
	sd_logger_serving_lock();
	get_file(name);
	sd_logger_serving_unlock();
}

static void transfer_thread(void *p1, void *p2, void *p3)
{
	xfer_line_t line;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	xfer_line_init(&line);
	uart_irq_callback_user_data_set(l_uart, uart_cb, NULL);
	uart_irq_rx_enable(l_uart);

	for (;;)
	{
		uint8_t byte;
		xfer_request_t req;

		if (ring_buf_get(&l_rx_rb, &byte, 1) == 0)
		{
			(void)k_sem_take(&l_rx_sem, K_FOREVER);
			continue;
		}
		if (!xfer_line_push(&line, byte, &req))
		{
			continue;
		}
		switch (req.cmd)
		{
		case XFER_CMD_LIST:
			do_list();
			break;
		case XFER_CMD_GET:
			do_get(req.name);
			break;
		default:
			send_err("badcmd");
			break;
		}
	}
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int usb_transfer_start(void)
{
	if (!device_is_ready(l_uart))
	{
		return -ENODEV;
	}
	k_thread_create(&l_thread, l_xfer_stack, K_THREAD_STACK_SIZEOF(l_xfer_stack), transfer_thread, NULL, NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&l_thread, "usb_transfer");
	return 0;
}
