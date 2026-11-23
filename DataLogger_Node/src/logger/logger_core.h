/*******************************************************
 * @file logger_core.h
 * @brief The logic of the SD logger that can be tested without hardware: the CSV line format, the log file names, and when
 *        buffered data is written and synced. No Zephyr, no file system.
 *
 * File format (CSV, one record per line, a header first):
 *     t_ms,dir,id,dlc,data
 *     12345,TX,201,2,8401
 *     12360,RX,300,4,5585FF23
 *     1000,NOTE,,,dropped=12
 * t_ms is the uptime in milliseconds (there is no RTC yet). dir is RX or TX (this Node received or sent the frame) or NOTE
 * (a message from the logger itself). id is 3 hex digits for an 11-bit identifier, data is the payload as hex, 2 digits per
 * byte with no separators. Decoding the signals is done afterwards with can/trackcan.dbc.
 *******************************************************/
#ifndef LOGGER_CORE_H
#define LOGGER_CORE_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*******************************************************
 * Macros
 *******************************************************/
#define LOGGER_LINE_MAX 80u			 /* a line never needs more than this */
#define LOGGER_FILE_NUMBER_MAX 9999u /* LOG0001.CSV .. LOG9999.CSV */
#define LOGGER_FILE_NAME_LEN 12u	 /* "LOG0007.CSV" plus the NUL */

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef enum
{
	LOGGER_DIR_RX = 0,
	LOGGER_DIR_TX,
} logger_dir_t;

/* When to write the buffer to the file and when to flush the file to the card. */
typedef struct
{
	uint32_t write_bytes;  /* write as soon as this many bytes are buffered */
	uint32_t write_age_ms; /* ... or when the oldest buffered byte is this old */
	uint32_t sync_ms;	   /* flush the file to the card at least this often while there is unsynced data */
	uint32_t pending_bytes;
	uint32_t first_pending_ms;
	uint32_t last_sync_ms;
	bool unsynced; /* data has been written since the last sync */
} logger_policy_t;

/* When to start a new log file: after max_bytes written to this file or max_age_ms since it was opened, whichever comes first. A limit
 * of 0 is "no limit". The check is made after a write, so a file can exceed max_bytes by up to one write. */
typedef struct
{
	uint32_t max_bytes;
	uint32_t max_age_ms;
	uint32_t opened_ms;
	uint32_t bytes; /* written to this file so far (saturates, never wraps) */
} logger_rotation_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* The header line, with its newline. */
const char *logger_csv_header(void);

/*
 * One frame as a CSV line with its newline. dlc above 8 is clamped to 8. Returns the length (without the NUL), or 0 when it does not
 * fit in `size` (the buffer then holds an empty string).
 */
size_t logger_format_frame(
	char *buf, size_t size, uint32_t t_ms, logger_dir_t dir, uint32_t id, uint8_t dlc, const uint8_t data[8]);

/* A message from the logger itself. Commas and line breaks in `text` become spaces so the CSV stays valid. Returns as above. */
size_t logger_format_note(char *buf, size_t size, uint32_t t_ms, const char *text);

/* "LOG0007.CSV" for n = 7. Returns 0, or -1 for an n outside 1..9999 or a buffer shorter than LOGGER_FILE_NAME_LEN. */
int logger_file_name(char *buf, size_t size, unsigned n);

/* Folds one directory entry into the highest log number seen so far (start from 0), so a card with thousands of files needs no
 * list of names. Entries that are not LOGnnnn.CSV (exactly 4 digits, any case) are ignored. */
void logger_note_file_name(unsigned *highest, const char *name);

/* The number to use next after `highest` was found: highest + 1, or 0 when 9999 is already used (the logger must then not write
 * rather than overwrite anything). */
unsigned logger_next_number(unsigned highest);

/*
 * The number for the next log file from a directory listing: one more than the highest LOGnnnn.CSV (exactly 4 digits, any case),
 * 1 for an empty card. Other files are ignored. Returns 0 when 9999 is already used: the logger must then not write rather than
 * overwrite anything.
 */
unsigned logger_next_file_number(const char *const names[], size_t count);

void logger_policy_init(
	logger_policy_t *p, uint32_t write_bytes, uint32_t write_age_ms, uint32_t sync_ms, uint32_t now_ms);
void logger_policy_added(logger_policy_t *p, uint32_t bytes, uint32_t now_ms); /* bytes went into the buffer */
bool logger_policy_should_write(const logger_policy_t *p, uint32_t now_ms);
void logger_policy_written(logger_policy_t *p); /* the buffer went to the file */
bool logger_policy_should_sync(const logger_policy_t *p, uint32_t now_ms);
void logger_policy_synced(logger_policy_t *p, uint32_t now_ms);

/* A new file was opened at now_ms: the counters start from zero. */
void logger_rotation_start(logger_rotation_t *r, uint32_t max_bytes, uint32_t max_age_ms, uint32_t now_ms);
void logger_rotation_wrote(logger_rotation_t *r, uint32_t bytes); /* bytes went into the file */
bool logger_rotation_due(const logger_rotation_t *r, uint32_t now_ms);

#endif /* LOGGER_CORE_H */
