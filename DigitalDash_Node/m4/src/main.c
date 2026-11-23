/*******************************************************
 * @file main.c
 * @brief Dash, M4 core: CAN logic and the snapshot writer.
 *
 * Three images, chosen when building (-DDASH_MODE, see CMakeLists.txt):
 *  - LIVE (the default): real FDCAN on the bus. Publishes only what the CAN logic received.
 * Anything without a source (speed, RPM, boost, battery, AFR, gear) stays invalid and the screen
 * shows "--". With nothing on the bus the ethanol alert comes on 2 s after start: the correct
 * fail-closed behavior.
 *  - LOOPBACK (bench, no wiring): the FDCAN controller runs in internal loopback and simulated
 * DataLogger and Ethanol Nodes send real TrackCAN frames through it, so the whole receive path is
 * exercised. The data is a simulation: DEMO flag set.
 *  - DEMO (bench): fake values straight into the snapshot, no CAN at all. DEMO flag set.
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

#if defined(DASH_MODE_DEMO) || defined(DASH_MODE_LOOPBACK)
#include "dash_sim.h"
#endif
#ifdef DASH_MODE_LOOPBACK
#include "dash_loopback_source.h"
#endif
#ifndef DASH_MODE_DEMO
#include "dash.h"
#endif
#include "dash_shared.h"
#include "dash_watchdog.h"
#include "dash_xcore.h"

/*******************************************************
 * Macros
 *******************************************************/
#define PUBLISH_PERIOD_MS 20
#define CAN_RETRY_MS 1000 /* a controller that could not start is tried again every second */

/* Watchdog: the monitor looks every 100 ms. The loop runs every 20 ms, so about 5 check-ins are
 * expected; 2 to 9 allows jitter but not a stall or a runaway. Two failed periods stop the feed
 * (200 ms) and the 300 ms hardware timeout then resets the chip. */
#define WDG_PERIOD_MS 100
#define WDG_TIMEOUT_MS 300
#define ENTITY_PUBLISH 0

/*******************************************************
 * Static Function Definitions
 *******************************************************/
#ifndef DASH_MODE_DEMO
/* Fills the CAN diagnostics of the snapshot from the controller and the CAN logic. */
static void fill_can_diagnostics(dash_snapshot_t *snap)
{
    struct dash_can_hw_status hw;
    struct dash_can_stats st;

    dash_port_stm32h7_status(&hw);
    dash_can_get_stats(&st);
    snap->can_state = hw.state;
    snap->can_tx_err = hw.tx_err;
    snap->can_rx_err = hw.rx_err;
    snap->can_init_step = hw.init_step;
    snap->can_init_err = hw.init_err;
    snap->can_core_clock_hz = hw.core_clock_hz;
    snap->can_prescaler = hw.prescaler;
    snap->can_tq_per_bit = hw.tq_per_bit;
    snap->can_sample_point = hw.sample_point_permille;
    snap->can_rx_frames = (uint16_t)st.rx_frames;
    snap->can_tx_dropped = (uint16_t)st.tx_dropped;
    snap->extra_valid |= DASH_EXTRA_CAN;
}
#endif

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
#if defined(DASH_MODE_DEMO) || defined(DASH_MODE_LOOPBACK)
    dash_sim_t sim;
    dash_sim_out_t drive;
#endif
#ifdef DASH_MODE_LOOPBACK
    dash_loopback_source_t source;
#endif
#ifndef DASH_MODE_DEMO
    bool can_logic_ok;
    bool can_started = false;
    uint32_t last_start_try_ms = 0;
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

#if defined(DASH_MODE_DEMO) || defined(DASH_MODE_LOOPBACK)
    dash_sim_init(&sim);
#endif
#ifndef DASH_MODE_DEMO
    can_logic_ok = dash_can_init(dash_port_stm32h7());
#ifdef DASH_MODE_LOOPBACK
    can_logic_ok = can_logic_ok && dash_loopback_source_init(&source, dash_port_stm32h7());
#endif
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

#if defined(DASH_MODE_DEMO)
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
        snap.display.oil_pressure_state = TCAN_RX_FRESH;
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
        /* LIVE and LOOPBACK: the CAN logic decodes the frames the controller received. A controller
         * that could not start is retried every second, and until it runs the screen shows no data.
         */
        if (!can_started &&
            (k_uptime_get_32() - last_start_try_ms >= CAN_RETRY_MS || last_start_try_ms == 0)) {
            last_start_try_ms =
                k_uptime_get_32() | 1u; /* never 0, so the first try is not repeated */
#ifdef DASH_MODE_LOOPBACK
            can_started = dash_port_stm32h7_start(true);
#else
            can_started = dash_port_stm32h7_start(false);
#endif
        }

        if (can_logic_ok) {
#ifdef DASH_MODE_LOOPBACK
            dash_sim_step(&sim, PUBLISH_PERIOD_MS, &drive);
            dash_loopback_source_step(&source, &drive);
            dash_can_set_engine(drive.rpm, drive.speed_kmh_x100);
#endif
            dash_can_step();
            dash_can_get_display(&snap.display);
        } else {
            snap.display.ethanol_alert =
                true; /* the CAN logic did not start: never present that as quiet */
        }
        fill_can_diagnostics(&snap);

#ifdef DASH_MODE_LOOPBACK
        /* Simulated data: say so, and show what the drive says for the fields the bus does not
         * carry. */
        snap.flags = DASH_FLAG_DEMO;
        snap.rpm = drive.rpm;
        snap.speed_kmh_x100 = drive.speed_kmh_x100;
        snap.gear = drive.gear;
        snap.boost_psi_x10 = drive.boost_psi_x10;
        snap.battery_volts_x10 = drive.battery_volts_x10;
        snap.afr_x10 = drive.afr_x10;
        snap.extra_valid |= DASH_EXTRA_ENGINE | DASH_EXTRA_BOOST | DASH_EXTRA_BATTERY |
                            DASH_EXTRA_AFR | DASH_EXTRA_GEAR;
#endif
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
