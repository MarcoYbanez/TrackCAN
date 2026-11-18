/*******************************************************
 * @file trackcan_core.h
 * @brief Hardware-independent TrackCAN messaging core shared by every Node.
 *
 * A Node supplies a tcan_port_t (send / receive / now_ms) for its own CAN driver. The core
 * then tracks received messages, flags stale data, schedules cyclic transmission and
 * converts display units. It makes no OS or vendor SDK calls and never blocks.
 *******************************************************/
#ifndef TRACKCAN_CORE_H
#define TRACKCAN_CORE_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "trackcan.h"

/*******************************************************
 * Macros
 *******************************************************/
#define TCAN_MAX_DLC 8u
#define TCAN_STALE_CYCLES 3u
#define TCAN_POLL_MAX_FRAMES 32u

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef enum {
    TCAN_NODE_DASH = 0,
    TCAN_NODE_DATALOGGER,
    TCAN_NODE_ETHANOL,
} tcan_node_t;

/* Order matches can/trackcan.dbc. Add new messages before TCAN_MSG_COUNT. */
typedef enum {
    TCAN_MSG_DASH_ENGINE = 0,
    TCAN_MSG_DATALOGGER_ANALOG,
    TCAN_MSG_DATALOGGER_LIGHTS,
    TCAN_MSG_ETHANOL_DATA,
    TCAN_MSG_HEARTBEAT_DASH,
    TCAN_MSG_HEARTBEAT_DATALOGGER,
    TCAN_MSG_HEARTBEAT_ETHANOL,
    TCAN_MSG_COUNT,
} tcan_msg_t;

typedef struct {
    uint32_t id; /* 11-bit standard identifier */
    uint8_t dlc;
    uint8_t data[TCAN_MAX_DLC];
} tcan_frame_t;

/*
 * Port interface. Each Node implements these for its own CAN controller.
 *  - send:    queue one frame without blocking. Return 0 on success, negative if dropped.
 *  - receive: fetch one pending frame without blocking. Return 1 if a frame was written,
 *             0 if none is pending.
 *  - now_ms:  free-running millisecond counter. Wrap-around at 2^32 is handled.
 */
typedef struct {
    int (*send)(const tcan_frame_t *frame);
    int (*receive)(tcan_frame_t *frame);
    uint32_t (*now_ms)(void);
} tcan_port_t;

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint16_t cycle_ms;
    tcan_node_t sender;
} tcan_msg_info_t;

typedef enum {
    TCAN_RX_NEVER = 0, /* nothing received yet */
    TCAN_RX_FRESH,
    TCAN_RX_STALE, /* nothing for more than TCAN_STALE_CYCLES cycles */
} tcan_rx_state_t;

typedef struct {
    const tcan_port_t *port;
    tcan_node_t node;

    uint8_t rx_data[TCAN_MSG_COUNT][TCAN_MAX_DLC];
    uint32_t rx_ms[TCAN_MSG_COUNT];
    bool rx_seen[TCAN_MSG_COUNT];

    uint32_t tx_ms[TCAN_MSG_COUNT];
    bool tx_started[TCAN_MSG_COUNT];

    uint32_t rx_frames;     /* accepted frames */
    uint32_t rx_bad_frames; /* known ID with wrong length */
    uint32_t tx_dropped;    /* port->send failed */
} tcan_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* Returns NULL if msg is out of range. */
const tcan_msg_info_t *tcan_msg_info(tcan_msg_t msg);

/* Returns true and sets *msg if id belongs to a known message. */
bool tcan_msg_from_id(uint32_t id, tcan_msg_t *msg);

/* Returns false if any argument is NULL or the port is incomplete. */
bool tcan_init(tcan_t *can, const tcan_port_t *port, tcan_node_t node);

/* Drains up to TCAN_POLL_MAX_FRAMES pending frames. Unknown IDs are ignored. */
void tcan_poll(tcan_t *can);

/* Copies the latest payload into data (TCAN_MAX_DLC bytes) when one has been received. */
tcan_rx_state_t tcan_rx_get(const tcan_t *can, tcan_msg_t msg, uint8_t data[TCAN_MAX_DLC]);

/* Wrap-safe staleness test: true when more than TCAN_STALE_CYCLES cycles have elapsed. */
bool tcan_is_stale(uint32_t now_ms, uint32_t last_ms, uint16_t cycle_ms);

/* True once per cycle for a message this Node owns. Call from the Node's periodic loop. */
bool tcan_tx_due(tcan_t *can, tcan_msg_t msg);

/*
 * Sends one message (length taken from the message table). Never blocks.
 * Returns 0 on success, -1 on bad arguments, -2 if this Node does not own msg,
 * -3 if the port dropped the frame.
 */
int tcan_tx(tcan_t *can, tcan_msg_t msg, const uint8_t *data);

/* Display conversions. The bus and the SD log stay in bar and degC. */
uint32_t tcan_bar_x100_to_psi_x10(uint16_t bar_x100);
int32_t tcan_degc_x10_to_degf_x10(int16_t degc_x10);

#endif /* TRACKCAN_CORE_H */
