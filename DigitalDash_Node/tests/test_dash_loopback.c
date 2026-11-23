/*******************************************************
 * @file test_dash_loopback.c
 * @brief Host test for the loopback path: simulated Nodes -> frames -> a loopback "controller" ->
 * the real Dash decoder -> display values. The controller is a ring buffer that, like the FDCAN
 * internal loopback, returns every frame sent.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>
#include <string.h>

#include "dash.h"
#include "dash_loopback_source.h"
#include "dash_sim.h"

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

#define RING 256u

/*******************************************************
 * Static Variables
 *******************************************************/
static int l_checks;
static int l_failures;

static tcan_frame_t l_ring[RING];
static unsigned l_head, l_tail;
static uint32_t l_now;
static unsigned l_sent;
static unsigned l_sent_this_step;
static unsigned l_max_burst;

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static int ring_send(const tcan_frame_t *f)
{
    if (l_head - l_tail >= RING) {
        return -1;
    }
    l_ring[l_head++ % RING] = *f;
    l_sent++;
    l_sent_this_step++;
    return 0;
}

static int ring_receive(tcan_frame_t *f)
{
    if (l_head == l_tail) {
        return 0;
    }
    *f = l_ring[l_tail++ % RING];
    return 1;
}

static uint32_t ring_now(void)
{
    return l_now;
}

static const tcan_port_t k_port = {ring_send, ring_receive, ring_now};

static int absdiff(int a, int b)
{
    return a > b ? a - b : b - a;
}

static void test_conversions(void)
{
    /* 1 psi = 0.0689476 bar: 145.0 psi is 10.0 bar = 1000 bar x100 (worked out by hand). */
    CHECK(dash_loopback_psi_x10_to_bar_x100(0) == 0);
    CHECK(dash_loopback_psi_x10_to_bar_x100(1450) == 1000);
    CHECK(dash_loopback_psi_x10_to_bar_x100(100) == 69); /* 10.0 psi = 0.689 bar */
    /* (F - 32) x 5 / 9: 212.0 F = 100.0 C, 32.0 F = 0 C, -40.0 F = -40.0 C, 220.0 F = 104.4 C. */
    CHECK(dash_loopback_degf_x10_to_degc_x10(2120) == 1000);
    CHECK(dash_loopback_degf_x10_to_degc_x10(320) == 0);
    CHECK(dash_loopback_degf_x10_to_degc_x10(-400) == -400);
    CHECK(dash_loopback_degf_x10_to_degc_x10(2200) == 1044);
}

static void test_demo_drive_survives_the_bus_round_trip(void)
{
    dash_loopback_source_t src;
    dash_sim_t sim;
    dash_sim_out_t drive;
    struct dash_display d;
    unsigned checked = 0;
    unsigned worst_psi = 0, worst_degf = 0;
    int32_t prev_coolant = 0;
    unsigned max_coolant_step = 0; /* the drive's largest coolant change in one 20 ms step, tenths of a degree F */

    l_head = l_tail = 0;
    l_now = 1000;
    l_sent = 0;
    dash_sim_init(&sim);
    CHECK(dash_can_init(&k_port));
    CHECK(dash_loopback_source_init(&src, &k_port));

    /* 20 minutes of the demo drive in 20 ms steps. */
    for (unsigned i = 0; i < 60000u; i++) {
        l_now += 20;
        l_sent_this_step = 0;
        dash_sim_step(&sim, 20, &drive);
        dash_loopback_source_step(&src, &drive);
        dash_can_step();
        if (i > 0 && (unsigned)absdiff(drive.coolant_degf_x10, prev_coolant) > max_coolant_step) {
            max_coolant_step = (unsigned)absdiff(drive.coolant_degf_x10, prev_coolant);
        }
        prev_coolant = drive.coolant_degf_x10;
        if (l_sent_this_step > l_max_burst) {
            l_max_burst = l_sent_this_step;
        }
        dash_can_get_display(&d);

        if (i < 20) {
            continue; /* the first cycle of every message has to arrive */
        }
        CHECK(d.analog_state == TCAN_RX_FRESH);
        CHECK(d.oil_pressure_state == TCAN_RX_FRESH);
        CHECK(d.lights_state == TCAN_RX_FRESH);
        CHECK(d.ethanol_state == TCAN_RX_FRESH);
        CHECK(!d.ethanol_alert);
        CHECK(d.ethanol_pct == 85);
        CHECK(d.ethanol_node_state == 2 && !d.ethanol_output_default);
        CHECK(d.fuel_temp_valid);

        /* The slow analog message (temperatures, fuel) is sent every 100 ms and the oil pressure every 20 ms, so a display value is
         * the drive's value from up to that long ago: compare with a tolerance for the drive's own motion, and exactly for the
         * slow-moving fuel and lamps. */
        worst_psi =
            (unsigned)absdiff((int)d.oil_pressure_psi_x10, (int)drive.oil_pressure_psi_x10) >
                    worst_psi
                ? (unsigned)absdiff((int)d.oil_pressure_psi_x10, (int)drive.oil_pressure_psi_x10)
                : worst_psi;
        worst_degf = (unsigned)absdiff(d.coolant_temp_degf_x10, drive.coolant_degf_x10) > worst_degf
                         ? (unsigned)absdiff(d.coolant_temp_degf_x10, drive.coolant_degf_x10)
                         : worst_degf;
        checked++;
    }
    CHECK(checked > 59000u);
    CHECK(worst_psi <= 120); /* oil pressure moves at most a few psi in 20 ms (rpm sweeps) */
    /* The coolant reaches the display up to 100 ms late (the slow message's cycle): five 20 ms steps of the drive's own motion, plus 2 tenths for
     * the degree F to degree C to degree F rounding. The bound follows from the cycle time, it is not a number chosen to pass. */
    CHECK(worst_degf <= 5u * max_coolant_step + 2u);
    CHECK(l_sent > 4000u);   /* messages really went out */
    /* The FDCAN controller has 3 transmit buffers: no 20 ms step may send more frames than that
     * (the Dash's own included). */
    CHECK(l_max_burst <= 3u);
}

static void test_values_are_exact_when_the_drive_is_still(void)
{
    dash_loopback_source_t src;
    dash_sim_out_t drive;
    struct dash_display d;

    l_head = l_tail = 0;
    l_now = 5000;
    CHECK(dash_can_init(&k_port));
    CHECK(dash_loopback_source_init(&src, &k_port));
    memset(&drive, 0, sizeof(drive));
    drive.oil_pressure_psi_x10 = 450; /* 45.0 psi */
    drive.oil_temp_degf_x10 = 2300;   /* 230.0 F */
    drive.coolant_degf_x10 = 1950;    /* 195.0 F */
    drive.fuel_pct_x10 = 755;         /* 75.5 % */
    drive.lights = DASH_SIM_LIGHT_CHARGE | DASH_SIM_LIGHT_FUEL_LOW;

    for (int i = 0; i < 40; i++) {
        l_now += 20;
        dash_loopback_source_step(&src, &drive);
        dash_can_step();
    }
    dash_can_get_display(&d);

    CHECK(absdiff((int)d.oil_pressure_psi_x10, 450) <=
          2); /* psi -> bar x100 -> psi: within rounding */
    CHECK(absdiff(d.oil_temp_degf_x10, 2300) <= 2);
    CHECK(absdiff(d.coolant_temp_degf_x10, 1950) <= 2);
    CHECK(d.fuel_level_pct_x10 == 755);
    CHECK(d.lights == (DASH_SIM_LIGHT_CHARGE | DASH_SIM_LIGHT_FUEL_LOW));
}

static void test_silence_goes_stale_and_alerts(void)
{
    dash_loopback_source_t src;
    dash_sim_out_t drive;
    struct dash_display d;

    l_head = l_tail = 0;
    l_now = 1000;
    CHECK(dash_can_init(&k_port));
    CHECK(dash_loopback_source_init(&src, &k_port));
    memset(&drive, 0, sizeof(drive));
    for (int i = 0; i < 50; i++) {
        l_now += 20;
        dash_loopback_source_step(&src, &drive);
        dash_can_step();
    }
    dash_can_get_display(&d);
    CHECK(d.analog_state == TCAN_RX_FRESH && d.ethanol_state == TCAN_RX_FRESH && !d.ethanol_alert);

    /* The simulated Nodes stop. The Dash keeps stepping on its own: nothing may stay "fresh". */
    l_now +=
        400; /* more than 3 cycles of the 100 ms ethanol message and of the 50 ms analog message */
    dash_can_step();
    dash_can_get_display(&d);
    CHECK(d.analog_state == TCAN_RX_STALE);
    CHECK(d.lights_state == TCAN_RX_STALE);
    CHECK(d.ethanol_state == TCAN_RX_STALE);
    CHECK(d.ethanol_alert); /* the Ethanol Node going quiet is critical */
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    test_conversions();
    test_demo_drive_survives_the_bus_round_trip();
    test_values_are_exact_when_the_drive_is_still();
    test_silence_goes_stale_and_alerts();

    printf("%d checks, %d failures\n", l_checks, l_failures);
    return l_failures == 0 ? 0 : 1;
}
