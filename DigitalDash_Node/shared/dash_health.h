/*******************************************************
 * @file dash_health.h
 * @brief Alive supervision for a core's critical tasks (modeled on AUTOSAR Watchdog Manager, the
 * same design as the Ethanol Node's ethanol_health). Hardware independent.
 *
 * Each supervised task calls dash_health_checkin() every time it completes a cycle. A monitor calls
 * dash_health_evaluate() once per monitor period and feeds the hardware watchdog only while
 * dash_health_should_feed() is true. Once supervision has failed the feed stops for good and the
 * hardware timeout resets the chip (AUTOSAR WdgM 7.3.3 behavior). A task that stalls is a failure,
 * and so is one that runs away.
 *******************************************************/
#ifndef DASH_HEALTH_H
#define DASH_HEALTH_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*******************************************************
 * Macros
 *******************************************************/
#define DASH_HEALTH_MAX_ENTITIES 4u

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef struct {
    uint8_t count; /* entities in use, at most DASH_HEALTH_MAX_ENTITIES */
    uint8_t min_per_period[DASH_HEALTH_MAX_ENTITIES]; /* expected check-ins per monitor period, at
                                                         least */
    uint8_t max_per_period[DASH_HEALTH_MAX_ENTITIES]; /* ... and at most (a runaway loop is a
                                                         failure too) */
    uint8_t fail_cycles; /* consecutive failed periods before the feed stops */
} dash_health_cfg_t;

typedef struct {
    dash_health_cfg_t cfg;
    volatile uint32_t checkins[DASH_HEALTH_MAX_ENTITIES]; /* each written by its own task only */
    uint32_t last_seen[DASH_HEALTH_MAX_ENTITIES];
    uint8_t failed_cycles;
    bool stopped;
} dash_health_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* A bad configuration (no entities, too many, a window that cannot be met, no failure limit) starts
 * already stopped: a monitor that cannot supervise must not keep the watchdog fed. */
void dash_health_init(dash_health_t *h, const dash_health_cfg_t *cfg);

/* Called by a task each time it finishes a cycle. An unknown entity is ignored. */
void dash_health_checkin(dash_health_t *h, uint8_t entity);

/* Called once per monitor period. Returns true if every entity checked in the expected number of
 * times. After cfg.fail_cycles consecutive failures it latches stopped. */
bool dash_health_evaluate(dash_health_t *h);

/* True while the hardware watchdog may be fed. Never becomes true again after a stop. */
bool dash_health_should_feed(const dash_health_t *h);

#endif /* DASH_HEALTH_H */
