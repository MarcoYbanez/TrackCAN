/*******************************************************
 * @file dash_view.c
 * @brief What the dash shows, decided from numbers: lit segments, the boost fill, lamp and box
 * states.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "dash_view.h"

#include <stddef.h>

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void limits_none(dash_limits_t *l)
{
    l->alert_low = DASH_LIMIT_NONE_LOW;
    l->warn_low = DASH_LIMIT_NONE_LOW;
    l->warn_high = DASH_LIMIT_NONE_HIGH;
    l->alert_high = DASH_LIMIT_NONE_HIGH;
    l->hysteresis = 0;
}

/* A high limit moved so that it is easier to LEAVE the level, without overflowing the "none"
 * marker. */
static int32_t high_eff(int32_t limit, int32_t hyst, bool inside)
{
    return (inside && limit != DASH_LIMIT_NONE_HIGH) ? limit - hyst : limit;
}

static int32_t low_eff(int32_t limit, int32_t hyst, bool inside)
{
    return (inside && limit != DASH_LIMIT_NONE_LOW) ? limit + hyst : limit;
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void dash_config_defaults(dash_config_t *cfg)
{
    cfg->rpm_full_scale = 9000;
    cfg->shift_start_rpm = 5500;
    cfg->shift_end_rpm = 8500;
    cfg->shift_flash_rpm = 8500;
    cfg->boost_max_psi_x10 = 200; /* PLACEHOLDER: 20.0 PSI, the owner sets the real maximum */
    limits_none(&cfg->coolant_degf_x10);
    limits_none(&cfg->oil_temp_degf_x10);
    limits_none(&cfg->oil_pressure_psi_x10);
    limits_none(&cfg->battery_volts_x10);
    limits_none(&cfg->fuel_pct_x10);
}

uint8_t dash_main_lit(uint16_t rpm, const dash_config_t *cfg)
{
    uint32_t full = cfg->rpm_full_scale;
    uint32_t lit;

    if (full == 0) {
        return 0;
    }
    lit = ((uint32_t)rpm * DASH_MAIN_SEGMENTS + full / 2u) / full;
    return (uint8_t)((lit > DASH_MAIN_SEGMENTS) ? DASH_MAIN_SEGMENTS : lit);
}

uint8_t dash_top_lit(uint16_t rpm, const dash_config_t *cfg)
{
    uint32_t span;
    uint32_t lit;

    if (cfg->shift_end_rpm <= cfg->shift_start_rpm || rpm <= cfg->shift_start_rpm) {
        return 0;
    }
    span = (uint32_t)cfg->shift_end_rpm - cfg->shift_start_rpm;
    /* Segment k (1..16) lights when rpm >= start + k * span / 16. */
    lit = ((uint32_t)(rpm - cfg->shift_start_rpm) * DASH_TOP_SEGMENTS) / span;
    return (uint8_t)((lit > DASH_TOP_SEGMENTS) ? DASH_TOP_SEGMENTS : lit);
}

bool dash_top_flash(uint16_t rpm, const dash_config_t *cfg)
{
    return cfg->shift_flash_rpm != 0 && rpm >= cfg->shift_flash_rpm;
}

uint16_t dash_boost_fill_px(int32_t boost_psi_x10, const dash_config_t *cfg)
{
    int64_t px;

    if (boost_psi_x10 <= 0 || cfg->boost_max_psi_x10 <= 0) {
        return 0;
    }
    if (boost_psi_x10 >= cfg->boost_max_psi_x10) {
        return DASH_BOOST_FILL_W;
    }
    px = ((int64_t)boost_psi_x10 * DASH_BOOST_FILL_W + cfg->boost_max_psi_x10 / 2) /
         cfg->boost_max_psi_x10;
    return (uint16_t)px;
}

dash_lamp_t dash_lamp(bool data_valid, bool condition_active)
{
    if (!data_valid) {
        return DASH_LAMP_NOT_LIT;
    }
    return condition_active ? DASH_LAMP_ACTIVE : DASH_LAMP_NORMAL;
}

dash_level_t dash_box_level(bool data_valid, int32_t value, const dash_limits_t *lim,
                            dash_level_t prev)
{
    int32_t ah;
    int32_t al;
    int32_t wh;
    int32_t wl;

    if (!data_valid || lim == NULL) {
        return DASH_LEVEL_NO_DATA;
    }

    /* To leave a level a value must come back inside by the hysteresis. */
    ah = high_eff(lim->alert_high, lim->hysteresis, prev == DASH_LEVEL_ALERT);
    al = low_eff(lim->alert_low, lim->hysteresis, prev == DASH_LEVEL_ALERT);
    wh = high_eff(lim->warn_high, lim->hysteresis, prev >= DASH_LEVEL_WARNING);
    wl = low_eff(lim->warn_low, lim->hysteresis, prev >= DASH_LEVEL_WARNING);

    if ((lim->alert_high != DASH_LIMIT_NONE_HIGH && value >= ah) ||
        (lim->alert_low != DASH_LIMIT_NONE_LOW && value <= al)) {
        return DASH_LEVEL_ALERT;
    }
    if ((lim->warn_high != DASH_LIMIT_NONE_HIGH && value >= wh) ||
        (lim->warn_low != DASH_LIMIT_NONE_LOW && value <= wl)) {
        return DASH_LEVEL_WARNING;
    }
    return DASH_LEVEL_NORMAL;
}

bool dash_box_value_fits(int32_t value, int32_t max_value)
{
    return value >= -99 && value <= max_value;
}

uint32_t dash_kmh_x100_to_mph_x10(uint32_t kmh_x100)
{
    /* 1 km/h = 0.621371 mph, so mph x10 = km/h x100 * 0.621371 / 10 = kmh_x100 * 62137 / 1,000,000.
     */
    return (uint32_t)(((uint64_t)kmh_x100 * 62137u + 500000u) / 1000000u);
}
