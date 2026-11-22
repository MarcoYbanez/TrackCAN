/*******************************************************
 * @file test_dash_shared.c
 * @brief Host tests for the cross-core snapshot exchange and link supervision.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>
#include <string.h>

#include "dash_shared.h"

/*******************************************************
 * Macros
 *******************************************************/
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        l_checks++;                                                                                \
        if (!(cond)) {                                                                             \
            l_failures++;                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
        }                                                                                          \
    } while (0)

/*******************************************************
 * Static Variables
 *******************************************************/
static int l_checks;
static int l_failures;
static int l_hook_enabled; /* simulates the writer completing during a read */

/* Called by dash_shared_read() between its copy and its sequence re-check (test builds only). */
void dash_test_midpoint(const dash_shared_t *sh)
{
    if (l_hook_enabled) {
        ((dash_shared_t *)sh)->seq += 2u;
    }
}

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static dash_snapshot_t make_snap(uint16_t rpm, uint8_t ethanol)
{
    dash_snapshot_t s;

    memset(&s, 0, sizeof(s));
    s.rpm = rpm;
    s.speed_kmh_x100 = 12345;
    s.display.ethanol_pct = ethanol;
    s.display.ethanol_alert = false;
    return s;
}

static void test_write_read(void)
{
    dash_shared_t sh;
    dash_snapshot_t in = make_snap(3500, 85);
    dash_snapshot_t out;
    uint32_t hb = 99;

    /* Memory that nobody initialised is never trusted. */
    memset(&sh, 0xA5, sizeof(sh));
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_BAD_MAGIC);

    /* Initialised but never written: still not valid, so zeros are never shown as live values. */
    dash_shared_init(&sh);
    CHECK(sh.magic == 0 && sh.seq == 0 && sh.heartbeat == 0);
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_BAD_MAGIC);

    dash_shared_write(&sh, &in);
    CHECK(sh.magic == DASH_SHARED_MAGIC && sh.layout == DASH_LAYOUT_TAG);
    CHECK(sh.seq == 2 && sh.heartbeat == 1);
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_OK);
    CHECK(hb == 1 && out.rpm == 3500 && out.speed_kmh_x100 == 12345);
    CHECK(out.display.ethanol_pct == 85);

    /* Each write advances the heartbeat by exactly one. */
    in = make_snap(4000, 60);
    dash_shared_write(&sh, &in);
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_OK && hb == 2 && out.rpm == 4000);
}

static void test_torn_read_is_refused(void)
{
    dash_shared_t sh;
    dash_snapshot_t out;
    uint32_t hb = 0;

    dash_shared_init(&sh);
    dash_shared_write(&sh, &(dash_snapshot_t){.rpm = 1000});

    /* The writer is mid-update (odd sequence): the reader must not return data, and must leave the
     * caller's copy alone. */
    memset(&out, 0, sizeof(out));
    out.rpm = 0xBEEF;
    hb = 0xABCD;
    sh.seq = sh.seq + 1u;
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_BUSY);
    CHECK(out.rpm == 0xBEEF && hb == 0xABCD);

    /* The writer finishes: reads work again. */
    sh.seq = sh.seq + 1u;
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_OK && out.rpm == 1000);

    /* The writer completes in the MIDDLE of the copy (sequence changes during the read): the
     * possibly torn copy is thrown away and `out` is again untouched. */
    out.rpm = 0xBEEF;
    hb = 0xABCD;
    l_hook_enabled = 1;
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_BUSY);
    l_hook_enabled = 0;
    CHECK(out.rpm == 0xBEEF && hb == 0xABCD);
}

static void test_layout_mismatch(void)
{
    dash_shared_t sh;
    dash_snapshot_t out;
    uint32_t hb = 0;

    /* Hand-computed: version 1 in the high half, the snapshot size in the low half. */
    CHECK(DASH_LAYOUT_TAG ==
          ((4u << 16) | (uint32_t)sizeof(dash_snapshot_t))); /* version 4: flags and engine-valid */

    dash_shared_init(&sh);
    dash_shared_write(&sh, &(dash_snapshot_t){.rpm = 1234});
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_OK);

    /* Only one core was reflashed after the snapshot layout changed: refuse, never misread. */
    out.rpm = 0xBEEF;
    sh.layout = DASH_LAYOUT_TAG + 8u;
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_LAYOUT_MISMATCH);
    CHECK(out.rpm == 0xBEEF);
    sh.layout = ((DASH_LAYOUT_VERSION + 1u) << 16) | (uint32_t)sizeof(dash_snapshot_t);
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_LAYOUT_MISMATCH);
}

static void test_link_up_and_lost(void)
{
    dash_link_t l;

    /* Start-up: waiting inside the grace period, lost just after it. */
    dash_link_init(&l, 1000);
    CHECK(dash_link_update(&l, false, 0, 1000) == DASH_LINK_WAITING);
    CHECK(dash_link_update(&l, false, 0, 3000) == DASH_LINK_WAITING);
    CHECK(dash_link_update(&l, false, 0, 3001) == DASH_LINK_LOST);

    /* The M4 appears late. The first read is only a sighting; the link comes up when the heartbeat
     * moves. */
    CHECK(dash_link_update(&l, true, 1, 3010) == DASH_LINK_LOST); /* still past the grace period */
    CHECK(dash_link_update(&l, true, 2, 3020) == DASH_LINK_UP);

    /* A moving heartbeat keeps it up. */
    CHECK(dash_link_update(&l, true, 3, 3270) == DASH_LINK_UP);

    /* A frozen heartbeat (the M4 hung) is lost after 250 ms, even though the data reads fine. */
    CHECK(dash_link_update(&l, true, 3, 3520) == DASH_LINK_UP);   /* 250 ms since the change */
    CHECK(dash_link_update(&l, true, 3, 3521) == DASH_LINK_LOST); /* 251 ms */

    /* When the M4 comes back, the link recovers. */
    CHECK(dash_link_update(&l, true, 4, 3530) == DASH_LINK_UP);
}

static void test_link_with_read_failures(void)
{
    dash_link_t l;

    dash_link_init(&l, 0);
    CHECK(dash_link_update(&l, true, 10, 100) == DASH_LINK_WAITING); /* a sighting, not yet proof */
    CHECK(dash_link_update(&l, true, 11, 120) == DASH_LINK_UP);

    /* Reads that keep failing (the writer is stuck mid-update) also end in a lost link. */
    CHECK(dash_link_update(&l, false, 0, 370) ==
          DASH_LINK_UP); /* 250 ms since the heartbeat moved */
    CHECK(dash_link_update(&l, false, 0, 371) == DASH_LINK_LOST); /* 251 ms */
}

static void test_leftover_data_is_never_live(void)
{
    dash_link_t l;

    /* A reset leaves the previous run's snapshot in memory: it reads fine, but its heartbeat never
     * moves. */
    dash_link_init(&l, 0);
    CHECK(dash_link_update(&l, true, 5000, 6) == DASH_LINK_WAITING);
    CHECK(dash_link_update(&l, true, 5000, 500) == DASH_LINK_WAITING);
    CHECK(dash_link_update(&l, true, 5000, 2000) ==
          DASH_LINK_WAITING); /* the grace period is not over */
    CHECK(dash_link_update(&l, true, 5000, 2001) ==
          DASH_LINK_LOST); /* it ends as the alert, never as data */

    /* If the real M4 then starts writing, the link recovers. */
    CHECK(dash_link_update(&l, true, 1, 2020) == DASH_LINK_UP);
}

static void test_wraps(void)
{
    dash_link_t l;
    dash_shared_t sh;
    dash_snapshot_t out;
    uint32_t hb = 0;

    /* The millisecond counter wraps. */
    dash_link_init(&l, 0xFFFFFF00u);
    CHECK(dash_link_update(&l, true, 1, 0xFFFFFF10u) == DASH_LINK_WAITING);
    CHECK(dash_link_update(&l, true, 2, 0xFFFFFF20u) == DASH_LINK_UP);
    CHECK(dash_link_update(&l, true, 3, 0x00000050u) == DASH_LINK_UP); /* 0x130 ms later */
    CHECK(dash_link_update(&l, true, 3, 0x00000200u) ==
          DASH_LINK_LOST); /* 0x1B0 ms with no change */

    /* The heartbeat wraps from 0xFFFFFFFF to 0: still counts as a change. */
    dash_link_init(&l, 0);
    CHECK(dash_link_update(&l, true, 0xFFFFFFFFu, 10) == DASH_LINK_WAITING);
    CHECK(dash_link_update(&l, true, 0x00000000u, 20) == DASH_LINK_UP);
    CHECK(l.last_heartbeat == 0 && l.last_change_ms == 20);

    /* The sequence counter wraps: an even value stays readable. */
    dash_shared_init(&sh);
    sh.seq = 0xFFFFFFFEu;
    dash_shared_write(&sh, &(dash_snapshot_t){.rpm = 777});
    CHECK(sh.seq == 0u);
    CHECK(dash_shared_read(&sh, &out, &hb) == DASH_READ_OK && out.rpm == 777);
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    test_write_read();
    test_torn_read_is_refused();
    test_layout_mismatch();
    test_link_up_and_lost();
    test_link_with_read_failures();
    test_leftover_data_is_never_live();
    test_wraps();

    printf("%d checks, %d failures\n", l_checks, l_failures);
    return l_failures == 0 ? 0 : 1;
}
