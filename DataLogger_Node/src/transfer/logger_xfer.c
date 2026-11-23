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

/* An overlong line must never look like a command: it is cut at XFER_LINE_MAX, and the longest command ("GET D0000007.CSV") is 16 characters. */
_Static_assert(XFER_LINE_MAX > 16u, "XFER_LINE_MAX must be longer than any command");

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

bool xfer_name_valid(const char *name, char out[XFER_NAME_MAX])
{
	size_t len = strlen(name);
	size_t first; /* index of the first digit */
	size_t digits;
	unsigned long n = 0;

	if (len == 11u && upper(name[0]) == 'L' && upper(name[1]) == 'O' && upper(name[2]) == 'G')
	{
		first = 3;
		digits = 4;
	}
	else if (len == 12u && (upper(name[0]) == 'D' || upper(name[0]) == 'M'))
	{
		first = 1;
		digits = 7;
	}
	else
	{
		return false;
	}
	if (name[first + digits] != '.' || upper(name[first + digits + 1]) != 'C' || upper(name[first + digits + 2]) != 'S'
		|| upper(name[first + digits + 3]) != 'V')
	{
		return false;
	}
	for (size_t i = first; i < first + digits; i++)
	{
		if (name[i] < '0' || name[i] > '9')
		{
			return false;
		}
		n = n * 10u + (unsigned long)(name[i] - '0');
	}
	if (n < 1u)
	{
		return false;
	}
	for (size_t i = 0; i <= len; i++)
	{
		out[i] = upper(name[i]);
	}
	return true;
}

xfer_request_t xfer_parse(const char *line)
{
	xfer_request_t r;

	memset(&r, 0, sizeof(r));
	r.cmd = XFER_CMD_BAD;
	if (strcmp(line, "LIST") == 0)
	{
		r.cmd = XFER_CMD_LIST;
	}
	else if (strncmp(line, "GET ", 4) == 0 && xfer_name_valid(line + 4, r.name))
	{
		r.cmd = XFER_CMD_GET;
	}
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

size_t xfer_format_list_entry(char *buf, size_t size, const char *name, uint32_t bytes)
{
	return finish(buf, size, snprintf(buf, size, "%s %lu\n", name, (unsigned long)bytes));
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
