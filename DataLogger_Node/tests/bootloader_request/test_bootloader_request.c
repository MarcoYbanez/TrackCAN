/*******************************************************
 * @file test_bootloader_request.c
 * @brief Host tests for the PJRC reboot request recognizer. Expected values are worked out by hand.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>

#include "bootloader_request.h"

/*******************************************************
 * Macros
 *******************************************************/
#define CHECK(cond)                                                                              \
    do {                                                                                         \
        l_checks++;                                                                              \
        if (!(cond)) {                                                                           \
            l_failures++;                                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                              \
        }                                                                                        \
    } while (0)

/*******************************************************
 * Static Variables
 *******************************************************/
static int l_checks;
static int l_failures;

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void test_constants_match_pjrc(void)
{
    /* cores/teensy4/usb.c:843 "if (usb_cdc_line_coding[0] == 134)". */
    CHECK(BOOTLOADER_REQUEST_BAUD == 134u);
    CHECK(BOOTLOADER_REQUEST_DELAY_MS >= 10u); /* at least PJRC's 80 start-of-frames (about 10 ms) */
}

static void test_ordinary_baud_rates_never_trigger(void)
{
    static const uint32_t bauds[] = {0, 1, 133, 135, 1340, 13400, 9600, 115200, 921600, 4294967295u};
    bootloader_request_t r;

    for (unsigned i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
        bootloader_request_init(&r);
        for (uint32_t t = 0; t < 5000; t += 10) {
            CHECK(!bootloader_request_poll(&r, bauds[i], t));
        }
    }
}

static void test_134_fires_once_after_the_delay(void)
{
    bootloader_request_t r;

    bootloader_request_init(&r);
    CHECK(!bootloader_request_poll(&r, 115200, 1000)); /* normal operation */
    CHECK(!bootloader_request_poll(&r, 134, 2000));    /* the request is seen: the delay starts */
    CHECK(!bootloader_request_poll(&r, 134, 2010));
    CHECK(!bootloader_request_poll(&r, 134, 2049));    /* 49 ms */
    CHECK(bootloader_request_poll(&r, 134, 2050));     /* 50 ms: now */
    CHECK(!bootloader_request_poll(&r, 134, 2051));    /* never twice */
    CHECK(!bootloader_request_poll(&r, 134, 9000));
    CHECK(!bootloader_request_poll(&r, 115200, 9100));
}

static void test_the_request_is_not_cancelled_like_pjrc(void)
{
    bootloader_request_t r;

    /* A host that sets 134 baud and closes the port at once: the rate may be reset before the delay is over. PJRC's firmware still
     * reboots (its countdown is never cancelled), and so must this. */
    bootloader_request_init(&r);
    CHECK(!bootloader_request_poll(&r, 134, 100));
    CHECK(!bootloader_request_poll(&r, 115200, 110));
    CHECK(!bootloader_request_poll(&r, 9600, 140));
    CHECK(bootloader_request_poll(&r, 115200, 150));
}

static void test_the_delay_counts_from_the_first_sighting(void)
{
    bootloader_request_t r;

    bootloader_request_init(&r);
    CHECK(!bootloader_request_poll(&r, 134, 500));
    CHECK(!bootloader_request_poll(&r, 134, 520)); /* seeing 134 again does not restart the delay */
    CHECK(!bootloader_request_poll(&r, 134, 540));
    CHECK(bootloader_request_poll(&r, 134, 550));  /* 50 ms after 500, not after 540 */
}

static void test_time_wrap(void)
{
    bootloader_request_t r;

    /* Only one side of a naive "now >= seen + delay" wraps here: the sighting is 0x20 ms before the counter wraps. */
    bootloader_request_init(&r);
    CHECK(!bootloader_request_poll(&r, 134, 0xFFFFFFE0u));
    CHECK(!bootloader_request_poll(&r, 134, 0xFFFFFFFFu)); /* 31 ms */
    CHECK(!bootloader_request_poll(&r, 134, 0x00000010u)); /* 48 ms, the counter has wrapped */
    CHECK(bootloader_request_poll(&r, 134, 0x00000012u));  /* 50 ms */
}

static void test_each_recognizer_is_independent(void)
{
    bootloader_request_t a;
    bootloader_request_t b;

    bootloader_request_init(&a);
    bootloader_request_init(&b);
    CHECK(!bootloader_request_poll(&a, 134, 0));
    CHECK(bootloader_request_poll(&a, 134, 60));
    CHECK(!bootloader_request_poll(&b, 134, 61)); /* b has only just seen it */
    CHECK(bootloader_request_poll(&b, 134, 111));
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    test_constants_match_pjrc();
    test_ordinary_baud_rates_never_trigger();
    test_134_fires_once_after_the_delay();
    test_the_request_is_not_cancelled_like_pjrc();
    test_the_delay_counts_from_the_first_sighting();
    test_time_wrap();
    test_each_recognizer_is_independent();

    printf("%d checks, %d failures\n", l_checks, l_failures);
    return l_failures == 0 ? 0 : 1;
}
