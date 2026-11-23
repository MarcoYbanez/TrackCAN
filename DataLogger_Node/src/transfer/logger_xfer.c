/*******************************************************
 * @file logger_xfer.c
 * @brief The USB log copy protocol's parser, replies and CRC-32. See logger_xfer.h.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "logger_xfer.h"

#include <stdio.h>
#include <string.h>

/* An overlong line must never look like a command: it is cut at XFER_LINE_MAX, and the longest command ("GET LOG0005.CSV") is 15 characters. */
_Static_assert(XFER_LINE_MAX > 15u, "XFER_LINE_MAX must be longer than any command");

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static char upper(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static size_t finish(char *buf, size_t size, int n)
{
	if (n < 0 || (size_t)n >= size)
	{
		if (size > 0)
		{
			buf[0] = '\0';
		}
		return 0;
	}
	return (size_t)n;
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void xfer_line_init(xfer_line_t *l)
{
	l->len = 0;
	l->text[0] = '\0';
}

xfer_request_t xfer_parse(const char *line)
{
	xfer_request_t r = { XFER_CMD_BAD, 0 };
	unsigned n = 0;

	if (strcmp(line, "LIST") == 0)
	{
		r.cmd = XFER_CMD_LIST;
		return r;
	}
	/* "GET LOGnnnn.CSV": exactly 15 characters. */
	if (strlen(line) != 15u || strncmp(line, "GET ", 4) != 0 || upper(line[4]) != 'L' || upper(line[5]) != 'O' || upper(line[6]) != 'G'
		|| line[11] != '.' || upper(line[12]) != 'C' || upper(line[13]) != 'S' || upper(line[14]) != 'V')
	{
		return r;
	}
	for (size_t i = 7; i < 11; i++)
	{
		if (line[i] < '0' || line[i] > '9')
		{
			return r;
		}
		n = n * 10u + (unsigned)(line[i] - '0');
	}
	if (n < 1u || n > 9999u)
	{
		return r;
	}
	r.cmd = XFER_CMD_GET;
	r.number = n;
	return r;
}

bool xfer_line_push(xfer_line_t *l, uint8_t byte, xfer_request_t *out)
{
	if (byte == '\r' || byte == '\n')
	{
		if (l->len == 0)
		{
			return false; /* an empty line: the second half of CR LF */
		}
		l->text[l->len] = '\0';
		*out = xfer_parse(l->text);
		xfer_line_init(l);
		return true;
	}
	if (l->len < XFER_LINE_MAX)
	{
		l->text[l->len++] = (char)byte;
	}
	return false;
}

size_t xfer_format_list_entry(char *buf, size_t size, unsigned number, uint32_t bytes)
{
	return finish(buf, size, snprintf(buf, size, "LOG%04u.CSV %lu\n", number, (unsigned long)bytes));
}

size_t xfer_format_end(char *buf, size_t size)
{
	return finish(buf, size, snprintf(buf, size, "END\n"));
}

size_t xfer_format_ok(char *buf, size_t size, uint32_t bytes)
{
	return finish(buf, size, snprintf(buf, size, "OK %lu\n", (unsigned long)bytes));
}

size_t xfer_format_crc(char *buf, size_t size, uint32_t crc)
{
	return finish(buf, size, snprintf(buf, size, "CRC %08lx\n", (unsigned long)crc));
}

size_t xfer_format_err(char *buf, size_t size, const char *word)
{
	return finish(buf, size, snprintf(buf, size, "ERR %s\n", word));
}

uint32_t xfer_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
	crc = ~crc;
	for (size_t i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (int b = 0; b < 8; b++)
		{
			crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
		}
	}
	return ~crc;
}
