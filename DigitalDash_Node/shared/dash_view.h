/*******************************************************
 * @file dash_view.h
 * @brief What the dash shows, decided from numbers: lit segments, the boost fill, lamp and box
 * states.
 *
 * Hardware and LVGL independent, so every rule is tested on the host. Every threshold lives in
 * dash_config_t, a set of VARIABLES the owner can change after real driving experience (TCAN-5).
 * The limits come from dash_thresholds.h.
 *
 * Rules that never change:
 *  - Missing or stale data is never shown as normal: a box shows NO DATA, a lamp is dimmed.
 *  - Values are in tenths (0.1 units) where a fraction matters.
 *******************************************************/
#ifndef DASH_VIEW_H
#define DASH_VIEW_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*******************************************************
 * Macros
 *******************************************************/
#define DASH_MAIN_SEGMENTS 60u /* the main RPM bar */
#define DASH_TOP_SEGMENTS 16u  /* the shift lights */
#define DASH_BOOST_FILL_W 161u /* pixels of the boost fill (dash_bg_layout.json: boost_fill) */

/* "No limit" markers for a gauge box. */
#define DASH_LIMIT_NONE_HIGH INT32_MAX
#define DASH_LIMIT_NONE_LOW INT32_MIN

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef enum {
    DASH_LEVEL_NO_DATA = 0, /* missing or stale */
    DASH_LEVEL_NORMAL,
    DASH_LEVEL_WARNING,
    DASH_LEVEL_ALERT,
} dash_level_t;

typedef enum {
    DASH_LAMP_NOT_LIT = 0, /* dimmed cyan: no data, or not monitored */
    DASH_LAMP_NORMAL,      /* cyan: monitored and healthy */
    DASH_LAMP_ACTIVE,      /* red: the condition is active */
} dash_lamp_t;

/* Limits for one gauge box. A value at or beyond a limit is in that level. */
typedef struct {
    int32_t alert_low;
    int32_t warn_low;
    int32_t warn_high;
    int32_t alert_high;
    int32_t hysteresis; /* how far back inside a limit a value must come to leave the level */
} dash_limits_t;

typedef struct {
    /* Main RPM bar: 60 segments evenly over 0 to rpm_full_scale. */
    uint16_t rpm_full_scale;
    /* Shift lights: 16 segments evenly over shift_start_rpm to shift_end_rpm. */
    uint16_t shift_start_rpm;
    uint16_t shift_end_rpm;
    uint16_t shift_flash_rpm; /* at or above this, all 16 flash */
    /* Boost: a full bar means this boost, in tenths of a PSI. */
    int32_t boost_max_psi_x10;
    /* Gauge boxes (values in tenths: degF, PSI, volts, percent). */
    dash_limits_t coolant_degf_x10;
    dash_limits_t oil_temp_degf_x10;
    dash_limits_t oil_pressure_psi_x10;
    dash_limits_t battery_volts_x10;
    dash_limits_t fuel_pct_x10;
} dash_config_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* Defaults: the scale and shift range from the design, and the gauge limits in dash_thresholds.h.
 * Battery and fuel have no limit here (the car's own lamps drive them). */
void dash_config_defaults(dash_config_t *cfg);

/* Segments lit on the main bar: round(rpm * 60 / full scale), 0 to 60. */
uint8_t dash_main_lit(uint16_t rpm, const dash_config_t *cfg);

/* Segments lit on the shift lights: 0 at or below the start, 16 at or above the end. */
uint8_t dash_top_lit(uint16_t rpm, const dash_config_t *cfg);

/* True when all 16 shift lights should flash. */
bool dash_top_flash(uint16_t rpm, const dash_config_t *cfg);

/* Width in pixels of the boost fill, 0 to 161. Empty at 0 PSI or in vacuum, full at the maximum. */
uint16_t dash_boost_fill_px(int32_t boost_psi_x10, const dash_config_t *cfg);

/* Lamp state. A lamp that cannot be judged is dimmed, never normal. */
dash_lamp_t dash_lamp(bool data_valid, bool condition_active);

/* Ethanol box level. The box itself carries the ethanol problem (red box, red text); the
 * full-screen error is kept for riskier data. ALERT (red) when the Ethanol Node reports a critical
 * fault, has gone silent or never appeared (alert), or is feeding the ECU its default E85 instead
 * of a measurement (output_is_default, only meaningful with fresh data). NO_DATA (grey) only while
 * nothing is wrong yet but nothing has arrived. NORMAL otherwise. A defaulted value must never look
 * like a reading. */
dash_level_t dash_ethanol_level(bool data_valid, bool output_is_default, bool alert);

/* Gauge box level with hysteresis. `prev` is the level shown last time. */
dash_level_t dash_box_level(bool data_valid, int32_t value, const dash_limits_t *lim,
                            dash_level_t prev);

/* True when a value prints in a 3 character gauge box (-99 .. max_value). Anything else shows "--".
 */
bool dash_box_value_fits(int32_t value, int32_t max_value);

/* Writes a tenths value as text ("14.2", "-8.5"). Returns false and writes "--" if the text is
 * longer than max_chars (it would wrap in its box) or the buffer is too small. */
bool dash_fmt_tenths(char *buf, size_t size, int32_t v_x10, size_t max_chars);

/* km/h in hundredths to mph in tenths, rounded. */
uint32_t dash_kmh_x100_to_mph_x10(uint32_t kmh_x100);

#endif /* DASH_VIEW_H */
