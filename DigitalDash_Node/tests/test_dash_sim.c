/*******************************************************
 * @file test_dash_sim.c
 * @brief Host tests for the demo drive: hand-computed gear maths, and physical sanity checks on
 * every 20 ms step of a 20 minute run.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>
#include <string.h>

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

#define STEP_MS 20u
#define RUN_STEPS (20u * 60u * 1000u / STEP_MS) /* 20 minutes */

/*******************************************************
 * Static Variables
 *******************************************************/
static int l_checks;
static int l_failures;

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void test_gear_maths(void)
{
    /* rpm = km/h x (rpm per km/h): 1st 115.72, 2nd 65.23, 3rd 45.77, 4th 32.57, 5th 24.73 (worked
     * out from the ratios x 3.9 final drive, 1.94 m tyre). Speeds are in hundredths of km/h. */
    CHECK(dash_sim_rpm_for(1, 5000) == 5786);  /* 50 km/h */
    CHECK(dash_sim_rpm_for(2, 10000) == 6523); /* 100 km/h */
    CHECK(dash_sim_rpm_for(3, 10000) == 4577);
    CHECK(dash_sim_rpm_for(4, 10000) == 3257);
    CHECK(dash_sim_rpm_for(5, 10000) == 2473);
    CHECK(dash_sim_rpm_for(1, 0) == 0);
    CHECK(dash_sim_rpm_for(0, 10000) == 0); /* neutral */
    CHECK(dash_sim_rpm_for(6, 10000) == 0); /* there is no 6th */
    /* A 6800 rpm shift in 1st gear is at about 58.8 km/h, as the model assumes. */
    CHECK(dash_sim_rpm_for(1, 5876) >= 6798 && dash_sim_rpm_for(1, 5876) <= 6802);
}

static void test_start_up(void)
{
    dash_sim_t s;
    dash_sim_out_t o;
    uint16_t early_lights = 0;
    uint16_t max_rpm = 0;

    dash_sim_init(&s);
    for (uint32_t i = 0; i < 45; i++) { /* 0.9 s: the starter is turning */
        dash_sim_step(&s, STEP_MS, &o);
        early_lights |= o.lights;
        if (o.rpm > max_rpm) {
            max_rpm = o.rpm;
        }
        CHECK(o.oil_pressure_psi_x10 <= 20);
    }
    CHECK(max_rpm <= 400);                              /* cranking, not running */
    CHECK((early_lights & DASH_SIM_LIGHT_OIL) != 0);    /* no oil pressure yet: lamp on */
    CHECK((early_lights & DASH_SIM_LIGHT_CHARGE) != 0); /* charge lamp on until the engine runs */
    CHECK(o.battery_volts_x10 < 125);                   /* pulled down by the starter */
    CHECK(o.gear == 0 && o.speed_kmh_x100 == 0);
}

static void test_determinism_and_bad_dt(void)
{
    dash_sim_t a;
    dash_sim_t b;
    dash_sim_out_t oa;
    dash_sim_out_t ob;

    memset(&oa, 0, sizeof(oa)); /* so the padding between fields compares equal */
    memset(&ob, 0, sizeof(ob));
    dash_sim_init(&a);
    dash_sim_init(&b);
    for (uint32_t i = 0; i < 5000; i++) {
        dash_sim_step(&a, STEP_MS, &oa);
        dash_sim_step(&b, STEP_MS, &ob);
    }
    CHECK(memcmp(&oa, &ob, sizeof(oa)) == 0);

    dash_sim_step(&a, 0, &oa);           /* a zero step still advances safely */
    dash_sim_step(&a, 4000000000u, &oa); /* a huge step is clamped, it does not blow the model up */
    CHECK(oa.rpm <= 7500 && oa.speed_kmh_x100 <= 20000);
}

static void test_long_run(void)
{
    dash_sim_t s;
    dash_sim_out_t o;
    dash_sim_out_t prev;
    uint32_t gear_steps = 0;
    uint32_t off_steps = 0;
    uint32_t last_change_ms = 0;
    uint16_t max_speed = 0;
    int16_t max_boost = -1000;
    int16_t min_boost = 1000;
    uint8_t max_gear = 0;
    int32_t max_cool = 0;
    int32_t max_oilt = 0;
    uint16_t min_press_running = 1000;
    uint16_t seen = 0;
    bool low_fuel_cleared = false;
    bool seen_low_fuel = false;
    bool upshifts_ok = true;
    uint32_t upshifts = 0;

    dash_sim_init(&s);
    dash_sim_step(&s, STEP_MS, &prev);
    for (uint32_t i = 1; i < RUN_STEPS; i++) {
        uint32_t t_ms = i * STEP_MS;

        dash_sim_step(&s, STEP_MS, &o);

        CHECK(o.rpm <= 7500);
        CHECK(o.speed_kmh_x100 <= 20000);
        CHECK(o.gear <= 5);
        CHECK(o.throttle_pct <= 100);
        CHECK(o.boost_psi_x10 >= -150 && o.boost_psi_x10 <= 200);
        CHECK(o.afr_x10 >= 90 && o.afr_x10 <= 230);
        CHECK(o.battery_volts_x10 >= 90 && o.battery_volts_x10 <= 160);
        CHECK(o.coolant_degf_x10 >= 1700 && o.coolant_degf_x10 <= 2600);
        CHECK(o.oil_temp_degf_x10 >= 1600 && o.oil_temp_degf_x10 <= 2800);
        CHECK(o.oil_pressure_psi_x10 <= 1000);
        CHECK(o.fuel_pct_x10 >= 90 && o.fuel_pct_x10 <= 1000);

        /* Smoothness: nothing jumps. Full throttle accelerates at most 20 km/h/s, braking 15
         * km/h/s. */
        CHECK((int)o.speed_kmh_x100 - (int)prev.speed_kmh_x100 <= 60 &&
              (int)prev.speed_kmh_x100 - (int)o.speed_kmh_x100 <= 60);
        CHECK((int)o.rpm - (int)prev.rpm <= 200 && (int)prev.rpm - (int)o.rpm <= 200);

        /* Gear changes by one, or to and from neutral. */
        if (o.gear != prev.gear && o.gear != 0 && prev.gear != 0) {
            CHECK(o.gear == prev.gear + 1 || o.gear + 1 == prev.gear ||
                  (o.gear == 3 && prev.gear == 5));
            /* Full throttle upshifts out of 1st and 2nd happen at the 6,800 rpm shift point (the
             * displayed rpm is a few revs below it when the gear changes). Shifts at the end of a
             * pull are a different case. */
            if (o.gear == prev.gear + 1 && prev.gear <= 2 && prev.throttle_pct == 100) {
                upshifts++;
                if (prev.rpm < 6600 || prev.rpm > 7000) {
                    upshifts_ok = false;
                }
            }
        }

        if (o.gear != prev.gear) {
            last_change_ms = t_ms;
        }

        /* Road speed and rpm agree through the gear, except for the second after a gear change
         * while the displayed rpm falls or rises to the new gear (at most 9,000 up and 6,000 down
         * per second). */
        if (o.gear >= 2 && t_ms - last_change_ms > 1000) {
            int32_t want = dash_sim_rpm_for(o.gear, o.speed_kmh_x100);
            int32_t diff = (int32_t)o.rpm - want;

            gear_steps++;
            if (diff < 0) {
                diff = -diff;
            }
            if (diff > want / 50 + 60) {
                off_steps++;
            }
        }

        if (o.speed_kmh_x100 > max_speed) {
            max_speed = o.speed_kmh_x100;
        }
        if (o.boost_psi_x10 > max_boost) {
            max_boost = o.boost_psi_x10;
        }
        if (o.boost_psi_x10 < min_boost) {
            min_boost = o.boost_psi_x10;
        }
        if (o.gear > max_gear) {
            max_gear = o.gear;
        }
        if (o.coolant_degf_x10 > max_cool) {
            max_cool = o.coolant_degf_x10;
        }
        if (o.oil_temp_degf_x10 > max_oilt) {
            max_oilt = o.oil_temp_degf_x10;
        }
        if (t_ms > 10000 && o.rpm > 2000 && o.oil_pressure_psi_x10 < min_press_running) {
            min_press_running = o.oil_pressure_psi_x10;
        }
        if (t_ms > 5000) { /* after the engine start */
            seen |= o.lights;
        }
        if (o.lights & DASH_SIM_LIGHT_FUEL_LOW) {
            seen_low_fuel = true;
        } else if (seen_low_fuel) {
            low_fuel_cleared = true; /* refuelled */
        }
        prev = o;
    }

    CHECK(max_speed >= 16900 && max_speed <= 17200); /* the first pull ends at 170 km/h */
    CHECK(max_gear == 5);
    CHECK(max_boost >= 150 && max_boost <= 170); /* 15 to 17 psi at the peak */
    CHECK(min_boost <= -100);                    /* manifold vacuum at idle and lift off */
    CHECK(max_cool >= 2200);                     /* the coolant crosses the 220 F limit */
    CHECK(max_oilt >= 2350);                     /* and the oil crosses 235 F in a long run */
    CHECK(min_press_running >= 180); /* a healthy engine above 2,000 rpm holds 18 psi or more */
    CHECK(gear_steps > 5000 && off_steps == 0); /* rpm always matches the gear and road speed */
    CHECK(upshifts >= 40 && upshifts_ok);       /* 2 per loop, all near 6,800 rpm */
    CHECK((seen & DASH_SIM_LIGHT_OIL) != 0);    /* the injected oil pressure sag */
    CHECK((seen & DASH_SIM_LIGHT_CHARGE) != 0); /* the charging fault */
    CHECK((seen & DASH_SIM_LIGHT_CHECK_ENGINE) != 0);
    CHECK(seen_low_fuel && low_fuel_cleared); /* the tank runs low, then is refilled */
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    test_gear_maths();
    test_start_up();
    test_determinism_and_bad_dt();
    test_long_run();

    printf("%d checks, %d failures\n", l_checks, l_failures);
    return l_failures == 0 ? 0 : 1;
}
