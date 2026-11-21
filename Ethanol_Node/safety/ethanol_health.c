/*******************************************************
 * @file ethanol_health.c
 * @brief Alive supervision for the critical tasks (modeled on AUTOSAR Watchdog Manager).
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "ethanol_health.h"

#include <string.h>

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void eth_health_init(eth_health_t *h)
{
    memset(h, 0, sizeof(*h));

    /* Monitor period 200 ms. The reader runs every 50 ms (expect about 4), the DAC task every
     * 100 ms (expect about 2). The window allows scheduling jitter but not a stall or a runaway. */
    h->min_per_cycle[ETH_ENTITY_READER] = ETH_HEALTH_PERIOD_MS / ETH_READER_PERIOD_MS - 2u;
    h->max_per_cycle[ETH_ENTITY_READER] = ETH_HEALTH_PERIOD_MS / ETH_READER_PERIOD_MS + 2u;
    h->min_per_cycle[ETH_ENTITY_DAC] = ETH_HEALTH_PERIOD_MS / ETH_DAC_PERIOD_MS - 1u;
    h->max_per_cycle[ETH_ENTITY_DAC] = ETH_HEALTH_PERIOD_MS / ETH_DAC_PERIOD_MS + 2u;
}

void eth_health_checkin(eth_health_t *h, eth_entity_t e)
{
    if ((unsigned)e < ETH_ENTITY_COUNT) {
        h->checkins[e]++;
    }
}

bool eth_health_evaluate(eth_health_t *h)
{
    bool all_ok = true;

    for (unsigned e = 0; e < ETH_ENTITY_COUNT; e++) {
        uint32_t delta = (uint32_t)(h->checkins[e] - h->last_seen[e]);

        h->last_seen[e] = h->checkins[e];
        if (delta < h->min_per_cycle[e] || delta > h->max_per_cycle[e]) {
            all_ok = false;
        }
    }

    if (all_ok) {
        h->failed_cycles = 0;
    } else if (h->failed_cycles < 255u) {
        h->failed_cycles++;
    }
    if (h->failed_cycles >= ETH_HEALTH_FAIL_CYCLES) {
        h->stopped = true;
    }
    return all_ok;
}

bool eth_health_should_feed(const eth_health_t *h)
{
    return !h->stopped;
}
