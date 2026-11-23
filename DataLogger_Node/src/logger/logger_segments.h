/*******************************************************
 * @file logger_segments.h
 * @brief The rules for two logging modes (TCAN-20), as pure logic with no Zephyr and no file system: the file names, the ring with a space
 *        quota per mode, the daily low-rate reduction, the button (edge, debounce, staleness) and the mode switch with its automatic stop.
 *
 * Two families of files, each with its own number sequence and its own space budget:
 *     Dnnnnnnn.CSV   daily: about one frame per CAN ID per second, always on
 *     Mnnnnnnn.CSV   manual (high resolution): every frame, between a button press and the next press (or the automatic stop)
 * The old LOGnnnn.CSV files are never touched: only a file whose name matches a family's pattern is ever counted or deleted.
 *******************************************************/
#ifndef LOGGER_SEGMENTS_H
#define LOGGER_SEGMENTS_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*******************************************************
 * Macros
 *******************************************************/
#define SEG_FILE_NUMBER_MAX 9999999u /* D0000001.CSV .. D9999999.CSV */
#define SEG_FILE_NAME_LEN 13u		 /* "D0000007.CSV" plus the NUL */
#define SEG_ID_COUNT 2048u			 /* 11-bit identifiers */

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef enum
{
	SEG_DAILY = 0,
	SEG_MANUAL,
	SEG_COUNT,
} seg_family_t;

/* The ring of one family: folded from a directory listing, one entry at a time. */
typedef struct
{
	uint64_t total_bytes;
	unsigned count;
	unsigned lowest;		/* the oldest file's number (the smallest), 0 when there is no file */
	uint32_t lowest_bytes;
	unsigned highest;		/* the newest file's number, 0 when there is no file */
} seg_ring_t;

/* Daily low-rate reduction: remembers when each (direction, identifier) was last kept. */
typedef struct
{
	uint32_t last_ms[2][SEG_ID_COUNT];
	uint8_t seen[2][SEG_ID_COUNT / 8u];
} seg_decim_t;

/* The button: a level that arrives in CAN frames, turned into one toggle per press. */
typedef struct
{
	bool seen;			/* at least one frame since init or since the frames stopped */
	bool pressed;		/* the last level */
	uint32_t last_frame_ms;
	uint32_t last_toggle_ms;
	bool toggled_once;
	uint32_t debounce_ms;
	uint32_t stale_ms;
} seg_button_t;

typedef enum
{
	SEG_MODE_DAILY = 0,
	SEG_MODE_MANUAL,
} seg_mode_t;

typedef struct
{
	seg_mode_t mode;
	uint32_t manual_start_ms;
	uint32_t manual_max_ms; /* 0 = no automatic stop */
} seg_mode_ctl_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* "D0000007.CSV" for (SEG_DAILY, 7). Returns 0, or -1 for a number outside 1..SEG_FILE_NUMBER_MAX or a short buffer. */
int seg_file_name(char *buf, size_t size, seg_family_t family, unsigned number);

/* Parses a directory entry name: true only for exactly the family's letter, 7 digits (1..9999999) and ".CSV" (any case). Anything else, including
 * LOGnnnn.CSV and the other family's files, is false: it is not this family's to count or delete. */
bool seg_parse_name(const char *name, seg_family_t family, unsigned *number);

void seg_ring_init(seg_ring_t *r);
void seg_ring_fold(seg_ring_t *r, unsigned number, uint32_t bytes); /* one file of the family, in any order */
/* True when the family holds more than quota_bytes AND has more than one file: the newest file is the open one and is never a candidate. */
bool seg_ring_over(const seg_ring_t *r, uint64_t quota_bytes);
unsigned seg_ring_next_number(const seg_ring_t *r); /* highest + 1, or 0 when SEG_FILE_NUMBER_MAX is already used (stop rather than overwrite) */

void seg_decim_init(seg_decim_t *d);
/* True when this frame is kept in the daily log: the first one of an (direction, identifier), then one per interval_ms. Wrap-safe. */
bool seg_decim_keep(seg_decim_t *d, bool tx, uint32_t id, uint32_t now_ms, uint32_t interval_ms);

void seg_button_init(seg_button_t *b, uint32_t debounce_ms, uint32_t stale_ms);
/* A button frame arrived. Returns true for exactly one accepted press (the rising edge). A button already pressed at the first frame
 * does not toggle: it must be released first. Edges closer than debounce_ms to the last toggle are ignored. */
bool seg_button_frame(seg_button_t *b, bool pressed, uint32_t now_ms);
/* Call regularly: when the frames have stopped for stale_ms the button is forgotten, as if no frame had ever been seen. */
void seg_button_tick(seg_button_t *b, uint32_t now_ms);

void seg_mode_init(seg_mode_ctl_t *m, uint32_t manual_max_ms);
void seg_mode_toggle(seg_mode_ctl_t *m, uint32_t now_ms);
/* Returns true once, when the manual mode ended by itself because manual_max_ms has passed. */
bool seg_mode_tick(seg_mode_ctl_t *m, uint32_t now_ms);

#endif /* LOGGER_SEGMENTS_H */
