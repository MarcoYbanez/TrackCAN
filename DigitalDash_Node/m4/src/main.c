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

    for (;;) {
        dash_snapshot_t snap;

        memset(&snap, 0, sizeof(snap));
        /* M1 TEST VALUES, not real data. Everything sweeps so one flash checks the whole screen:
         * the RPM bars (and the shift lights flashing near 8,500), a 3 digit speed, every gauge box
         * across its red limit, and the lamps switching on and off. Each cycle takes a few seconds.
         */
        snap.rpm = (uint16_t)((tick * 45u) % 9500u);
        snap.speed_kmh_x100 = (uint16_t)((tick * 11u) % 20000u);

        snap.display.analog_state = TCAN_RX_FRESH;
        snap.display.oil_pressure_psi_x10 = (tick * 3u) % 800u; /* 0 to 80 psi, red below 15 */
        snap.display.oil_temp_degf_x10 =
            (int32_t)(2000u + (tick * 4u) % 500u); /* 200 to 250 F, red at 235 */
        snap.display.coolant_temp_degf_x10 =
            (int32_t)(1800u + (tick * 5u) % 600u); /* 180 to 240 F, red at 220 */
        snap.display.fuel_level_pct_x10 = (uint16_t)(1000u - (tick * 2u) % 1000u);

        snap.display.ethanol_state = TCAN_RX_FRESH;
        snap.display.ethanol_pct = 85;
        snap.display.ethanol_alert = false;

        snap.display.lights_state = TCAN_RX_FRESH;
        {
            uint32_t phase = (tick / 75u) & 0xFu; /* a new combination every 1.5 s */

            snap.display.lights =
                (uint16_t)(((phase & 1u) ? 0x0001u : 0u) | /* check engine */
                           ((phase & 2u) ? 0x0002u : 0u) | /* oil warning */
                           ((phase & 4u) ? 0x0020u : 0u) | /* low fuel: fuel box red */
                           ((phase & 8u) ? 0x0100u : 0u)); /* charge warning */
        }

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
