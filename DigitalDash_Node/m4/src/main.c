/*******************************************************
 * @file main.c
 * @brief Dash, M4 core: CAN logic and the snapshot writer.
 *
 * M1: publishes TEST VALUES into the shared block so the M7 display path can be proven. M2 replaces
 * them with real CAN data from dash_can_get_display().
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/kernel.h>

#include "dash_shared.h"
#include "dash_sim.h"
#include "dash_xcore.h"

/*******************************************************
 * Macros
 *******************************************************/
#define PUBLISH_PERIOD_MS 20

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    const struct device *const ipm = DEVICE_DT_GET(DT_NODELABEL(mailbox));
    dash_shared_t *const shared = dash_xcore_shared();
    uint32_t tick = 0;
    dash_sim_t sim;
    dash_sim_out_t drive;

    dash_xcore_clock_on();
    dash_sim_init(&sim);

    /* The M7 may already be running: only touch the block while holding the semaphore. */
    while (!dash_xcore_lock()) {
        k_msleep(1);
    }
    dash_shared_init(shared);
    dash_xcore_unlock();

    if (!device_is_ready(ipm)) {
        printk("mailbox not ready\n");
    }

    for (;;) {
        dash_snapshot_t snap;

        memset(&snap, 0, sizeof(snap));
        /* DEMO DRIVE, not real data (dash_sim): gears, speed, boost, temperatures, lamps and
         * faults. The real CAN values replace this in M2. Boost, battery, AFR and gear have no CAN
         * source yet, so only the demo sets them. */
        dash_sim_step(&sim, PUBLISH_PERIOD_MS, &drive);
        snap.rpm = drive.rpm;
        snap.speed_kmh_x100 = drive.speed_kmh_x100;
        snap.gear = drive.gear;
        snap.boost_psi_x10 = drive.boost_psi_x10;
        snap.battery_volts_x10 = drive.battery_volts_x10;
        snap.afr_x10 = drive.afr_x10;
        snap.extra_valid = DASH_EXTRA_BOOST | DASH_EXTRA_BATTERY | DASH_EXTRA_AFR | DASH_EXTRA_GEAR;

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

        if (dash_xcore_lock()) {
            dash_shared_write(shared, &snap);
            dash_xcore_unlock();
            if (device_is_ready(ipm)) {
                (void)ipm_send(ipm, 0, 0, NULL, 0); /* "new snapshot", no payload */
            }
        }
        /* If the lock was not available this cycle the heartbeat simply does not advance: the M7
         * treats a stalled heartbeat as a lost link. */

        tick++;
        k_msleep(PUBLISH_PERIOD_MS);
    }
    return 0;
}
