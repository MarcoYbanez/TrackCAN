/*******************************************************
 * @file ethanol_health.h
 * @brief Alive supervision for the critical tasks (modeled on AUTOSAR Watchdog Manager).
 *
 * Each supervised task calls eth_health_checkin() every time it completes a cycle. A monitor
 * calls eth_health_evaluate() once per monitor period and feeds the hardware watchdog only
 * while eth_health_should_feed() is true. Per AUTOSAR WdgM 7.3.3, once supervision has failed
 * the feed stops and the hardware timeout resets the chip.
 *******************************************************/
#ifndef ETHANOL_HEALTH_H
#define ETHANOL_HEALTH_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "ethanol_config.h"

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef enum {
    ETH_ENTITY_READER = 0,
    ETH_ENTITY_DAC,
    ETH_ENTITY_COUNT,
} eth_entity_t;

typedef struct {
    uint32_t checkins[ETH_ENTITY_COUNT];
    uint32_t last_seen[ETH_ENTITY_COUNT];
    uint8_t min_per_cycle[ETH_ENTITY_COUNT]; /* expected check-ins per monitor period, at least */
    uint8_t max_per_cycle[ETH_ENTITY_COUNT]; /* ... and at most (a runaway loop is a failure too) */
    uint8_t failed_cycles;
    bool stopped;
} eth_health_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/
void eth_health_init(eth_health_t *h);

/* Called by a task each time it finishes a cycle. */
void eth_health_checkin(eth_health_t *h, eth_entity_t e);

/* Called once per monitor period. Returns true if every entity checked in the expected number
 * of times. Fails closed: after ETH_HEALTH_FAIL_CYCLES consecutive failures it latches stopped. */
bool eth_health_evaluate(eth_health_t *h);

/* True while the hardware watchdog may be fed. Never becomes true again after a stop. */
bool eth_health_should_feed(const eth_health_t *h);

#endif /* ETHANOL_HEALTH_H */
