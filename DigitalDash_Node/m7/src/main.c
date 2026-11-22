/*******************************************************
 * @file main.c
 * @brief Dash, M7 core: the display. Reads the snapshot the M4 publishes and draws it.
 *
 * Fail closed: if the M4 is missing or its heartbeat stops, the screen shows a big alert. It never
 * keeps showing old numbers as if they were live.
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

#include "dash_shared.h"
#include "dash_xcore.h"

LOG_MODULE_REGISTER(dash_m7, LOG_LEVEL_INF);

/*******************************************************
 * Macros
 *******************************************************/
#define UI_PERIOD_MS 20
#define READ_PERIOD_MS 50

/*******************************************************
 * Static Variables
 *******************************************************/
static lv_obj_t *l_title;
static lv_obj_t *l_rpm;
static lv_obj_t *l_speed;
static lv_obj_t *l_ethanol;
static lv_obj_t *l_link;
static lv_obj_t *l_alert; /* full-screen alert, hidden until needed */

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

static void ui_build(void)
{
    lv_obj_t *scr = lv_screen_active();

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    l_title = lv_label_create(scr);
    lv_label_set_text(l_title, "TrackCAN dash  (M1 test)");
    lv_obj_set_style_text_color(l_title, lv_color_hex(0x808080), 0);
    lv_obj_align(l_title, LV_ALIGN_TOP_LEFT, 8, 6);

    l_rpm = lv_label_create(scr);
    lv_obj_set_style_text_font(l_rpm, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(l_rpm, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(l_rpm, LV_ALIGN_TOP_LEFT, 8, 40);

    l_speed = lv_label_create(scr);
    lv_obj_set_style_text_font(l_speed, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(l_speed, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(l_speed, LV_ALIGN_TOP_LEFT, 8, 110);

    l_ethanol = lv_label_create(scr);
    lv_obj_set_style_text_font(l_ethanol, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(l_ethanol, lv_color_hex(0x40C0FF), 0);
    lv_obj_align(l_ethanol, LV_ALIGN_TOP_LEFT, 8, 160);

    l_link = lv_label_create(scr);
    lv_obj_align(l_link, LV_ALIGN_BOTTOM_LEFT, 8, -6);

    /* The alert covers everything. */
    l_alert = lv_label_create(scr);
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

    ui_build();
    display_blanking_off(display);
    dash_link_init(&link, k_uptime_get_32());

    for (;;) {
        int64_t now = k_uptime_get();

        if (now >= next_read) {
            dash_read_t r = read_snapshot(&snap, &heartbeat);
            dash_link_state_t state =
                dash_link_update(&link, r == DASH_READ_OK, heartbeat, (uint32_t)now);

            next_read = now + READ_PERIOD_MS;

            if (r == DASH_READ_LAYOUT_MISMATCH) {
                /* The two images were built from different layouts: never draw their data. */
                lv_label_set_text(l_link, "firmware mismatch");
                lv_obj_set_style_text_color(l_link, lv_color_hex(0xFF4040), 0);
                ui_set_alert(true, "REFLASH\nBOTH CORES");
            } else if (state == DASH_LINK_UP) {
                if (r == DASH_READ_OK) { /* only a good read changes the screen */
                    lv_label_set_text_fmt(l_rpm, "%u rpm", (unsigned)snap.rpm);
                    lv_label_set_text_fmt(l_speed, "%u.%02u km/h",
                                          (unsigned)(snap.speed_kmh_x100 / 100u),
                                          (unsigned)(snap.speed_kmh_x100 % 100u));
                    lv_label_set_text_fmt(l_ethanol, "ethanol %u %%",
                                          (unsigned)snap.display.ethanol_pct);
                    lv_label_set_text_fmt(l_link, "M4 link up  hb %u  sig %u", (unsigned)heartbeat,
                                          (unsigned)l_signals);
                    lv_obj_set_style_text_color(l_link, lv_color_hex(0x40FF40), 0);
                    ui_set_alert(snap.display.ethanol_alert, "CHECK\nETHANOL");
                }
                /* A failed read keeps the last good screen until the link times out. */
            } else if (state == DASH_LINK_WAITING) {
                lv_label_set_text(l_link, "waiting for M4 ...");
                lv_obj_set_style_text_color(l_link, lv_color_hex(0xFFC000), 0);
                ui_set_alert(false, "");
            } else {
                /* Lost: never show stale numbers as live. */
                lv_label_set_text(l_link, "M4 link LOST");
                lv_obj_set_style_text_color(l_link, lv_color_hex(0xFF4040), 0);
                ui_set_alert(true, "NO DATA\nCAN CORE LOST");
            }
        }

        lv_timer_handler();
        k_msleep(UI_PERIOD_MS);
    }
    return 0;
}
