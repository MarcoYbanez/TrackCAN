/*******************************************************
 * @file dash_watchdog.h
 * @brief Hardware watchdog glue shared by the M4 and M7 apps (Zephyr).
 *
 * A monitor thread runs dash_health_evaluate() once per period and feeds the hardware watchdog only
 * while every supervised task is checking in as expected. When a task stalls or runs away the feed
 * stops for good and the watchdog resets the chip. The monitor thread has the highest application
 * priority, so a busy main loop cannot starve it into a false reset; if the monitor itself dies,
 * nothing feeds and the chip resets too.
 *******************************************************/
#ifndef DASH_WATCHDOG_H
#define DASH_WATCHDOG_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#include "dash_health.h"

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/*
 * Installs and starts the watchdog (reset after timeout_ms without a feed) and the monitor thread
 * (evaluates every period_ms). timeout_ms must be longer than period_ms plus scheduling jitter.
 * options are passed to wdt_setup(). Returns 0, or a negative errno if the watchdog could not be
 * started: the caller must treat that as a fault.
 */
int dash_watchdog_start(const struct device *wdt, const dash_health_cfg_t *cfg, uint32_t period_ms,
                        uint32_t timeout_ms, uint8_t options);

/* Called by a supervised task each time it completes a cycle. */
void dash_watchdog_checkin(uint8_t entity);

/* True once supervision has failed and the feed has stopped (a reset is about to happen). */
bool dash_watchdog_stopped(void);

#endif /* DASH_WATCHDOG_H */
