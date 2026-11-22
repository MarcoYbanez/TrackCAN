/*******************************************************
 * @file dash_shared.h
 * @brief Snapshot exchange between the Dash's two cores: the M4 (CAN logic) writes, the M7
 *        (display) reads.
 *
 * Hardware independent. The memory is SRAM4, which both cores can reach. The M4 owns the
 * writes, the M7 only reads. Two protections, because a torn or frozen snapshot must never
 * reach the driver:
 *  - a sequence counter (odd while the writer is busy) so a reader never uses a half-written
 *    snapshot;
 *  - a heartbeat counter that increases on every completed write, so the reader can tell that
 *    the M4 has stopped. Frozen data is treated as a lost link, never as valid.
 * The apps additionally hold a hardware semaphore (HSEM) around the copy, and the M4 signals the
 * M7 through the HSEM mailbox after each write.
 *******************************************************/
#ifndef DASH_SHARED_H
#define DASH_SHARED_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "dash.h"

/*******************************************************
 * Macros
 *******************************************************/
#define DASH_SHARED_ADDR 0x38000000u  /* SRAM4 (D3 domain), reachable by both cores */
#define DASH_SHARED_MAGIC 0x44534831u /* "DSH1" */
#define DASH_LINK_TIMEOUT_MS 250u     /* no heartbeat change for this long: the M4 is gone */
#define DASH_BOOT_GRACE_MS 2000u      /* how long the M7 waits for the M4 at start-up */
#define DASH_READ_RETRIES 4u
/* Layout tag: bump the version whenever the snapshot or its meaning changes. The size is part of
 * the tag, so reflashing only one core after a struct change is refused instead of misread. */
#define DASH_LAYOUT_VERSION                                                                        \
    5u /* 5: CAN diagnostics; 4: flags and engine-valid; 3: boost, battery, AFR, gear */
#define DASH_LAYOUT_TAG ((DASH_LAYOUT_VERSION << 16) | (uint32_t)sizeof(dash_snapshot_t))

/* Fields with no CAN source yet (TCAN-4). The real M4 leaves them unset, so the screen shows "--".
 * Only the demo drive (dash_sim) sets them. */
#define DASH_EXTRA_BOOST 0x01u
#define DASH_EXTRA_BATTERY 0x02u
#define DASH_EXTRA_AFR 0x04u
#define DASH_EXTRA_GEAR 0x08u
#define DASH_EXTRA_CAN                                                                             \
    0x20u /* the CAN diagnostics below are real: the CAN controller is running                     \
           */
#define DASH_EXTRA_ENGINE                                                                          \
    0x10u /* rpm and speed come from a real measurement (TCAN-7); without it they show "--" */

/* Mode flags, set by the M4 image. DEMO is a compile-time constant of the demo build, so a live
 * image cannot set it. */
#define DASH_FLAG_DEMO 0x01u

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef struct {
    struct dash_display display; /* everything the screen shows, in display units */
    uint16_t rpm;                /* 1 rpm per bit */
    uint16_t speed_kmh_x100;     /* 0.01 km/h per bit */
    int16_t boost_psi_x10; /* 0.1 psi per bit, negative is vacuum. Valid only if DASH_EXTRA_BOOST is
                              set */
    uint16_t battery_volts_x10; /* 0.1 V per bit. Valid only if DASH_EXTRA_BATTERY is set */
    uint16_t afr_x10;           /* 0.1 per bit. Valid only if DASH_EXTRA_AFR is set */
    uint8_t gear;               /* 0 neutral, 1 to 5. Valid only if DASH_EXTRA_GEAR is set */
    uint8_t extra_valid;        /* DASH_EXTRA_* bits: a field is shown only if its bit is set */
    uint8_t flags;              /* DASH_FLAG_* bits */
    uint8_t can_state;          /* enum dash_can_hw_state. Valid only if DASH_EXTRA_CAN is set */
    uint8_t can_tx_err;         /* controller transmit error counter */
    uint8_t can_rx_err;         /* controller receive error counter */
    uint8_t can_init_step;      /* enum dash_can_init_step of a failed controller start */
    int16_t can_init_err;       /* its negative errno */
    uint32_t can_core_clock_hz; /* the FDCAN kernel clock the driver computes with */
    uint16_t can_prescaler;
    uint16_t can_tq_per_bit;
    uint16_t can_sample_point; /* per mille, 875 = 87.5 % */
    uint16_t can_rx_frames;    /* frames accepted by the CAN logic (wraps) */
    uint16_t can_tx_dropped;   /* frames the port could not send (wraps) */
} dash_snapshot_t;

typedef struct {
    uint32_t magic;              /* set by the FIRST completed write, never by init */
    uint32_t layout;             /* DASH_LAYOUT_TAG of the writer */
    volatile uint32_t seq;       /* odd while the writer is updating */
    volatile uint32_t heartbeat; /* +1 per completed write */
    dash_snapshot_t snap;
} dash_shared_t;

typedef enum {
    DASH_READ_OK = 0,
    DASH_READ_BUSY,            /* the writer was mid-update on every attempt */
    DASH_READ_BAD_MAGIC,       /* nothing has been written yet */
    DASH_READ_LAYOUT_MISMATCH, /* the two cores were built from different snapshot layouts */
} dash_read_t;

typedef enum {
    DASH_LINK_WAITING =
        0, /* the M4 has not been seen to write yet, still inside the start-up grace */
    DASH_LINK_UP,
    DASH_LINK_LOST, /* never seen after the grace period, or the heartbeat stopped */
} dash_link_state_t;

typedef struct {
    bool seen;      /* a good read has been made */
    bool confirmed; /* the heartbeat has CHANGED since then: the M4 is writing right now, not
                       leftover data */
    uint32_t last_heartbeat;
    uint32_t last_change_ms;
    uint32_t start_ms;
} dash_link_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* Writer side (M4). Clears the block. The magic stays 0, so an unwritten block is never valid. */
void dash_shared_init(dash_shared_t *sh);

/* Writer side (M4). Odd seq, copy, even seq, then layout, magic and heartbeat. */
void dash_shared_write(dash_shared_t *sh, const dash_snapshot_t *snap);

/* Reader side (M7). Copies a consistent snapshot, or says why not. `out` and *heartbeat are changed
 * ONLY when the result is DASH_READ_OK. */
dash_read_t dash_shared_read(const dash_shared_t *sh, dash_snapshot_t *out, uint32_t *heartbeat);

/* Reader side (M7). Tracks whether the M4 is alive. Wrap-safe in time and heartbeat. */
void dash_link_init(dash_link_t *l, uint32_t now_ms);
dash_link_state_t dash_link_update(dash_link_t *l, bool read_ok, uint32_t heartbeat,
                                   uint32_t now_ms);

#endif /* DASH_SHARED_H */
