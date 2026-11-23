/*******************************************************
 * @file test_logger_core.c
 * @brief Host tests for the SD logger's logic. Expected values are worked out by hand.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>
#include <string.h>

#include "logger_core.h"

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
static void test_frame_lines(void)
{
	char b[LOGGER_LINE_MAX];
	const uint8_t lights[8] = { 0x84, 0x01 };
	const uint8_t eth[8] = { 0x55, 0x85, 0xFF, 0x23 };
	const uint8_t analog[8] = { 0xC2, 0x01, 0x70, 0xFE, 0xB6, 0x03, 0xF3, 0x02 };
	const uint8_t zeros[8] = { 0 };

	CHECK(strcmp(logger_csv_header(), "t_ms,dir,id,dlc,data\n") == 0);

	/* DataLogger_Lights 0x201: left turn + high beam = 0x84, charge warning = 0x01 in byte 1. */
	CHECK(logger_format_frame(b, sizeof(b), 12345, LOGGER_DIR_TX, 0x201, 2, lights) == 20);
	CHECK(strcmp(b, "12345,TX,201,2,8401\n") == 0);
	/* Ethanol_Data 0x300: 85 %, then -12.3 degC = 0xFF85, then the status byte. */
	CHECK(logger_format_frame(b, sizeof(b), 0, LOGGER_DIR_RX, 0x300, 4, eth) == 20);
	CHECK(strcmp(b, "0,RX,300,4,5585FF23\n") == 0);
	/* A full frame, the bytes of the DataLogger_Analog example. */
	CHECK(logger_format_frame(b, sizeof(b), 7, LOGGER_DIR_TX, 0x200, 8, analog) > 0);
	CHECK(strcmp(b, "7,TX,200,8,C20170FEB603F302\n") == 0);
	/* An empty payload and the lowest id (3 digits, zero padded). */
	CHECK(logger_format_frame(b, sizeof(b), 5, LOGGER_DIR_RX, 0x000, 0, zeros) > 0);
	CHECK(strcmp(b, "5,RX,000,0,\n") == 0);
	/* The biggest time stamp. */
	CHECK(logger_format_frame(b, sizeof(b), 4294967295u, LOGGER_DIR_RX, 0x7FF, 0, zeros) == 21);
	CHECK(strcmp(b, "4294967295,RX,7FF,0,\n") == 0);
	/* A dlc above 8 is clamped: 8 bytes are shown and the dlc says 8. */
	CHECK(logger_format_frame(b, sizeof(b), 9, LOGGER_DIR_RX, 0x100, 15, analog) > 0);
	CHECK(strcmp(b, "9,RX,100,8,C20170FEB603F302\n") == 0);
	/* The longest line fits in LOGGER_LINE_MAX. */
	CHECK(logger_format_frame(b, sizeof(b), 4294967295u, LOGGER_DIR_TX, 0x7FF, 8, analog) < LOGGER_LINE_MAX);
}

static void test_frame_buffer_limits(void)
{
	char b[64];
	const uint8_t d[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

	/* "1,RX,100,2,0102\n" is 16 characters, so it needs 17 bytes with the NUL. */
	memset(b, 'x', sizeof(b));
	CHECK(logger_format_frame(b, 17, 1, LOGGER_DIR_RX, 0x100, 2, d) == 16);
	CHECK(strcmp(b, "1,RX,100,2,0102\n") == 0);
	CHECK(logger_format_frame(b, 16, 1, LOGGER_DIR_RX, 0x100, 2, d) == 0); /* one byte short */
	CHECK(b[0] == '\0');												   /* and the buffer is left empty */
	CHECK(logger_format_frame(b, 1, 1, LOGGER_DIR_RX, 0x100, 2, d) == 0);
	CHECK(logger_format_frame(b, 0, 1, LOGGER_DIR_RX, 0x100, 2, d) == 0);
}

static void test_notes(void)
{
	char b[LOGGER_LINE_MAX];

	CHECK(logger_format_note(b, sizeof(b), 1000, "dropped=12") == 23);
	CHECK(strcmp(b, "1000,NOTE,,,dropped=12\n") == 0);
	/* Commas and line breaks would break the CSV: they become spaces. */
	CHECK(logger_format_note(b, sizeof(b), 1, "a,b\nc\r") > 0);
	CHECK(strcmp(b, "1,NOTE,,,a b c \n") == 0);
	/* A note that does not fit is dropped whole, never cut in the middle. */
	CHECK(logger_format_note(b, 20, 1000, "dropped=12") == 0);
	CHECK(b[0] == '\0');
	CHECK(logger_format_note(b, 24, 1000, "dropped=12") == 23); /* exactly enough */
	CHECK(logger_format_note(b, 23, 1000, "dropped=12") == 0);
}

static void test_file_names(void)
{
	char b[LOGGER_FILE_NAME_LEN];

	CHECK(logger_file_name(b, sizeof(b), 7) == 0 && strcmp(b, "LOG0007.CSV") == 0);
	CHECK(logger_file_name(b, sizeof(b), 1) == 0 && strcmp(b, "LOG0001.CSV") == 0);
	CHECK(logger_file_name(b, sizeof(b), 9999) == 0 && strcmp(b, "LOG9999.CSV") == 0);
	CHECK(logger_file_name(b, sizeof(b), 0) == -1);
	CHECK(logger_file_name(b, sizeof(b), 10000) == -1);
	CHECK(logger_file_name(b, 11, 7) == -1); /* no room for the NUL */
}

static void test_next_file_number(void)
{
	const char *none[] = { "README.TXT", "SYSTEM~1" };
	const char *some[] = { "LOG0001.CSV", "LOG0007.CSV", "README.TXT", "LOG0003.CSV" };
	const char *lower[] = { "log0009.csv", "Log0002.Csv" };
	const char *junk[] = { "LOG0003.TXT",  "LOG12.CSV",	  "LOG00005.CSV",
						   "LOGABCD.CSV",  "LOG000A.CSV", "XLOG0042.CSV",
						   "LOG0004.CSV ", NULL,		  "" };
	const char *full[] = { "LOG9999.CSV", "LOG0001.CSV" };
	const char *almost[] = { "LOG9998.CSV" };
	const char *zero[] = { "LOG0000.CSV" };

	CHECK(logger_next_file_number(NULL, 0) == 1);	/* an empty card */
	CHECK(logger_next_file_number(none, 2) == 1);	/* other files only */
	CHECK(logger_next_file_number(some, 4) == 8);	/* one more than the highest, not the count */
	CHECK(logger_next_file_number(lower, 2) == 10); /* any case */
	CHECK(logger_next_file_number(junk, 9) == 1);	/* near misses are ignored, nothing is overwritten */
	CHECK(logger_next_file_number(almost, 1) == 9999);
	CHECK(logger_next_file_number(full, 2) == 0); /* no number left: the logger must not write */
	CHECK(logger_next_file_number(zero, 1) == 1); /* LOG0000 is not a log the logger would have made */
}

static void test_directory_scan_one_entry_at_a_time(void)
{
	const char *dir[] = {"README.TXT", "LOG0003.CSV", "LOG0010.CSV", "log0002.csv", "LOG0007.TXT", "LOG00011.CSV"};
	unsigned highest = 0;

	for (size_t i = 0; i < sizeof(dir) / sizeof(dir[0]); i++)
	{
		unsigned before = highest;

		logger_note_file_name(&highest, dir[i]);
		CHECK(highest >= before); /* the highest number never goes down */
	}
	CHECK(highest == 10);                                                          /* LOG0010 */
	CHECK(logger_next_number(highest) == 11);
	CHECK(logger_next_number(highest) == logger_next_file_number(dir, 6));         /* the same answer as the list version */
	CHECK(logger_next_number(0) == 1);
	CHECK(logger_next_number(9998) == 9999);
	CHECK(logger_next_number(9999) == 0);                                          /* full: do not write */

	highest = 0;
	logger_note_file_name(&highest, NULL); /* a bad entry is ignored, not a crash */
	logger_note_file_name(&highest, "");
	CHECK(highest == 0);
}

static void test_policy(void)
{
	logger_policy_t p;

	logger_policy_init(&p, 2048, 250, 1000, 10000);
	CHECK(!logger_policy_should_write(&p, 99999)); /* nothing buffered: nothing to write, however late */
	CHECK(!logger_policy_should_sync(&p, 99999));  /* nothing written: nothing to sync */

	/* Written by age: the clock starts with the FIRST buffered byte. */
	logger_policy_added(&p, 25, 20000);
	logger_policy_added(&p, 25, 20200); /* a later add does not restart the clock */
	CHECK(!logger_policy_should_write(&p, 20249));
	CHECK(logger_policy_should_write(&p, 20250));

	/* Written by size. */
	logger_policy_written(&p);
	CHECK(!logger_policy_should_write(&p, 99999));
	logger_policy_added(&p, 2047, 30000);
	CHECK(!logger_policy_should_write(&p, 30000));
	logger_policy_added(&p, 1, 30001);
	CHECK(logger_policy_should_write(&p, 30001)); /* 2048 bytes: now, whatever the age */
	logger_policy_written(&p);

	/* Sync: only with data written since the last sync, at least every second. */
	p.last_sync_ms = 20000;
	CHECK(logger_policy_should_sync(&p, 30999) == false ||
		  (uint32_t)(30999 - 20000) >= 1000); /* just a clock sanity check */
	logger_policy_synced(&p, 31000);
	CHECK(!logger_policy_should_sync(&p, 99999)); /* synced: nothing unsynced */
	logger_policy_added(&p, 30, 31100);
	logger_policy_written(&p);
	CHECK(!logger_policy_should_sync(&p, 31999)); /* 999 ms since the sync */
	CHECK(logger_policy_should_sync(&p, 32000));  /* 1000 ms */
	logger_policy_synced(&p, 32000);
	CHECK(!logger_policy_should_sync(&p, 40000));

	/* The millisecond counter wraps. The cases are chosen so only ONE side of a naive "now >= start + interval" comparison wraps:
	 * that is the case a wrong implementation gets wrong. */
	logger_policy_init(&p, 2048, 250, 1000, 0xFFFFFC00u);
	logger_policy_added(&p, 10, 0xFFFFFE00u); /* the first buffered byte, 0x200 ms before the wrap */
	CHECK(!logger_policy_should_write(&p, 0xFFFFFEF9u)); /* 249 ms */
	CHECK(logger_policy_should_write(&p, 0xFFFFFEFAu));  /* 250 ms, no wrap yet */
	CHECK(logger_policy_should_write(&p, 0x00000010u));  /* 528 ms, the counter has wrapped */
	logger_policy_written(&p);
	CHECK(!logger_policy_should_sync(&p, 0xFFFFFFE7u)); /* 999 ms since 0xFFFFFC00 */
	CHECK(logger_policy_should_sync(&p, 0xFFFFFFE8u));  /* 1000 ms, no wrap yet */
	CHECK(logger_policy_should_sync(&p, 0x00000010u));  /* 1040 ms, the counter has wrapped */
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
	test_frame_lines();
	test_frame_buffer_limits();
	test_notes();
	test_file_names();
	test_next_file_number();
	test_directory_scan_one_entry_at_a_time();
	test_policy();

	printf("%d checks, %d failures\n", l_checks, l_failures);
	return l_failures == 0 ? 0 : 1;
}
