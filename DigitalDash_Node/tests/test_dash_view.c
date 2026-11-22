/*******************************************************
 * @file test_dash_view.c
 * @brief Host tests for the dash view rules. Expected values are worked out by hand.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>

#include "dash_view.h"

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

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void test_main_bar(void)
{
    dash_config_t c;

    dash_config_defaults(&c); /* full scale 9000 rpm over 60 segments: 150 rpm per segment */

    CHECK(dash_main_lit(0, &c) == 0);
    CHECK(dash_main_lit(74, &c) == 0); /* just under half a segment */
    CHECK(dash_main_lit(75, &c) == 1); /* half a segment rounds up */
    CHECK(dash_main_lit(150, &c) == 1);
    CHECK(dash_main_lit(4500, &c) == 30);
    CHECK(dash_main_lit(6600, &c) == 44); /* the end of the cyan zone */
    CHECK(dash_main_lit(7500, &c) == 50); /* the end of the amber zone */
    CHECK(dash_main_lit(9000, &c) == 60);
    CHECK(dash_main_lit(12000, &c) == 60); /* over the scale: never more than 60 */
    CHECK(dash_main_lit(65535, &c) == 60);

    c.rpm_full_scale = 0; /* a bad setting must not divide by zero */
    CHECK(dash_main_lit(5000, &c) == 0);

    c.rpm_full_scale = 8000; /* the owner changes the scale: 4000 rpm is half the bar */
    CHECK(dash_main_lit(4000, &c) == 30);
}

static void test_shift_lights(void)
{
    dash_config_t c;

    dash_config_defaults(&c); /* 5500 to 8500: 187.5 rpm per segment */

    CHECK(dash_top_lit(0, &c) == 0);
    CHECK(dash_top_lit(5500, &c) == 0); /* at the start nothing is lit yet */
    CHECK(dash_top_lit(5687, &c) == 0); /* one rpm short of the first segment */
    CHECK(dash_top_lit(5688, &c) == 1); /* 5500 + 187.5 */
    CHECK(dash_top_lit(6500, &c) == 5); /* (1000 * 16) / 3000 = 5 */
    CHECK(dash_top_lit(7000, &c) == 8);
    CHECK(dash_top_lit(8499, &c) == 15);
    CHECK(dash_top_lit(8500, &c) == 16);
    CHECK(dash_top_lit(9500, &c) == 16);

    /* A nonsense range lights nothing instead of misbehaving. */
    c.shift_end_rpm = c.shift_start_rpm;
    CHECK(dash_top_lit(8000, &c) == 0);

    dash_config_defaults(&c);
    CHECK(!dash_top_flash(8499, &c));
    CHECK(dash_top_flash(8500, &c));
    c.shift_flash_rpm = 0; /* 0 turns flashing off */
    CHECK(!dash_top_flash(9000, &c));
}

static void test_boost_bar(void)
{
    dash_config_t c;

    dash_config_defaults(&c); /* full bar = 20.0 PSI over 161 pixels */

    CHECK(dash_boost_fill_px(0, &c) == 0);
    CHECK(dash_boost_fill_px(-125, &c) == 0);  /* vacuum: an empty bar */
    CHECK(dash_boost_fill_px(100, &c) == 81);  /* 10.0 PSI: 161 / 2 = 80.5 rounds to 81 */
    CHECK(dash_boost_fill_px(200, &c) == 161); /* full boost = full bar */
    CHECK(dash_boost_fill_px(350, &c) == 161); /* over the maximum: still full */
    CHECK(dash_boost_fill_px(1, &c) == 1);     /* 0.1 PSI: 161 / 200 = 0.8 rounds to 1 */

    c.boost_max_psi_x10 = 150; /* the owner lowers the maximum: the same boost fills more */
    CHECK(dash_boost_fill_px(100, &c) == 107); /* 161 * 100 / 150 = 107.3 */
    c.boost_max_psi_x10 = 0;                   /* a bad setting is an empty bar, not a crash */
    CHECK(dash_boost_fill_px(100, &c) == 0);
}

static void test_lamps(void)
{
    CHECK(dash_lamp(false, false) == DASH_LAMP_NOT_LIT);
    CHECK(dash_lamp(false, true) ==
          DASH_LAMP_NOT_LIT); /* no data is never "active" and never "normal" */
    CHECK(dash_lamp(true, false) == DASH_LAMP_NORMAL);
    CHECK(dash_lamp(true, true) == DASH_LAMP_ACTIVE);
}

static void test_box_levels(void)
{
    dash_limits_t l = {
        .alert_low = 100, .warn_low = 150, .warn_high = 2200, .alert_high = 2400, .hysteresis = 20};
    dash_level_t lv;

    /* Missing data is never normal. */
    CHECK(dash_box_level(false, 1800, &l, DASH_LEVEL_NORMAL) == DASH_LEVEL_NO_DATA);
    CHECK(dash_box_level(true, 1800, NULL, DASH_LEVEL_NORMAL) == DASH_LEVEL_NO_DATA);

    CHECK(dash_box_level(true, 1800, &l, DASH_LEVEL_NORMAL) == DASH_LEVEL_NORMAL);
    CHECK(dash_box_level(true, 2199, &l, DASH_LEVEL_NORMAL) == DASH_LEVEL_NORMAL);
    CHECK(dash_box_level(true, 2200, &l, DASH_LEVEL_NORMAL) ==
          DASH_LEVEL_WARNING); /* at the limit */
    CHECK(dash_box_level(true, 2400, &l, DASH_LEVEL_NORMAL) == DASH_LEVEL_ALERT);
    CHECK(dash_box_level(true, 150, &l, DASH_LEVEL_NORMAL) == DASH_LEVEL_WARNING);
    CHECK(dash_box_level(true, 100, &l, DASH_LEVEL_NORMAL) == DASH_LEVEL_ALERT);

    /* Hysteresis on the way down from a high alert: it must come back 20 inside the limit. */
    lv = dash_box_level(true, 2400, &l, DASH_LEVEL_NORMAL);
    CHECK(lv == DASH_LEVEL_ALERT);
    lv = dash_box_level(true, 2390, &l, lv); /* below 2400 but within 20 of it: stays in alert */
    CHECK(lv == DASH_LEVEL_ALERT);
    lv = dash_box_level(true, 2379, &l,
                        lv); /* back below 2380: drops to warning (still over 2180) */
    CHECK(lv == DASH_LEVEL_WARNING);
    lv = dash_box_level(true, 2190, &l, lv); /* warning needs to fall below 2180 to clear */
    CHECK(lv == DASH_LEVEL_WARNING);
    lv = dash_box_level(true, 2179, &l, lv);
    CHECK(lv == DASH_LEVEL_NORMAL);

    /* Hysteresis on a low limit (for example oil pressure). */
    lv = dash_box_level(true, 150, &l, DASH_LEVEL_NORMAL);
    CHECK(lv == DASH_LEVEL_WARNING);
    lv = dash_box_level(true, 165, &l, lv); /* above 150 but within 20: stays in warning */
    CHECK(lv == DASH_LEVEL_WARNING);
    lv = dash_box_level(true, 171, &l, lv);
    CHECK(lv == DASH_LEVEL_NORMAL);
}

static void test_unset_limits_never_warn(void)
{
    dash_config_t c;

    dash_config_defaults(&c);
    /* Battery has no limit: a good value is normal, extreme values do not overflow or flip levels.
     */
    CHECK(dash_box_level(true, 0, &c.battery_volts_x10, DASH_LEVEL_NORMAL) == DASH_LEVEL_NORMAL);
    CHECK(dash_box_level(true, INT32_MAX - 1, &c.battery_volts_x10, DASH_LEVEL_ALERT) ==
          DASH_LEVEL_NORMAL);
    CHECK(dash_box_level(true, INT32_MIN + 1, &c.battery_volts_x10, DASH_LEVEL_ALERT) ==
          DASH_LEVEL_NORMAL);
    CHECK(dash_box_level(true, 5, &c.fuel_pct_x10, DASH_LEVEL_NORMAL) == DASH_LEVEL_NORMAL);

    /* Only one side set: the other side stays unlimited. */
    c.battery_volts_x10.warn_low = 200;
    c.battery_volts_x10.alert_low = 100;
    CHECK(dash_box_level(true, 5000, &c.battery_volts_x10, DASH_LEVEL_NORMAL) == DASH_LEVEL_NORMAL);
    CHECK(dash_box_level(true, 150, &c.battery_volts_x10, DASH_LEVEL_NORMAL) == DASH_LEVEL_WARNING);
}

static void test_owner_thresholds(void)
{
    dash_config_t c;
    dash_level_t lv;

    dash_config_defaults(&c);

    /* Oil pressure: red below 15 psi, one fixed limit. Values are tenths of psi. */
    CHECK(dash_box_level(true, 150, &c.oil_pressure_psi_x10, DASH_LEVEL_NORMAL) ==
          DASH_LEVEL_NORMAL);
    CHECK(dash_box_level(true, 149, &c.oil_pressure_psi_x10, DASH_LEVEL_NORMAL) ==
          DASH_LEVEL_WARNING);
    CHECK(dash_box_level(true, 0, &c.oil_pressure_psi_x10, DASH_LEVEL_NORMAL) ==
          DASH_LEVEL_WARNING);
    CHECK(dash_box_level(true, 800, &c.oil_pressure_psi_x10, DASH_LEVEL_NORMAL) ==
          DASH_LEVEL_NORMAL);
    lv = dash_box_level(true, 140, &c.oil_pressure_psi_x10, DASH_LEVEL_NORMAL);
    lv = dash_box_level(true, 155, &c.oil_pressure_psi_x10, lv); /* 15.5 psi: not back far enough */
    CHECK(lv == DASH_LEVEL_WARNING);
    lv = dash_box_level(true, 160, &c.oil_pressure_psi_x10, lv); /* 16.0 psi: cleared */
    CHECK(lv == DASH_LEVEL_NORMAL);

    /* Coolant: red at 220 F. */
    CHECK(dash_box_level(true, 2199, &c.coolant_degf_x10, DASH_LEVEL_NORMAL) == DASH_LEVEL_NORMAL);
    CHECK(dash_box_level(true, 2200, &c.coolant_degf_x10, DASH_LEVEL_NORMAL) == DASH_LEVEL_WARNING);
    CHECK(dash_box_level(true, 1000, &c.coolant_degf_x10, DASH_LEVEL_NORMAL) == DASH_LEVEL_NORMAL);
    lv = dash_box_level(true, 2205, &c.coolant_degf_x10, DASH_LEVEL_NORMAL);
    lv = dash_box_level(true, 2190, &c.coolant_degf_x10,
                        lv); /* 219 F: still inside the 2 F margin */
    CHECK(lv == DASH_LEVEL_WARNING);
    lv = dash_box_level(true, 2170, &c.coolant_degf_x10, lv);
    CHECK(lv == DASH_LEVEL_NORMAL);

    /* Oil temperature: red at 235 F. */
    CHECK(dash_box_level(true, 2349, &c.oil_temp_degf_x10, DASH_LEVEL_NORMAL) == DASH_LEVEL_NORMAL);
    CHECK(dash_box_level(true, 2350, &c.oil_temp_degf_x10, DASH_LEVEL_NORMAL) ==
          DASH_LEVEL_WARNING);

    /* Missing data is still no data, whatever the limits are. */
    CHECK(dash_box_level(false, 0, &c.oil_pressure_psi_x10, DASH_LEVEL_NORMAL) ==
          DASH_LEVEL_NO_DATA);
}

static void test_box_fit(void)
{
    /* The gauge boxes hold exactly 3 characters: -99 .. 999, ethanol E0 .. E99. */
    CHECK(dash_box_value_fits(0, 999));
    CHECK(dash_box_value_fits(999, 999));
    CHECK(!dash_box_value_fits(1000, 999)); /* 4 digits would wrap onto a second line */
    CHECK(dash_box_value_fits(-99, 999));
    CHECK(!dash_box_value_fits(-100, 999)); /* "-100" is 4 characters */
    CHECK(dash_box_value_fits(99, 99));
    CHECK(!dash_box_value_fits(100, 99)); /* "E100" is 4 characters */
}

static void test_speed_units(void)
{
    /* 1 km/h = 0.621371 mph; result in tenths of mph. */
    CHECK(dash_kmh_x100_to_mph_x10(0) == 0);
    CHECK(dash_kmh_x100_to_mph_x10(10000) == 621);  /* 100.00 km/h = 62.1 mph */
    CHECK(dash_kmh_x100_to_mph_x10(16093) == 1000); /* 160.93 km/h = 100.0 mph */
    CHECK(dash_kmh_x100_to_mph_x10(65535) ==
          4072); /* 655.35 km/h = 407.2 mph, the largest the message holds */
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    test_main_bar();
    test_shift_lights();
    test_boost_bar();
    test_lamps();
    test_box_levels();
    test_unset_limits_never_warn();
    test_owner_thresholds();
    test_box_fit();
    test_speed_units();

    printf("%d checks, %d failures\n", l_checks, l_failures);
    return l_failures == 0 ? 0 : 1;
}
