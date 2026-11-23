/*******************************************************
 * @file test_logger_segments.c
 * @brief Host tests for the daily and manual logging rules. Expected values are worked out by hand.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger_segments.h"

/*******************************************************
 * Macros
 *******************************************************/
#define CHECK(cond)                                                                                                    \
	do                                                                                                                 \
	{                                                                                                                  \
		l_checks++;                                                                                                    \
		if (!(cond))                                                                                                   \
		{                                                                                                              \
			l_failures++;                                                                                              \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
		}                                                                                                              \
	} while (0)

/*******************************************************
 * Static Variables
 *******************************************************/
static int l_checks;
static int l_failures;

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void test_names(void)
{
	char b[SEG_FILE_NAME_LEN];
	unsigned n = 0;

	CHECK(seg_file_name(b, sizeof(b), SEG_DAILY, 7) == 0 && strcmp(b, "D0000007.CSV") == 0);
	CHECK(seg_file_name(b, sizeof(b), SEG_MANUAL, 7) == 0 && strcmp(b, "M0000007.CSV") == 0);
	CHECK(seg_file_name(b, sizeof(b), SEG_DAILY, 9999999) == 0 && strcmp(b, "D9999999.CSV") == 0);
	CHECK(seg_file_name(b, sizeof(b), SEG_DAILY, 1) == 0 && strcmp(b, "D0000001.CSV") == 0);
	CHECK(seg_file_name(b, sizeof(b), SEG_DAILY, 0) == -1);
	CHECK(seg_file_name(b, sizeof(b), SEG_DAILY, 10000000) == -1);
	CHECK(seg_file_name(b, SEG_FILE_NAME_LEN - 1u, SEG_DAILY, 7) == -1); /* one byte short */
	CHECK(seg_file_name(b, SEG_FILE_NAME_LEN, SEG_DAILY, 7) == 0);		 /* exactly enough */

	CHECK(seg_parse_name("D0000007.CSV", SEG_DAILY, &n) && n == 7u);
	CHECK(seg_parse_name("d0001234.csv", SEG_DAILY, &n) && n == 1234u); /* any case */
	CHECK(seg_parse_name("M9999999.CSV", SEG_MANUAL, &n) && n == 9999999u);
	CHECK(seg_parse_name("D0000001.CSV", SEG_DAILY, &n) && n == 1u);
	CHECK(!seg_parse_name("D0000000.CSV", SEG_DAILY, &n)); /* number 0 */
	CHECK(!seg_parse_name("D0000007.CSV", SEG_MANUAL, &n)); /* the other family's file is not mine */
	CHECK(!seg_parse_name("M0000007.CSV", SEG_DAILY, &n));
	CHECK(!seg_parse_name("LOG0007.CSV", SEG_DAILY, &n)); /* the old logs are never touched */
	CHECK(!seg_parse_name("LOG0007.CSV", SEG_MANUAL, &n));
	CHECK(!seg_parse_name("D000007.CSV", SEG_DAILY, &n));	 /* 6 digits */
	CHECK(!seg_parse_name("D00000007.CSV", SEG_DAILY, &n)); /* 8 digits */
	CHECK(!seg_parse_name("D00000A7.CSV", SEG_DAILY, &n));
	CHECK(!seg_parse_name("D0000007.TXT", SEG_DAILY, &n));
	CHECK(!seg_parse_name("D0000007CSV", SEG_DAILY, &n));
	CHECK(!seg_parse_name("D0000007.CSVX", SEG_DAILY, &n));
	CHECK(!seg_parse_name("", SEG_DAILY, &n));
	CHECK(!seg_parse_name("D0000007.CS", SEG_DAILY, &n));
	CHECK(!seg_parse_name("../0000007.CSV", SEG_DAILY, &n));
}

static void test_ring(void)
{
	seg_ring_t r;

	seg_ring_init(&r);
	CHECK(r.count == 0u && r.total_bytes == 0u && r.lowest == 0u && r.highest == 0u);
	CHECK(seg_ring_next_number(&r) == 1u);	 /* an empty family starts at 1 */
	CHECK(!seg_ring_over(&r, 0));

	/* One file, however big, is never a candidate: it is the open one. */
	seg_ring_fold(&r, 5, 4000000000u);
	CHECK(!seg_ring_over(&r, 1000));
	CHECK(r.lowest == 5u && r.highest == 5u && seg_ring_next_number(&r) == 6u);

	/* Several files in any order: lowest, highest and the 64-bit total. */
	seg_ring_fold(&r, 9, 4000000000u);
	seg_ring_fold(&r, 3, 1500u);
	seg_ring_fold(&r, 7, 4000000000u);
	CHECK(r.count == 4u);
	CHECK(r.lowest == 3u && r.lowest_bytes == 1500u);
	CHECK(r.highest == 9u && seg_ring_next_number(&r) == 10u);
	CHECK(r.total_bytes == 12000001500ull); /* more than 2^32: no wrap */

	/* Over means strictly more than the quota. */
	CHECK(seg_ring_over(&r, 12000001499ull));
	CHECK(!seg_ring_over(&r, 12000001500ull));
	CHECK(!seg_ring_over(&r, 13000000000ull));

	/* The highest number used stops the logger instead of reusing a name. */
	seg_ring_init(&r);
	seg_ring_fold(&r, SEG_FILE_NUMBER_MAX, 10);
	CHECK(seg_ring_next_number(&r) == 0u);
	seg_ring_init(&r);
	seg_ring_fold(&r, SEG_FILE_NUMBER_MAX - 1u, 10);
	CHECK(seg_ring_next_number(&r) == SEG_FILE_NUMBER_MAX);

	/* The lowest is found whatever the order. */
	seg_ring_init(&r);
	seg_ring_fold(&r, 2, 20);
	seg_ring_fold(&r, 1, 10);
	CHECK(r.lowest == 1u && r.lowest_bytes == 10u);
	seg_ring_init(&r);
	seg_ring_fold(&r, 1, 10);
	seg_ring_fold(&r, 2, 20);
	CHECK(r.lowest == 1u && r.lowest_bytes == 10u);
}

static seg_decim_t l_decim;

static void test_decimation(void)
{
	seg_decim_init(&l_decim);

	CHECK(seg_decim_keep(&l_decim, true, 0x200, 1000, 1000)); /* the first frame of an ID is kept */
	CHECK(!seg_decim_keep(&l_decim, true, 0x200, 1050, 1000));
	CHECK(!seg_decim_keep(&l_decim, true, 0x200, 1999, 1000)); /* 999 ms */
	CHECK(seg_decim_keep(&l_decim, true, 0x200, 2000, 1000));	/* 1000 ms: kept, and the interval restarts from here */
	CHECK(!seg_decim_keep(&l_decim, true, 0x200, 2999, 1000));
	CHECK(seg_decim_keep(&l_decim, true, 0x200, 3000, 1000));

	/* Each identifier has its own clock. */
	CHECK(seg_decim_keep(&l_decim, true, 0x201, 3001, 1000));
	CHECK(!seg_decim_keep(&l_decim, true, 0x201, 3500, 1000));
	CHECK(seg_decim_keep(&l_decim, true, 0x7FF, 3001, 1000)); /* the top identifier */
	CHECK(seg_decim_keep(&l_decim, true, 0x000, 3001, 1000)); /* the bottom one */
	CHECK(seg_decim_keep(&l_decim, true, 0x400, 3001, 1000)); /* differs from 0x000 only above bit 9: must not share its clock */
	CHECK(seg_decim_keep(&l_decim, true, 0x3FF, 3001, 1000));
	CHECK(seg_decim_keep(&l_decim, true, 0x7FE, 3001, 1000));
	CHECK(!seg_decim_keep(&l_decim, true, 0x400, 3500, 1000)); /* and it has a clock of its own */

	/* A frame this Node sent and the same ID received are separate streams (a loopback pair must both survive). */
	CHECK(seg_decim_keep(&l_decim, false, 0x200, 3002, 1000));
	CHECK(!seg_decim_keep(&l_decim, false, 0x200, 3500, 1000));
	CHECK(!seg_decim_keep(&l_decim, true, 0x200, 3500, 1000));

	/* An interval of 0 keeps every frame. */
	CHECK(seg_decim_keep(&l_decim, true, 0x300, 10, 0));
	CHECK(seg_decim_keep(&l_decim, true, 0x300, 10, 0));

	/* The millisecond counter wraps: only ONE side of a naive "now >= last + interval" comparison wraps here. */
	seg_decim_init(&l_decim);
	CHECK(seg_decim_keep(&l_decim, true, 0x100, 0xFFFFFE00u, 1000));
	CHECK(!seg_decim_keep(&l_decim, true, 0x100, 0xFFFFFFFFu, 1000)); /* 511 ms */
	CHECK(!seg_decim_keep(&l_decim, true, 0x100, 0x000001E7u, 1000)); /* 512 + 487 = 999 ms, the counter has wrapped */
	CHECK(seg_decim_keep(&l_decim, true, 0x100, 0x000001E8u, 1000));	/* 1000 ms */

	/* A time of 0 is a real time, not "never seen". */
	seg_decim_init(&l_decim);
	CHECK(seg_decim_keep(&l_decim, true, 0x100, 0, 1000));
	CHECK(!seg_decim_keep(&l_decim, true, 0x100, 500, 1000));
}

static void test_button(void)
{
	seg_button_t b;

	/* debounce 300 ms, stale after 300 ms (3 missed 100 ms frames). */
	seg_button_init(&b, 300, 300);

	/* Released, then pressed: one toggle on the press. */
	CHECK(!seg_button_frame(&b, false, 1000));
	CHECK(seg_button_frame(&b, true, 1100));	/* the rising edge */
	CHECK(!seg_button_frame(&b, true, 1200)); /* held: no second toggle */
	CHECK(!seg_button_frame(&b, true, 1300));
	CHECK(!seg_button_frame(&b, false, 1400)); /* the release does nothing */

	/* A second press must be at least debounce_ms after the previous toggle (the toggle was at 1100; frames are in time order). */
	CHECK(!seg_button_frame(&b, false, 1350));
	CHECK(!seg_button_frame(&b, true, 1399));	/* 299 ms after the toggle: a bounce, ignored */
	CHECK(!seg_button_frame(&b, false, 1450));
	CHECK(seg_button_frame(&b, true, 1500));	/* 400 ms after the toggle at 1100: accepted */
	CHECK(!seg_button_frame(&b, false, 1600));
	CHECK(!seg_button_frame(&b, true, 1650));	/* 150 ms after the toggle at 1500: ignored */
	CHECK(!seg_button_frame(&b, false, 1700));
	CHECK(seg_button_frame(&b, true, 1800));	/* exactly 300 ms after 1500: accepted */

	/* A button already down at the first frame does not toggle; it must be released first. */
	seg_button_init(&b, 300, 300);
	CHECK(!seg_button_frame(&b, true, 5000));
	CHECK(!seg_button_frame(&b, true, 5100));
	CHECK(!seg_button_frame(&b, false, 5200));
	CHECK(seg_button_frame(&b, true, 5300)); /* now a real press */

	/* Frames that stop are forgotten; a button still down when they resume is a first frame again (no toggle). */
	seg_button_init(&b, 300, 300);
	CHECK(!seg_button_frame(&b, false, 1000));
	CHECK(seg_button_frame(&b, true, 1100));
	seg_button_tick(&b, 1399); /* 299 ms since the last frame: not stale yet */
	CHECK(!seg_button_frame(&b, true, 1399));
	seg_button_tick(&b, 1698); /* 299 ms since 1399 */
	CHECK(!seg_button_frame(&b, true, 1698)); /* still the same session: held */
	seg_button_tick(&b, 1998); /* exactly 300 ms since 1698: stale */
	CHECK(!seg_button_frame(&b, true, 5000)); /* back after a long silence, still pressed: a first frame, no toggle */
	CHECK(!seg_button_frame(&b, false, 5100));
	CHECK(seg_button_frame(&b, true, 5200)); /* a real press again */

	/* The other side of the wrap: the toggle is 256 ms before it, and the next press is 240 ms later, still BEFORE the wrap, where a naive
	 * "now >= last + debounce" (whose sum has wrapped) would wrongly accept it. */
	seg_button_init(&b, 300, 300);
	CHECK(!seg_button_frame(&b, false, 0xFFFFFE00u));
	CHECK(seg_button_frame(&b, true, 0xFFFFFF00u));
	CHECK(!seg_button_frame(&b, false, 0xFFFFFF80u));
	CHECK(!seg_button_frame(&b, true, 0xFFFFFFF0u)); /* 240 ms after the toggle: ignored */

	/* Staleness: exactly stale_ms without a frame forgets the button, so a press that follows a released frame is NOT a toggle (a first frame);
	 * one millisecond earlier it still is. */
	seg_button_init(&b, 300, 300);
	CHECK(!seg_button_frame(&b, false, 1000));
	seg_button_tick(&b, 1299); /* 299 ms: not stale */
	CHECK(seg_button_frame(&b, true, 1299)); /* a real press */
	seg_button_init(&b, 300, 300);
	CHECK(!seg_button_frame(&b, false, 1000));
	seg_button_tick(&b, 1300); /* exactly 300 ms: stale, forgotten */
	CHECK(!seg_button_frame(&b, true, 1301)); /* a first frame again: no toggle */
	CHECK(!seg_button_frame(&b, false, 1400));
	CHECK(seg_button_frame(&b, true, 1500)); /* and a real press after a release */

	/* The first press of a session is never held back by the debounce time (nothing has toggled yet). */
	seg_button_init(&b, 300, 300);
	CHECK(!seg_button_frame(&b, false, 0));
	CHECK(seg_button_frame(&b, true, 10)); /* 10 ms after init: toggled_once is false, so accepted */

	/* The counter wraps: only ONE side of "now >= last + debounce" wraps. The toggle is 256 ms before the wrap; 299 ms later is 0x2B. */
	seg_button_init(&b, 300, 300);
	CHECK(!seg_button_frame(&b, false, 0xFFFFFE00u));
	CHECK(seg_button_frame(&b, true, 0xFFFFFF00u)); /* toggle */
	CHECK(!seg_button_frame(&b, false, 0xFFFFFF80u));
	CHECK(!seg_button_frame(&b, true, 0x0000002Bu)); /* 256 + 43 = 299 ms: ignored, though the counter has wrapped */

	seg_button_init(&b, 300, 300);
	CHECK(!seg_button_frame(&b, false, 0xFFFFFE00u));
	CHECK(seg_button_frame(&b, true, 0xFFFFFF00u));
	CHECK(!seg_button_frame(&b, false, 0xFFFFFF80u));
	CHECK(seg_button_frame(&b, true, 0x0000002Cu)); /* 256 + 44 = 300 ms: accepted */
}

static void test_mode(void)
{
	seg_mode_ctl_t m;

	seg_mode_init(&m, 600000);
	CHECK(m.mode == SEG_MODE_DAILY);
	CHECK(!seg_mode_tick(&m, 5000)); /* daily never stops by itself */

	seg_mode_toggle(&m, 1000);
	CHECK(m.mode == SEG_MODE_MANUAL && m.manual_start_ms == 1000u);
	CHECK(!seg_mode_tick(&m, 1000 + 599999)); /* 599999 ms */
	CHECK(m.mode == SEG_MODE_MANUAL);
	CHECK(seg_mode_tick(&m, 1000 + 600000)); /* exactly the maximum: ends once */
	CHECK(m.mode == SEG_MODE_DAILY);
	CHECK(!seg_mode_tick(&m, 1000 + 600001)); /* and says so only once */

	/* A second press ends it by hand, and the maximum is counted from each start. */
	seg_mode_toggle(&m, 700000);
	CHECK(m.mode == SEG_MODE_MANUAL);
	seg_mode_toggle(&m, 710000);
	CHECK(m.mode == SEG_MODE_DAILY);
	CHECK(!seg_mode_tick(&m, 700000 + 600000)); /* it was stopped already: nothing to end */
	seg_mode_toggle(&m, 800000);
	CHECK(!seg_mode_tick(&m, 800000 + 599999));
	CHECK(seg_mode_tick(&m, 800000 + 600000));

	/* No maximum: never stops by itself. */
	seg_mode_init(&m, 0);
	seg_mode_toggle(&m, 10);
	CHECK(!seg_mode_tick(&m, 0xFFFFFFFFu));
	CHECK(m.mode == SEG_MODE_MANUAL);

	/* The counter wraps: only ONE side of "now >= start + max" wraps. */
	seg_mode_init(&m, 1000);
	seg_mode_toggle(&m, 0xFFFFFE00u);
	CHECK(!seg_mode_tick(&m, 0xFFFFFFFFu)); /* 511 ms */
	CHECK(!seg_mode_tick(&m, 0x000001E7u)); /* 999 ms */
	CHECK(seg_mode_tick(&m, 0x000001E8u));	/* 1000 ms */
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
	test_names();
	test_ring();
	test_decimation();
	test_button();
	test_mode();

	printf("%d checks, %d failures\n", l_checks, l_failures);
	return l_failures == 0 ? 0 : 1;
}
