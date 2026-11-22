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
        /* M1 TEST VALUES, not real data. */
        snap.rpm = (uint16_t)(1000u + (tick * 37u) % 6000u);
        snap.speed_kmh_x100 = (uint16_t)((tick * 11u) % 20000u);
        snap.display.ethanol_pct = 85;
        snap.display.ethanol_alert = false;

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
