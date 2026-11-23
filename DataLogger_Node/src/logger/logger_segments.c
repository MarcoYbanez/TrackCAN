/*******************************************************
 * @file logger_segments.c
 * @brief The rules for the daily and manual logging modes. See logger_segments.h.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "logger_segments.h"

#include <stdio.h>
#include <string.h>

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static char family_letter(seg_family_t family)
{
	return family == SEG_MANUAL ? 'M' : 'D';
}

static char upper(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int seg_file_name(char *buf, size_t size, seg_family_t family, unsigned number)
{
	if (number < 1u || number > SEG_FILE_NUMBER_MAX || size < SEG_FILE_NAME_LEN)
	{
		return -1;
	}
	(void)snprintf(buf, size, "%c%07u.CSV", family_letter(family), number);
	return 0;
}

bool seg_parse_name(const char *name, seg_family_t family, unsigned *number)
{
	unsigned n = 0;

	if (strlen(name) != 12u || upper(name[0]) != family_letter(family) || name[8] != '.' || upper(name[9]) != 'C' || upper(name[10]) != 'S'
		|| upper(name[11]) != 'V')
	{
		return false;
	}
	for (size_t i = 1; i <= 7; i++)
	{
		if (name[i] < '0' || name[i] > '9')
		{
			return false;
		}
		n = n * 10u + (unsigned)(name[i] - '0');
	}
	if (n < 1u)
	{
		return false;
	}
	*number = n;
	return true;
}

void seg_ring_init(seg_ring_t *r)
{
	memset(r, 0, sizeof(*r));
}

void seg_ring_fold(seg_ring_t *r, unsigned number, uint32_t bytes)
{
	r->total_bytes += bytes;
	r->count++;
	if (r->lowest == 0u || number < r->lowest)
	{
		r->lowest = number;
		r->lowest_bytes = bytes;
	}
	if (number > r->highest)
	{
		r->highest = number;
	}
}

bool seg_ring_over(const seg_ring_t *r, uint64_t quota_bytes)
{
	return r->count > 1u && r->total_bytes > quota_bytes;
}

unsigned seg_ring_next_number(const seg_ring_t *r)
{
	return r->highest < SEG_FILE_NUMBER_MAX ? r->highest + 1u : 0u;
}

void seg_decim_init(seg_decim_t *d)
{
	memset(d, 0, sizeof(*d));
}

bool seg_decim_keep(seg_decim_t *d, bool tx, uint32_t id, uint32_t now_ms, uint32_t interval_ms)
{
	unsigned dir = tx ? 1u : 0u;
	unsigned i = (unsigned)(id & (SEG_ID_COUNT - 1u));
	uint8_t bit = (uint8_t)(1u << (i & 7u));

	if ((d->seen[dir][i >> 3] & bit) == 0u || (uint32_t)(now_ms - d->last_ms[dir][i]) >= interval_ms)
	{
		d->seen[dir][i >> 3] |= bit;
		d->last_ms[dir][i] = now_ms;
		return true;
	}
	return false;
}

void seg_button_init(seg_button_t *b, uint32_t debounce_ms, uint32_t stale_ms)
{
	memset(b, 0, sizeof(*b));
	b->debounce_ms = debounce_ms;
	b->stale_ms = stale_ms;
}

bool seg_button_frame(seg_button_t *b, bool pressed, uint32_t now_ms)
{
	bool toggle = false;

	b->last_frame_ms = now_ms;
	if (!b->seen)
	{
		/* The first frame: a button that is already down does not count as a press, it must be released first (a press is a rising edge). */
		b->seen = true;
		b->pressed = pressed;
		return false;
	}
	if (pressed && !b->pressed && (!b->toggled_once || (uint32_t)(now_ms - b->last_toggle_ms) >= b->debounce_ms))
	{
		toggle = true;
		b->toggled_once = true;
		b->last_toggle_ms = now_ms;
	}
	b->pressed = pressed;
	return toggle;
}

void seg_button_tick(seg_button_t *b, uint32_t now_ms)
{
	if (b->seen && (uint32_t)(now_ms - b->last_frame_ms) >= b->stale_ms)
	{
		b->seen = false; /* the next frame is a first frame again */
	}
}

void seg_mode_init(seg_mode_ctl_t *m, uint32_t manual_max_ms)
{
	m->mode = SEG_MODE_DAILY;
	m->manual_start_ms = 0;
	m->manual_max_ms = manual_max_ms;
}

void seg_mode_toggle(seg_mode_ctl_t *m, uint32_t now_ms)
{
	if (m->mode == SEG_MODE_DAILY)
	{
		m->mode = SEG_MODE_MANUAL;
		m->manual_start_ms = now_ms;
	}
	else
	{
		m->mode = SEG_MODE_DAILY;
	}
}

bool seg_mode_tick(seg_mode_ctl_t *m, uint32_t now_ms)
{
	if (m->mode == SEG_MODE_MANUAL && m->manual_max_ms != 0u && (uint32_t)(now_ms - m->manual_start_ms) >= m->manual_max_ms)
	{
		m->mode = SEG_MODE_DAILY;
		return true;
	}
	return false;
}
