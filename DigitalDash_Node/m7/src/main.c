/*******************************************************
 * @file main.c
 * @brief Dash, M7 core: the display. Reads the snapshot the M4 publishes and draws it on the
 *        SquareLine screen.
 *
 * Fail closed: if the M4 is missing or its heartbeat stops, the screen shows a big alert. It never
 * keeps showing old numbers as if they were live. Missing data shows "--", never a number.
 *
 * What is shown is decided by dash_view (tested on the host). The RPM bars are drawn by dash_render
 * from the saved geometry into transparent canvases laid over the background image.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <lvgl.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "dash_render.h"
#include "dash_shared.h"
#include "dash_view.h"
#include "dash_xcore.h"
#include "ui.h"

LOG_MODULE_REGISTER(dash_m7, LOG_LEVEL_INF);

/*******************************************************
 * Macros
 *******************************************************/
#define UI_PERIOD_MS 20
#define READ_PERIOD_MS 50
#define SHIFT_FLASH_MS 125 /* flash half period */

#define LIGHT_CHECK_ENGINE 0x0001u
#define LIGHT_OIL_WARNING 0x0002u
#define LIGHT_FUEL_LOW 0x0020u
#define LIGHT_CHARGE_WARNING 0x0100u

/* Bar canvases: ARGB8888, transparent where a segment is dark. */
LV_DRAW_BUF_DEFINE_STATIC(l_main_buf, 461, 39, LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(l_top_buf, 461, 9, LV_COLOR_FORMAT_ARGB8888);

/*******************************************************
 * Static Variables
 *******************************************************/
static lv_obj_t *l_main_canvas;
static lv_obj_t *l_top_canvas;
static lv_obj_t *l_boost_fill;
static lv_obj_t *l_alert; /* full-screen alert, hidden until needed */

static uint8_t l_main_lit;
static uint8_t l_top_lit;
static dash_config_t l_cfg; /* the warning thresholds: variables, see dash_view.h */

/* Last level shown per gauge box, for hysteresis. */
static dash_level_t l_lvl_coolant, l_lvl_oil_temp, l_lvl_oil_press, l_lvl_fuel;

static uint32_t l_last_log_s;
static bool l_boot_frame_logged;
static bool l_boot_data_logged;
static volatile uint32_t l_signals; /* counts the M4's "new snapshot" signals */

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void ipm_cb(const struct device *dev, void *user_data, uint32_t id, volatile void *data)
{
    (void)dev;
    (void)user_data;
    (void)id;
    (void)data;
    l_signals++; /* nothing is drawn here: it is only a wake-up hint */
}

static lv_obj_t *bar_canvas(lv_draw_buf_t *buf, const dash_bar_t *bar, uint8_t *lit)
{
    lv_obj_t *c = lv_canvas_create(ui_Screen1);

    lv_canvas_set_draw_buf(c, buf);
    lv_obj_set_pos(c, bar->screen_x, bar->screen_y);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    dash_bar_clear((uint32_t *)buf->data, buf->header.stride / 4u, bar, lit);
    lv_obj_move_to_index(c, 1); /* above the background image, below every label and lamp */
    return c;
}

static void ui_build(void)
{
    ui_init(); /* the SquareLine screen */

    /* The export creates the gauge tile overlays AFTER the number labels, and each overlay has an
     * opaque background, so they would draw over the numbers and hide them. Put them just above the
     * background image, under every label. (Fixed here, not in the design, so a re-export does not
     * undo it.) */
    {
        lv_obj_t *const overlays[] = {ui_Oil_Pressure_Overlay, ui_Oil_Temp_Overlay, ui_AFR_Overlay,
                                      ui_Ethanol_Overlay,      ui_Battery_Overlay,  ui_Fuel_Overlay,
                                      ui_Coolant_Overlay};

        for (size_t i = 0; i < sizeof(overlays) / sizeof(overlays[0]); i++) {
            lv_obj_move_to_index(overlays[i], 1);
        }
    }

    /* The design's sample text ("100", "12.1", "-8.5") must never look like a reading: until the
     * first live snapshot every value shows "--". */
    {
        lv_obj_t *const values[] = {ui_MPH_Label, ui_Coolant,   ui_Oil_Pressure,
                                    ui_AFR,       ui_Oil_Temp,  ui_Ethanol,
                                    ui_Battery,   ui_Gas_Level, ui_Boost_Label};

        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
            lv_label_set_text(values[i], "--");
        }
    }

    l_main_canvas = bar_canvas(&l_main_buf, &dash_main_bar, &l_main_lit);
    l_top_canvas = bar_canvas(&l_top_buf, &dash_top_bar, &l_top_lit);

    l_boost_fill = lv_obj_create(ui_Screen1);
    lv_obj_remove_style_all(l_boost_fill);
    lv_obj_set_pos(l_boost_fill, DASH_BOOST_FILL_X, DASH_BOOST_FILL_Y);
    lv_obj_set_size(l_boost_fill, 0, DASH_BOOST_FILL_H_PX);
    lv_obj_set_style_bg_opa(l_boost_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(l_boost_fill, lv_color_hex(DASH_BOOST_FILL_ARGB & 0xFFFFFFu), 0);
    lv_obj_add_flag(l_boost_fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(l_boost_fill, 1);

    /* The alert covers everything. */
    l_alert = lv_label_create(ui_Screen1);
    lv_obj_set_size(l_alert, LV_PCT(100), LV_PCT(100)); /* resolution independent */
    lv_obj_set_style_bg_opa(l_alert, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(l_alert, lv_color_hex(0xC00000), 0);
    lv_obj_set_style_text_color(l_alert, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l_alert, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(l_alert, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(l_alert, 90, 0);
    lv_obj_align(l_alert, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(l_alert, LV_OBJ_FLAG_HIDDEN);
}

static void ui_set_alert(bool on, const char *text)
{
    if (on) {
        lv_label_set_text(l_alert, text);
        lv_obj_remove_flag(l_alert, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(l_alert, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Custom state n (1 or 2) on, the other off. n = 0 clears both. */
static void set_user_state(lv_obj_t *o, int n)
{
    lv_obj_remove_state(o, LV_STATE_USER_1 | LV_STATE_USER_2);
    if (n == 1) {
        lv_obj_add_state(o, LV_STATE_USER_1);
    } else if (n == 2) {
        lv_obj_add_state(o, LV_STATE_USER_2);
    }
}

/* A gauge box: the text (NULL means no data) and the red tint on the label and its tile when out of
 * range. Anything that is not live shows "--", never a number. */
static void show_box_text(lv_obj_t *label, lv_obj_t *tile, const char *text, dash_level_t lvl)
{
    if (text != NULL) {
        lv_label_set_text(label, text);
    } else {
        lv_label_set_text(label, "--");
        lvl = DASH_LEVEL_NO_DATA;
    }
    set_user_state(label, lvl >= DASH_LEVEL_WARNING ? 1 : 0);
    set_user_state(tile, lvl >= DASH_LEVEL_WARNING ? 1 : 0);
}

/* A whole number in a 3 character box (-99 .. max_value): a value that does not fit shows "--"
 * instead of wrapping. */
static void show_box(lv_obj_t *label, lv_obj_t *tile, bool valid, int32_t value, int32_t max_value,
                     const char *fmt, dash_level_t lvl)
{
    char txt[8];

    if (valid && dash_box_value_fits(value, max_value)) {
        lv_snprintf(txt, sizeof(txt), fmt, (int)value);
        show_box_text(label, tile, txt, lvl);
    } else {
        show_box_text(label, tile, NULL, DASH_LEVEL_NO_DATA);
    }
}

/* A tenths value ("14.2") in a box that holds max_chars characters. */
static void show_tenths(lv_obj_t *label, lv_obj_t *tile, bool valid, int32_t v_x10,
                        size_t max_chars)
{
    char txt[12];

    if (valid && dash_fmt_tenths(txt, sizeof(txt), v_x10, max_chars)) {
        show_box_text(label, tile, txt, DASH_LEVEL_NORMAL);
    } else {
        show_box_text(label, tile, NULL, DASH_LEVEL_NO_DATA);
    }
}

static void show_lamp(lv_obj_t *lamp, dash_lamp_t st)
{
    if (st == DASH_LAMP_NOT_LIT) {
        lv_obj_add_flag(lamp, LV_OBJ_FLAG_HIDDEN); /* the dim outline in the background shows */
        return;
    }
    set_user_state(lamp, st == DASH_LAMP_ACTIVE ? 2 : 1);
    lv_obj_remove_flag(lamp, LV_OBJ_FLAG_HIDDEN);
}

/* Draws one good snapshot. */
static void show_snapshot(const dash_snapshot_t *s)
{
    const struct dash_display *d = &s->display;
    bool analog = d->analog_state == TCAN_RX_FRESH;
    bool lights = d->lights_state == TCAN_RX_FRESH;
    bool eth = d->ethanol_state == TCAN_RX_FRESH;
    int32_t oil_press = (int32_t)(d->oil_pressure_psi_x10 / 10u);
    int32_t oil_temp = d->oil_temp_degf_x10 / 10;
    int32_t coolant = d->coolant_temp_degf_x10 / 10;
    int32_t fuel = (int32_t)(d->fuel_level_pct_x10 / 10u);

    /* RPM bars. The engine values come from the M4's own timer, so they are fresh with the link. */
    if (dash_bar_set_lit((uint32_t *)l_main_buf.data, l_main_buf.header.stride / 4u, &dash_main_bar,
                         &l_main_lit, dash_main_lit(s->rpm, &l_cfg))) {
        lv_obj_invalidate(l_main_canvas);
    }
    if (dash_bar_set_lit((uint32_t *)l_top_buf.data, l_top_buf.header.stride / 4u, &dash_top_bar,
                         &l_top_lit, dash_top_lit(s->rpm, &l_cfg))) {
        lv_obj_invalidate(l_top_canvas);
    }
    if (dash_top_flash(s->rpm, &l_cfg)) {
        bool on = ((k_uptime_get_32() / SHIFT_FLASH_MS) & 1u) == 0u;

        if (on) {
            lv_obj_remove_flag(l_top_canvas, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(l_top_canvas, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_remove_flag(l_top_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text_fmt(ui_MPH_Label, "%u",
                          (unsigned)(dash_kmh_x100_to_mph_x10(s->speed_kmh_x100) / 10u));

    l_lvl_coolant = dash_box_level(analog, coolant, &l_cfg.coolant_degf_x10, l_lvl_coolant);
    l_lvl_oil_temp = dash_box_level(analog, oil_temp, &l_cfg.oil_temp_degf_x10, l_lvl_oil_temp);
    l_lvl_oil_press = dash_box_level(analog, (int32_t)d->oil_pressure_psi_x10,
                                     &l_cfg.oil_pressure_psi_x10, l_lvl_oil_press);
    l_lvl_fuel =
        dash_box_level(analog, (int32_t)d->fuel_level_pct_x10, &l_cfg.fuel_pct_x10, l_lvl_fuel);
    /* The car's own low-fuel lamp (read through an opto-coupler) turns the fuel box red. It already
     * includes the ECM's debounce and the factory refill hysteresis, so no percent limit is added
     * on top. */
    if (lights && (d->lights & LIGHT_FUEL_LOW)) {
        l_lvl_fuel = DASH_LEVEL_ALERT;
    }
    show_box(ui_Coolant, ui_Coolant_Overlay, analog, coolant, 999, "%d", l_lvl_coolant);
    show_box(ui_Oil_Temp, ui_Oil_Temp_Overlay, analog, oil_temp, 999, "%d", l_lvl_oil_temp);
    show_box(ui_Oil_Pressure, ui_Oil_Pressure_Overlay, analog, oil_press, 999, "%d",
             l_lvl_oil_press);
    show_box(ui_Gas_Level, ui_Fuel_Overlay, analog, fuel, 999, "%d", l_lvl_fuel);
    /* Red box and red text when the Node is feeding the ECU its default E85 instead of a
     * measurement. */
    show_box(ui_Ethanol, ui_Ethanol_Overlay, eth, d->ethanol_pct, 99, "E%d",
             dash_ethanol_level(eth, d->ethanol_output_default));

    /* Battery, AFR and boost have no CAN source yet (TCAN-4): the M4 marks them valid only in the
     * demo drive, otherwise they show "--". */
    show_tenths(ui_Battery, ui_Battery_Overlay, (s->extra_valid & DASH_EXTRA_BATTERY) != 0,
                s->battery_volts_x10, 4);
    show_tenths(ui_AFR, ui_AFR_Overlay, (s->extra_valid & DASH_EXTRA_AFR) != 0, s->afr_x10, 4);
    {
        char txt[12];
        bool ok = (s->extra_valid & DASH_EXTRA_BOOST) != 0 &&
                  dash_fmt_tenths(txt, sizeof(txt), s->boost_psi_x10, 5);
        uint16_t px = ok ? dash_boost_fill_px(s->boost_psi_x10, &l_cfg) : 0;

        lv_label_set_text(ui_Boost_Label, ok ? txt : "--");
        set_user_state(ui_Boost_Label, 0);
        if (px > 0) {
            lv_obj_set_width(l_boost_fill, px);
            lv_obj_remove_flag(l_boost_fill, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(l_boost_fill, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Once a second the serial console shows what the demo is doing, to compare with the screen. */
    if ((k_uptime_get_32() / 1000u) != l_last_log_s) {
        l_last_log_s = k_uptime_get_32() / 1000u;
        LOG_INF("gear %u  %u rpm  %u mph  boost %d.%d psi",
                (unsigned)((s->extra_valid & DASH_EXTRA_GEAR) ? s->gear : 0), (unsigned)s->rpm,
                (unsigned)(dash_kmh_x100_to_mph_x10(s->speed_kmh_x100) / 10u),
                (int)(s->boost_psi_x10 / 10),
                (int)(s->boost_psi_x10 < 0 ? -(s->boost_psi_x10 % 10) : s->boost_psi_x10 % 10));
        /* The values the boxes are given, and whether their data counts as live (1 = fresh). */
        LOG_INF("boxes: coolant %d F  oil %d F  press %u psi  fuel %u %%  eth %u %%  afr %u.%u  "
                "batt %u.%u V",
                (int)coolant, (int)oil_temp, (unsigned)oil_press, (unsigned)fuel,
                (unsigned)d->ethanol_pct, (unsigned)(s->afr_x10 / 10u),
                (unsigned)(s->afr_x10 % 10u), (unsigned)(s->battery_volts_x10 / 10u),
                (unsigned)(s->battery_volts_x10 % 10u));
        LOG_INF("fresh: analog %d  ethanol %d  lights %d  lights 0x%03x", analog, eth, lights,
                (unsigned)d->lights);
    }

    /* Lamps: the ones with a CAN source (the car's own lamps through opto-couplers). The others
     * stay dim. */
    show_lamp(ui_CEL_REG, dash_lamp(lights, lights && (d->lights & LIGHT_CHECK_ENGINE)));
    show_lamp(ui_OIL_REG, dash_lamp(lights, lights && (d->lights & LIGHT_OIL_WARNING)));
    show_lamp(ui_BAT_REG, dash_lamp(lights, lights && (d->lights & LIGHT_CHARGE_WARNING)));
    show_lamp(ui_TMP_REG, DASH_LAMP_NOT_LIT);
    show_lamp(ui_TCC_REG, DASH_LAMP_NOT_LIT);
    show_lamp(ui_CAN_REG, DASH_LAMP_NOT_LIT);
    show_lamp(ui_GPS_REG, DASH_LAMP_NOT_LIT);

    ui_set_alert(d->ethanol_alert, "CHECK\nETHANOL");
}

/* Reads one consistent snapshot under the hardware semaphore. */
static dash_read_t read_snapshot(dash_snapshot_t *snap, uint32_t *heartbeat)
{
    dash_read_t r;

    if (!dash_xcore_lock()) {
        return DASH_READ_BUSY;
    }
    sys_cache_data_invd_range(dash_xcore_shared(), sizeof(dash_shared_t)); /* M4 wrote it */
    r = dash_shared_read(dash_xcore_shared(), snap, heartbeat);
    dash_xcore_unlock();
    return r;
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    const struct device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    const struct device *const ipm = DEVICE_DT_GET(DT_NODELABEL(mailbox));
    dash_link_t link;
    dash_snapshot_t snap;
    uint32_t heartbeat = 0;
    int64_t next_read = 0;

    if (!device_is_ready(display)) {
        LOG_ERR("display not ready");
        return 0;
    }

    dash_xcore_clock_on();

    if (device_is_ready(ipm)) {
        ipm_register_callback(ipm, ipm_cb, NULL);
        ipm_set_enabled(ipm, 1);
    }

    dash_config_defaults(&l_cfg);
    LV_DRAW_BUF_INIT_STATIC(l_main_buf);
    LV_DRAW_BUF_INIT_STATIC(l_top_buf);
    ui_build();
    display_blanking_off(display);
    dash_link_init(&link, k_uptime_get_32());
    LOG_INF("boot: UI built, display on at %u ms after kernel start", (unsigned)k_uptime_get_32());

    for (;;) {
        int64_t now = k_uptime_get();

        if (now >= next_read) {
            dash_read_t r = read_snapshot(&snap, &heartbeat);
            dash_link_state_t state =
                dash_link_update(&link, r == DASH_READ_OK, heartbeat, (uint32_t)now);

            next_read = now + READ_PERIOD_MS;

            if (r == DASH_READ_LAYOUT_MISMATCH) {
                /* The two images were built from different layouts: never draw their data. */
                ui_set_alert(true, "REFLASH\nBOTH CORES");
            } else if (state == DASH_LINK_UP) {
                if (r == DASH_READ_OK) { /* only a good read changes the screen */
                    if (!l_boot_data_logged) {
                        l_boot_data_logged = true;
                        LOG_INF("boot: first data from the M4 shown at %u ms",
                                (unsigned)k_uptime_get_32());
                    }
                    show_snapshot(&snap);
                }
                /* A failed read keeps the last good screen until the link times out. */
            } else if (state == DASH_LINK_LOST) {
                /* Never show stale numbers as live. */
                ui_set_alert(true, "NO DATA\nCAN CORE LOST");
            }
            /* DASH_LINK_WAITING: the bars are empty and the boxes were never filled. */
        }

        lv_timer_handler();
        if (!l_boot_frame_logged) {
            l_boot_frame_logged = true;
            LOG_INF("boot: first frame drawn at %u ms", (unsigned)k_uptime_get_32());
        }
        k_msleep(UI_PERIOD_MS);
    }
    return 0;
}
