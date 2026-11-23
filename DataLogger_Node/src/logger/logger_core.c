/*******************************************************
 * @file logger_core.c
 * @brief The logic of the SD logger that can be tested without hardware. See logger_core.h.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "logger_core.h"

#include <stdio.h>
#include <string.h>

/*******************************************************
 * Static Variables
 *******************************************************/
static const char k_header[] = "t_ms,dir,id,dlc,data\n";
static const char k_hex[] = "0123456789ABCDEF";

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static char upper(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
const char *logger_csv_header(void)
{
	return k_header;
}

size_t logger_format_frame(
	char *buf, size_t size, uint32_t t_ms, logger_dir_t dir, uint32_t id, uint8_t dlc, const uint8_t data[8])
{
	uint8_t n = dlc > 8u ? 8u : dlc;
	int head;
	size_t len;

	if (size == 0)
	{
		return 0;
	}
	buf[0] = '\0';
	head = snprintf(buf,
					size,
					"%lu,%s,%03lX,%u,",
					(unsigned long)t_ms,
					dir == LOGGER_DIR_TX ? "TX" : "RX",
					(unsigned long)id,
					(unsigned)n);
	if (head < 0)
	{
		buf[0] = '\0';
		return 0;
	}
	len = (size_t)head;
	if (len + (size_t)n * 2u + 2u > size)
	{ /* data, newline and the NUL */
		buf[0] = '\0';
		return 0;
	}
	for (uint8_t i = 0; i < n; i++)
	{
		buf[len++] = k_hex[data[i] >> 4];
		buf[len++] = k_hex[data[i] & 0x0Fu];
	}
	buf[len++] = '\n';
	buf[len] = '\0';
	return len;
}

size_t logger_format_note(char *buf, size_t size, uint32_t t_ms, const char *text)
{
	int head;
	size_t len;

	if (size == 0)
	{
		return 0;
	}
	buf[0] = '\0';
	head = snprintf(buf, size, "%lu,NOTE,,,", (unsigned long)t_ms);
	if (head < 0)
	{
		buf[0] = '\0';
		return 0;
	}
	len = (size_t)head;
	for (const char *c = text; *c != '\0'; c++)
	{
		if (len + 2u > size)
		{ /* this character, a newline and the NUL do not fit */
			buf[0] = '\0';
			return 0;
		}
		buf[len++] = (*c == ',' || *c == '\n' || *c == '\r') ? ' ' : *c;
	}
	if (len + 2u > size)
	{
		buf[0] = '\0';
		return 0;
	}
	buf[len++] = '\n';
	buf[len] = '\0';
	return len;
}

int logger_file_name(char *buf, size_t size, unsigned n)
{
	if (n < 1u || n > LOGGER_FILE_NUMBER_MAX || size < LOGGER_FILE_NAME_LEN)
	{
		return -1;
	}
	(void)snprintf(buf, size, "LOG%04u.CSV", n);
	return 0;
}

void logger_note_file_name(unsigned *highest, const char *name)
{
	unsigned n = 0;
	bool ok = name != NULL && strlen(name) == 11u && upper(name[0]) == 'L' && upper(name[1]) == 'O' &&
		  upper(name[2]) == 'G' && name[7] == '.' && upper(name[8]) == 'C' && upper(name[9]) == 'S' &&
		  upper(name[10]) == 'V';

	for (int d = 3; ok && d < 7; d++)
	{
		if (name[d] < '0' || name[d] > '9')
		{
			ok = false;
		}
		else
		{
			n = n * 10u + (unsigned)(name[d] - '0');
		}
	}
	if (ok && n > *highest)
	{
		*highest = n;
	}
}

unsigned logger_next_number(unsigned highest)
{
	return highest >= LOGGER_FILE_NUMBER_MAX ? 0u : highest + 1u;
}

unsigned logger_next_file_number(const char *const names[], size_t count)
{
	unsigned highest = 0;

	for (size_t i = 0; i < count; i++)
	{
		logger_note_file_name(&highest, names[i]);
	}
	return logger_next_number(highest);
}

void logger_policy_init(
	logger_policy_t *p, uint32_t write_bytes, uint32_t write_age_ms, uint32_t sync_ms, uint32_t now_ms)
{
	memset(p, 0, sizeof(*p));
	p->write_bytes = write_bytes;
	p->write_age_ms = write_age_ms;
	p->sync_ms = sync_ms;
	p->last_sync_ms = now_ms;
}

void logger_policy_added(logger_policy_t *p, uint32_t bytes, uint32_t now_ms)
{
	if (bytes == 0)
	{
		return;
	}
	if (p->pending_bytes == 0)
	{
		p->first_pending_ms = now_ms;
	}
	p->pending_bytes += bytes;
}

bool logger_policy_should_write(const logger_policy_t *p, uint32_t now_ms)
{
	if (p->pending_bytes == 0)
	{
		return false;
	}
	return p->pending_bytes >= p->write_bytes || (uint32_t)(now_ms - p->first_pending_ms) >= p->write_age_ms;
}

void logger_policy_written(logger_policy_t *p)
{
	if (p->pending_bytes > 0)
	{
		p->unsynced = true;
	}
	p->pending_bytes = 0;
}

bool logger_policy_should_sync(const logger_policy_t *p, uint32_t now_ms)
{
	return p->unsynced && (uint32_t)(now_ms - p->last_sync_ms) >= p->sync_ms;
}

void logger_policy_synced(logger_policy_t *p, uint32_t now_ms)
{
	p->unsynced = false;
	p->last_sync_ms = now_ms;
}

void logger_rotation_start(logger_rotation_t *r, uint32_t max_bytes, uint32_t max_age_ms, uint32_t now_ms)
{
	r->max_bytes = max_bytes;
	r->max_age_ms = max_age_ms;
	r->opened_ms = now_ms;
	r->bytes = 0;
}

void logger_rotation_wrote(logger_rotation_t *r, uint32_t bytes)
{
	r->bytes = (bytes > UINT32_MAX - r->bytes) ? UINT32_MAX : r->bytes + bytes;
}

bool logger_rotation_due(const logger_rotation_t *r, uint32_t now_ms)
{
	if (r->max_bytes != 0u && r->bytes >= r->max_bytes)
	{
		return true;
	}
	return r->max_age_ms != 0u && (uint32_t)(now_ms - r->opened_ms) >= r->max_age_ms;
}
