/*******************************************************
 * @file test_logger_xfer.c
 * @brief Host tests for the USB log copy protocol (parser, replies, CRC-32). Expected values are worked out by hand or taken from zlib.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>
#include <string.h>

#include "logger_xfer.h"

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
/* Feeds a whole string and returns the number of requests it produced (the last one in *out). */
static int feed(xfer_line_t *l, const char *s, xfer_request_t *out)
{
	int n = 0;

	for (; *s != '\0'; s++)
	{
		if (xfer_line_push(l, (uint8_t)*s, out))
		{
			n++;
		}
	}
	return n;
}

static void test_crc32(void)
{
	const uint8_t nine[] = "123456789";
	const char *fox = "The quick brown fox jumps over the lazy dog";
	uint32_t c;

	/* Values from Python's zlib.crc32. */
	CHECK(xfer_crc32_update(0, (const uint8_t *)"", 0) == 0x00000000u);
	CHECK(xfer_crc32_update(0, (const uint8_t *)"a", 1) == 0xE8B7BE43u);
	CHECK(xfer_crc32_update(0, nine, 9) == 0xCBF43926u);
	CHECK(xfer_crc32_update(0, (const uint8_t *)fox, strlen(fox)) == 0x414FA339u);

	/* In pieces: the same as in one go, however it is cut. */
	c = xfer_crc32_update(0, nine, 5);
	c = xfer_crc32_update(c, nine + 5, 4);
	CHECK(c == 0xCBF43926u);
	c = 0;
	for (size_t i = 0; i < 9; i++)
	{
		c = xfer_crc32_update(c, nine + i, 1);
	}
	CHECK(c == 0xCBF43926u);

	/* One flipped bit changes it. */
	{
		uint8_t bad[9];

		memcpy(bad, nine, 9);
		bad[4] ^= 0x01u;
		CHECK(xfer_crc32_update(0, bad, 9) != 0xCBF43926u);
	}
}

static void test_parse(void)
{
	xfer_request_t r;

	r = xfer_parse("LIST");
	CHECK(r.cmd == XFER_CMD_LIST);
	r = xfer_parse("GET LOG0005.CSV");
	CHECK(r.cmd == XFER_CMD_GET && r.number == 5u);
	r = xfer_parse("GET LOG9999.CSV");
	CHECK(r.cmd == XFER_CMD_GET && r.number == 9999u);
	r = xfer_parse("GET LOG0001.CSV");
	CHECK(r.cmd == XFER_CMD_GET && r.number == 1u);
	r = xfer_parse("get log0042.csv"); /* "GET" must be upper case, the name may be any case */
	CHECK(r.cmd == XFER_CMD_BAD);
	r = xfer_parse("GET log0042.csv");
	CHECK(r.cmd == XFER_CMD_GET && r.number == 42u);

	/* Everything else is refused, and above all nothing that looks like a path. */
	CHECK(xfer_parse("").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("list").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("LIST ").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("LISTX").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET LOG0000.CSV").cmd == XFER_CMD_BAD);	/* number 0 */
	CHECK(xfer_parse("GET LOG005.CSV").cmd == XFER_CMD_BAD);	/* 3 digits */
	CHECK(xfer_parse("GET LOG00005.CSV").cmd == XFER_CMD_BAD);	/* 5 digits */
	CHECK(xfer_parse("GET LOG00A5.CSV").cmd == XFER_CMD_BAD);	/* not a digit */
	CHECK(xfer_parse("GET LOG0005.TXT").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET LOG0005CSV").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET ../LOG005.CSV").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET /SD:/LOG005.CSV").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET FOO0005.CSV").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET  LOG005.CSV").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET LOG0005.CSVX").cmd == XFER_CMD_BAD);	/* anything after the name: refused, not ignored */
	CHECK(xfer_parse("GET LOG0005.CSV ").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET LOG0005.CSV;").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET LOG0005.CSV\\x").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("GET LOG0005.CS").cmd == XFER_CMD_BAD);	/* one character short */
	CHECK(xfer_parse("XGET LOG0005.CSV").cmd == XFER_CMD_BAD);
	CHECK(xfer_parse("DEL LOG0005.CSV").cmd == XFER_CMD_BAD); /* there is no delete */
	CHECK(xfer_parse("PUT LOG0005.CSV").cmd == XFER_CMD_BAD);
}

static void test_line_assembly(void)
{
	xfer_line_t l;
	xfer_request_t r;

	xfer_line_init(&l);
	CHECK(feed(&l, "LIST\n", &r) == 1 && r.cmd == XFER_CMD_LIST);
	CHECK(feed(&l, "LIST\r\n", &r) == 1 && r.cmd == XFER_CMD_LIST); /* CR LF is one request, not two */
	CHECK(feed(&l, "\n\n\r\n", &r) == 0);							/* empty lines are ignored */
	CHECK(feed(&l, "GET LOG0007.CSV\r", &r) == 1 && r.cmd == XFER_CMD_GET && r.number == 7u);
	CHECK(feed(&l, "LIST", &r) == 0);								/* not complete until a line ending */
	CHECK(feed(&l, "\n", &r) == 1 && r.cmd == XFER_CMD_LIST);
	CHECK(feed(&l, "LIST\nLIST\n", &r) == 2);						/* two commands in one burst */

	/* Exactly XFER_LINE_MAX characters fit; one more is an overflow, thrown away whole and reported once as BAD. */
	{
		char max_line[XFER_LINE_MAX + 2u];
		char long_line[XFER_LINE_MAX + 3u];

		memset(max_line, 'X', XFER_LINE_MAX);
		max_line[XFER_LINE_MAX] = '\n';
		max_line[XFER_LINE_MAX + 1u] = '\0';
		CHECK(feed(&l, max_line, &r) == 1 && r.cmd == XFER_CMD_BAD); /* fits, and is not a command */

		memset(long_line, 'X', XFER_LINE_MAX + 1u);
		long_line[XFER_LINE_MAX + 1u] = '\n';
		long_line[XFER_LINE_MAX + 2u] = '\0';
		CHECK(feed(&l, long_line, &r) == 1 && r.cmd == XFER_CMD_BAD);
		/* The overflow must not leak into the next line. */
		CHECK(feed(&l, "LIST\n", &r) == 1 && r.cmd == XFER_CMD_LIST);
	}

	/* A long line that happens to END in a valid command is still rejected as a whole. */
	{
		char s[XFER_LINE_MAX + 20u];

		memset(s, 'A', XFER_LINE_MAX + 4u);
		strcpy(&s[XFER_LINE_MAX + 4u], "LIST\n");
		CHECK(feed(&l, s, &r) == 1 && r.cmd == XFER_CMD_BAD);
	}
}

static void test_replies(void)
{
	char b[48];

	CHECK(xfer_format_list_entry(b, sizeof(b), 5, 1234567u) == 20u && strcmp(b, "LOG0005.CSV 1234567\n") == 0);
	CHECK(xfer_format_list_entry(b, sizeof(b), 9999, 0) == 14u && strcmp(b, "LOG9999.CSV 0\n") == 0);
	CHECK(xfer_format_list_entry(b, sizeof(b), 12, 4294967295u) == 23u && strcmp(b, "LOG0012.CSV 4294967295\n") == 0);
	CHECK(xfer_format_end(b, sizeof(b)) == 4u && strcmp(b, "END\n") == 0);
	CHECK(xfer_format_ok(b, sizeof(b), 1234567u) == 11u && strcmp(b, "OK 1234567\n") == 0);
	CHECK(xfer_format_crc(b, sizeof(b), 0x1A2B3C4Du) == 13u && strcmp(b, "CRC 1a2b3c4d\n") == 0);
	CHECK(xfer_format_crc(b, sizeof(b), 0x0000000Au) == 13u && strcmp(b, "CRC 0000000a\n") == 0); /* always 8 digits */
	CHECK(xfer_format_err(b, sizeof(b), "notfound") == 13u && strcmp(b, "ERR notfound\n") == 0);

	/* A buffer too small gives 0 and an empty string, never a cut-off reply. */
	CHECK(xfer_format_ok(b, 10, 1234567u) == 0u && b[0] == '\0'); /* "OK 1234567\n" needs 12 */
	CHECK(xfer_format_ok(b, 12, 1234567u) == 11u);				  /* exactly fits with the NUL */
	CHECK(xfer_format_end(b, 4) == 0u && b[0] == '\0');
	CHECK(xfer_format_end(b, 5) == 4u);
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
	test_crc32();
	test_parse();
	test_line_assembly();
	test_replies();

	printf("%d checks, %d failures\n", l_checks, l_failures);
	return l_failures == 0 ? 0 : 1;
}
