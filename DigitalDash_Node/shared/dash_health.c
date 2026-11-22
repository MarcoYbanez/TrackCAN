/*******************************************************
 * @file dash_health.c
 * @brief Alive supervision for a core's critical tasks. See dash_health.h.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "dash_health.h"

#include <stddef.h>
#include <string.h>

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void dash_health_init(dash_health_t *h, const dash_health_cfg_t *cfg)
{
    bool ok = (cfg != NULL) && cfg->count >= 1u && cfg->count <= DASH_HEALTH_MAX_ENTITIES &&
              cfg->fail_cycles >= 1u;

    memset(h, 0, sizeof(*h));
    if (ok) {
        h->cfg = *cfg;
        for (unsigned e = 0; e < cfg->count; e++) {
            if (cfg->min_per_period[e] > cfg->max_per_period[e]) {
                ok = false; /* no count can satisfy this window */
            }
        }
    }
    if (!ok) {
        h->stopped = true;
    }
}

void dash_health_checkin(dash_health_t *h, uint8_t entity)
{
    if (entity < h->cfg.count) {
        h->checkins[entity]++;
    }
}

bool dash_health_evaluate(dash_health_t *h)
{
    bool all_ok = true;

    for (unsigned e = 0; e < h->cfg.count; e++) {
        uint32_t now = h->checkins[e];
        uint32_t delta = (uint32_t)(now - h->last_seen[e]);

        h->last_seen[e] = now;
        if (delta < h->cfg.min_per_period[e] || delta > h->cfg.max_per_period[e]) {
            all_ok = false;
        }
    }

    if (all_ok) {
        h->failed_cycles = 0;
    } else if (h->failed_cycles < 255u) {
        h->failed_cycles++;
    }
    if (h->stopped || h->failed_cycles >= h->cfg.fail_cycles) {
        h->stopped = true;
        return false; /* once stopped, a period is never reported healthy again */
    }
    return all_ok;
}

bool dash_health_should_feed(const dash_health_t *h)
{
    return !h->stopped;
}
