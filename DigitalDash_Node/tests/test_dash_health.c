/*******************************************************
 * @file test_dash_health.c
 * @brief Host tests for the alive supervision. Expected results are worked out by hand.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>

#include "dash_health.h"

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

/* Two tasks: entity 0 expected 2 to 4 times per period, entity 1 exactly 1 to 2 times. Two failed
 * periods stop it. */
static const dash_health_cfg_t k_cfg = {
    .count = 2, .min_per_period = {2, 1}, .max_per_period = {4, 2}, .fail_cycles = 2};

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void checkins(dash_health_t *h, uint8_t entity, unsigned n)
{
    while (n--) {
        dash_health_checkin(h, entity);
    }
}

static void test_healthy_runs_forever(void)
{
    dash_health_t h;

    dash_health_init(&h, &k_cfg);
    CHECK(dash_health_should_feed(&h)); /* starts fed: the first period is judged like any other */
    for (int period = 0; period < 1000; period++) {
        checkins(&h, 0, 3);
        checkins(&h, 1, 1);
        CHECK(dash_health_evaluate(&h));
        CHECK(dash_health_should_feed(&h));
    }
}

static void test_window_edges(void)
{
    dash_health_t h;

    dash_health_init(&h, &k_cfg);
    checkins(&h, 0, 2); /* the lowest allowed counts */
    checkins(&h, 1, 1);
    CHECK(dash_health_evaluate(&h));
    checkins(&h, 0, 4); /* the highest allowed counts */
    checkins(&h, 1, 2);
    CHECK(dash_health_evaluate(&h));

    dash_health_init(&h, &k_cfg);
    checkins(&h, 0, 1); /* one short */
    checkins(&h, 1, 1);
    CHECK(!dash_health_evaluate(&h));
    dash_health_init(&h, &k_cfg);
    checkins(&h, 0, 5); /* one too many: a runaway loop */
    checkins(&h, 1, 1);
    CHECK(!dash_health_evaluate(&h));
    dash_health_init(&h, &k_cfg);
    checkins(&h, 0, 3);
    checkins(&h, 1, 3); /* the second task running away fails the period too */
    CHECK(!dash_health_evaluate(&h));
}

static void test_stall_stops_the_feed_and_it_stays_stopped(void)
{
    dash_health_t h;

    dash_health_init(&h, &k_cfg);
    checkins(&h, 0, 3);
    checkins(&h, 1, 1);
    CHECK(dash_health_evaluate(&h));

    /* Task 1 stalls. One failed period is tolerated, the second stops the feed. */
    checkins(&h, 0, 3);
    CHECK(!dash_health_evaluate(&h));
    CHECK(dash_health_should_feed(&h));
    checkins(&h, 0, 3);
    CHECK(!dash_health_evaluate(&h));
    CHECK(!dash_health_should_feed(&h));

    /* Never recovers, even if the task comes back and every period is perfect (WdgM 7.3.3). */
    for (int i = 0; i < 20; i++) {
        checkins(&h, 0, 3);
        checkins(&h, 1, 1);
        CHECK(!dash_health_evaluate(&h));
        CHECK(!dash_health_should_feed(&h));
    }
}

static void test_one_bad_period_between_good_ones_is_tolerated(void)
{
    dash_health_t h;

    dash_health_init(&h, &k_cfg);
    for (int i = 0; i < 50; i++) {
        checkins(&h, 0, 3);
        checkins(&h, 1, 1);
        CHECK(dash_health_evaluate(&h));
        checkins(&h, 0, 3); /* a single hiccup */
        CHECK(!dash_health_evaluate(&h));
        CHECK(dash_health_should_feed(&h));
    }
}

static void test_counter_wrap_and_bad_entity(void)
{
    dash_health_t h;

    dash_health_init(&h, &k_cfg);
    h.checkins[0] = 0xFFFFFFFEu; /* the counters wrap: the difference is still right */
    h.last_seen[0] = 0xFFFFFFFEu;
    checkins(&h, 0, 3);
    checkins(&h, 1, 1);
    CHECK(dash_health_evaluate(&h));

    dash_health_checkin(&h, 2); /* an unknown entity is ignored, not a memory write */
    dash_health_checkin(&h, 3);
    dash_health_checkin(&h, 200);
    dash_health_checkin(&h, 255);
    CHECK(h.checkins[2] == 0 && h.checkins[3] == 0); /* nothing was counted for them */
    checkins(&h, 0, 3);
    checkins(&h, 1, 1);
    CHECK(dash_health_evaluate(&h));
}

static void test_bad_config_starts_stopped(void)
{
    dash_health_t h;
    dash_health_cfg_t c = k_cfg;

    dash_health_init(&h, NULL);
    CHECK(!dash_health_should_feed(&h));

    c.count = 0;
    dash_health_init(&h, &c);
    CHECK(!dash_health_should_feed(&h));

    c = k_cfg;
    c.count = DASH_HEALTH_MAX_ENTITIES + 1u;
    dash_health_init(&h, &c);
    CHECK(!dash_health_should_feed(&h));

    c = k_cfg;
    c.fail_cycles = 0; /* a monitor that can never fail would feed forever */
    dash_health_init(&h, &c);
    CHECK(!dash_health_should_feed(&h));

    c = k_cfg;
    c.min_per_period[1] = 3; /* min above max: no count can pass */
    dash_health_init(&h, &c);
    CHECK(!dash_health_should_feed(&h));
    CHECK(!dash_health_evaluate(&h));
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    test_healthy_runs_forever();
    test_window_edges();
    test_stall_stops_the_feed_and_it_stays_stopped();
    test_one_bad_period_between_good_ones_is_tolerated();
    test_counter_wrap_and_bad_entity();
    test_bad_config_starts_stopped();

    printf("%d checks, %d failures\n", l_checks, l_failures);
    return l_failures == 0 ? 0 : 1;
}
