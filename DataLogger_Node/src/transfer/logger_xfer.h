/*******************************************************
 * @file logger_xfer.h
 * @brief The command protocol for copying SD logs to the Mac over the USB serial port (TCAN-21): the parser, the replies and the CRC-32.
 *        No Zephyr, no file system: tested on the host.
 *
 * Host -> Node, one command per line (CR or LF ends it):
 *     LIST                  the closed log files
 *     GET LOG0005.CSV       one closed log file
 * Node -> host:
 *     LIST:   one "LOG0005.CSV 1234567" line per file (name, size in bytes), then "END"
 *     GET:    "OK 1234567", exactly that many raw bytes, then "CRC 1a2b3c4d" (CRC-32 of the bytes, the same as zlib.crc32)
 *     errors: "ERR <word>"  (badcmd, notfound, open, io)
 * Read-only by design: there is no command that writes or deletes anything, and only names of the form LOGnnnn.CSV are accepted
 * (no paths), so a command can never reach any other file.
 *******************************************************/
#ifndef LOGGER_XFER_H
#define LOGGER_XFER_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*******************************************************
 * Macros
 *******************************************************/
#define XFER_LINE_MAX 32u /* the longest command is "GET LOG0005.CSV" (15 characters) */

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef enum
{
	XFER_CMD_LIST = 0,
	XFER_CMD_GET,
	XFER_CMD_BAD, /* not a command, a name that is not LOGnnnn.CSV, or a line that was too long */
} xfer_cmd_t;

typedef struct
{
	xfer_cmd_t cmd;
	unsigned number; /* GET: the file number 1..9999 */
} xfer_request_t;

/* Collects bytes into lines. Bytes beyond XFER_LINE_MAX are dropped, and because no valid command is that long the line is then reported
 * as XFER_CMD_BAD, whatever it started with. */
typedef struct
{
	char text[XFER_LINE_MAX + 1u];
	size_t len;
} xfer_line_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

void xfer_line_init(xfer_line_t *l);

/*
 * Feeds one received byte. Returns true when a line is complete (a CR or LF after at least one character) and the
 * request is in *out. Empty lines are ignored, so CR LF from a terminal gives one request.
 */
bool xfer_line_push(xfer_line_t *l, uint8_t byte, xfer_request_t *out);

/* Parses one line (no line ending). "LIST" and "GET LOGnnnn.CSV" (exactly 4 digits, 1..9999, upper or lower case) are the only commands. */
xfer_request_t xfer_parse(const char *line);

/* Replies, each ending in a newline. Return the length, or 0 if it does not fit in `size` (the buffer then holds an empty string). */
size_t xfer_format_list_entry(char *buf, size_t size, unsigned number, uint32_t bytes);
size_t xfer_format_end(char *buf, size_t size);
size_t xfer_format_ok(char *buf, size_t size, uint32_t bytes);
size_t xfer_format_crc(char *buf, size_t size, uint32_t crc);
size_t xfer_format_err(char *buf, size_t size, const char *word);

/* CRC-32 (the zlib/PNG polynomial). Start with crc = 0 and feed the data in any number of pieces; the result is the same as for one piece. */
uint32_t xfer_crc32_update(uint32_t crc, const uint8_t *data, size_t len);

#endif /* LOGGER_XFER_H */
