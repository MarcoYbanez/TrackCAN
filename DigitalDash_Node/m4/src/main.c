/*******************************************************
 * @file main.c
 * @brief Dash, M4 core: CAN logic and the snapshot writer.
 *
 * Two images, chosen when building (see CMakeLists.txt):
 *  - DEMO (-DDASH_DEMO=ON, the default for now): publishes a demo drive (dash_sim) and sets
 * DASH_FLAG_DEMO, so the M7 draws a DEMO banner and frame on every frame. Bench only.
 *  - LIVE (-DDASH_DEMO=OFF): publishes only what the CAN logic received. Anything without a source
 * stays invalid and the screen shows "--". Until the FDCAN port exists (TCAN-6) nothing is
 * received, so the ethanol alert fires 2 s after start: the correct fail-closed behavior.
 *
 * The publish loop is supervised: if it stalls or runs away, the M4's own watchdog (IWDG2) resets
 * the chip.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>

#ifdef DASH_DEMO
#include "dash_sim.h"
#else
#include "dash.h"
#endif
#include "dash_shared.h"
#include "dash_watchdog.h"
#include "dash_xcore.h"

/*******************************************************
 * Macros
 *******************************************************/
#define PUBLISH_PERIOD_MS 20

/* Watchdog: the monitor looks every 100 ms. The loop runs every 20 ms, so about 5 check-ins are
 * expected; 2 to 9 allows jitter but not a stall or a runaway. Two failed periods stop the feed
 * (200 ms) and the 300 ms hardware timeout then resets the chip. */
#define WDG_PERIOD_MS 100
#define WDG_TIMEOUT_MS 300
#define ENTITY_PUBLISH 0

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    const struct device *const ipm = DEVICE_DT_GET(DT_NODELABEL(mailbox));
    const struct device *const wdt = DEVICE_DT_GET(DT_NODELABEL(iwdg2));
    dash_shared_t *const shared = dash_xcore_shared();
    const dash_health_cfg_t health = {
        .count = 1, .min_per_period = {2}, .max_per_period = {9}, .fail_cycles = 2};
#ifdef DASH_DEMO
    dash_sim_t sim;
    dash_sim_out_t drive;
#else
    bool can_ok;
#endif

    dash_xcore_clock_on();

    /* The M7 may already be running: only touch the block while holding the semaphore. */
    while (!dash_xcore_lock()) {
        k_msleep(1);
    }
    dash_shared_init(shared);
    dash_xcore_unlock();

    if (!device_is_ready(ipm)) {
        printk("mailbox not ready\n");
    }

#ifdef DASH_DEMO
    dash_sim_init(&sim);
#else
    can_ok = dash_can_init(dash_port_stm32h7());
#endif

    if (dash_watchdog_start(wdt, &health, WDG_PERIOD_MS, WDG_TIMEOUT_MS, 0) < 0) {
        /* No watchdog: do not run unsupervised. The M7 sees no heartbeat and shows the lost-link
         * alert. */
        printk("watchdog failed to start\n");
        for (;;) {
            k_msleep(1000);
        }
    }

    for (;;) {
        dash_snapshot_t snap;

        memset(&snap, 0, sizeof(snap));
#ifdef DASH_DEMO
        /* DEMO DRIVE, not real data (dash_sim): gears, speed, boost, temperatures, lamps and
         * faults. Boost, battery, AFR and gear have no CAN source yet, so only the demo sets them.
         */
        dash_sim_step(&sim, PUBLISH_PERIOD_MS, &drive);
        snap.flags = DASH_FLAG_DEMO;
        snap.rpm = drive.rpm;
        snap.speed_kmh_x100 = drive.speed_kmh_x100;
        snap.gear = drive.gear;
        snap.boost_psi_x10 = drive.boost_psi_x10;
        snap.battery_volts_x10 = drive.battery_volts_x10;
        snap.afr_x10 = drive.afr_x10;
        snap.extra_valid = DASH_EXTRA_ENGINE | DASH_EXTRA_BOOST | DASH_EXTRA_BATTERY |
                           DASH_EXTRA_AFR | DASH_EXTRA_GEAR;

        snap.display.analog_state = TCAN_RX_FRESH;
        snap.display.oil_pressure_psi_x10 = drive.oil_pressure_psi_x10;
        snap.display.oil_temp_degf_x10 = drive.oil_temp_degf_x10;
        snap.display.coolant_temp_degf_x10 = drive.coolant_degf_x10;
        snap.display.fuel_level_pct_x10 = drive.fuel_pct_x10;
        snap.display.ethanol_state = TCAN_RX_FRESH;
        snap.display.ethanol_pct = 85;
        snap.display.ethanol_alert = false;
        snap.display.lights_state = TCAN_RX_FRESH;
        snap.display.lights = drive.lights;
#else
        /* LIVE: only what was received. Nothing has a source for rpm, speed, boost, battery, AFR or
         * gear yet, so extra_valid stays 0 and the M7 shows "--". */
        if (can_ok) {
            dash_can_step();
            dash_can_get_display(&snap.display);
        } else {
            snap.display.ethanol_alert =
                true; /* the CAN logic did not start: never present that as quiet */
        }
#endif

        if (dash_xcore_lock()) {
            dash_shared_write(shared, &snap);
            dash_xcore_unlock();
            if (device_is_ready(ipm)) {
                (void)ipm_send(ipm, 0, 0, NULL, 0); /* "new snapshot", no payload */
            }
        }
        /* If the lock was not available this cycle the heartbeat simply does not advance: the M7
         * treats a stalled heartbeat as a lost link. */

        dash_watchdog_checkin(ENTITY_PUBLISH);

#ifdef DASH_TEST_HANG_AFTER_MS
        if (k_uptime_get_32() > DASH_TEST_HANG_AFTER_MS) {
            for (;;) {
                /* TEST ONLY: a busy hang. The watchdog must reset the chip. */
            }
        }
#endif
        k_msleep(PUBLISH_PERIOD_MS);
    }
    return 0;
}
