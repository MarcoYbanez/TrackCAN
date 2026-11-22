/*******************************************************
 * @file dash_shared.c
 * @brief Snapshot exchange between the Dash's two cores: the M4 (CAN logic) writes, the M7
 *        (display) reads.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "dash_shared.h"

#include <string.h>

/*******************************************************
 * Macros
 *******************************************************/
/* Orders the sequence counter against the data copy on both cores. */
#define DASH_FENCE() __atomic_thread_fence(__ATOMIC_SEQ_CST)

/* Test seam: the host tests use it to simulate the writer finishing in the middle of a read. It is
 * not defined in the firmware builds. */
#ifndef DASH_READ_MIDPOINT_HOOK
#define DASH_READ_MIDPOINT_HOOK(sh) ((void)0)
#else
void dash_test_midpoint(const dash_shared_t *sh); /* provided by the test program */
#endif

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void dash_shared_init(dash_shared_t *sh)
{
    memset(sh, 0, sizeof(*sh));
    DASH_FENCE();
}

void dash_shared_write(dash_shared_t *sh, const dash_snapshot_t *snap)
{
    sh->seq = sh->seq + 1u; /* odd: updating */
    DASH_FENCE();
    memcpy(&sh->snap, snap, sizeof(*snap));
    DASH_FENCE();
    sh->seq = sh->seq + 1u; /* even: complete */
    DASH_FENCE();
    sh->layout = DASH_LAYOUT_TAG;
    sh->magic = DASH_SHARED_MAGIC; /* only now is the block valid */
    DASH_FENCE();
    sh->heartbeat = sh->heartbeat + 1u;
    DASH_FENCE();
}

dash_read_t dash_shared_read(const dash_shared_t *sh, dash_snapshot_t *out, uint32_t *heartbeat)
{
    if (sh->magic != DASH_SHARED_MAGIC) {
        return DASH_READ_BAD_MAGIC;
    }
    if (sh->layout != DASH_LAYOUT_TAG) {
        return DASH_READ_LAYOUT_MISMATCH;
    }

    for (unsigned attempt = 0; attempt < DASH_READ_RETRIES; attempt++) {
        dash_snapshot_t local; /* a half-written copy must never reach the caller */
        uint32_t before = sh->seq;
        uint32_t hb;

        DASH_FENCE();
        if ((before & 1u) != 0u) {
            continue; /* the writer is mid-update */
        }
        memcpy(&local, &sh->snap, sizeof(local));
        hb = sh->heartbeat;
        DASH_READ_MIDPOINT_HOOK(sh);
        DASH_FENCE();
        if (sh->seq == before) {
            *out = local;
            *heartbeat = hb;
            return DASH_READ_OK;
        }
    }
    return DASH_READ_BUSY;
}

void dash_link_init(dash_link_t *l, uint32_t now_ms)
{
    memset(l, 0, sizeof(*l));
    l->start_ms = now_ms;
    l->last_change_ms = now_ms;
}

dash_link_state_t dash_link_update(dash_link_t *l, bool read_ok, uint32_t heartbeat,
                                   uint32_t now_ms)
{
    /* The first good read is only a sighting: the block may hold the previous run's last snapshot
     * (SRAM4 survives a reset). The link is UP only once the heartbeat has changed, which proves
     * the M4 is writing now. Leftover data has a frozen heartbeat, so it never counts as live and
     * ends as LOST after the start-up grace. */
    if (read_ok) {
        if (!l->seen) {
            l->seen = true;
            l->last_heartbeat = heartbeat;
            l->last_change_ms = now_ms;
        } else if (heartbeat != l->last_heartbeat) {
            l->confirmed = true;
            l->last_heartbeat = heartbeat;
            l->last_change_ms = now_ms;
        }
    }

    if (!l->confirmed) {
        return ((uint32_t)(now_ms - l->start_ms) > DASH_BOOT_GRACE_MS) ? DASH_LINK_LOST
                                                                       : DASH_LINK_WAITING;
    }
    return ((uint32_t)(now_ms - l->last_change_ms) > DASH_LINK_TIMEOUT_MS) ? DASH_LINK_LOST
                                                                           : DASH_LINK_UP;
}
