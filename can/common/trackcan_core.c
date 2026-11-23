/*******************************************************
 * @file trackcan_core.c
 * @brief Hardware-independent TrackCAN messaging core shared by every Node.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "trackcan_core.h"

#include <string.h>

/*******************************************************
 * Macros
 *******************************************************/
#define PSI_X10_PER_BAR_X100_NUM 145038u /* 1 bar = 14.5038 psi */
#define PSI_X10_PER_BAR_X100_DEN 100000u

/*******************************************************
 * Static Variables
 *******************************************************/
/* IDs, lengths and cycle times all come from can/trackcan.dbc via the generated header. */
static const tcan_msg_info_t l_msgs[TCAN_MSG_COUNT] = {
    [TCAN_MSG_DASH_ENGINE] = {TRACKCAN_DASH_ENGINE_FRAME_ID, TRACKCAN_DASH_ENGINE_LENGTH,
                              TRACKCAN_DASH_ENGINE_CYCLE_TIME_MS, TCAN_NODE_DASH},
    [TCAN_MSG_DATALOGGER_ANALOG] = {TRACKCAN_DATA_LOGGER_ANALOG_FRAME_ID,
                                    TRACKCAN_DATA_LOGGER_ANALOG_LENGTH,
                                    TRACKCAN_DATA_LOGGER_ANALOG_CYCLE_TIME_MS,
                                    TCAN_NODE_DATALOGGER},
    [TCAN_MSG_DATALOGGER_LIGHTS] = {TRACKCAN_DATA_LOGGER_LIGHTS_FRAME_ID,
                                    TRACKCAN_DATA_LOGGER_LIGHTS_LENGTH,
                                    TRACKCAN_DATA_LOGGER_LIGHTS_CYCLE_TIME_MS,
                                    TCAN_NODE_DATALOGGER},
    [TCAN_MSG_ETHANOL_DATA] = {TRACKCAN_ETHANOL_DATA_FRAME_ID, TRACKCAN_ETHANOL_DATA_LENGTH,
                               TRACKCAN_ETHANOL_DATA_CYCLE_TIME_MS, TCAN_NODE_ETHANOL},
    [TCAN_MSG_HEARTBEAT_DASH] = {TRACKCAN_HEARTBEAT_DASH_FRAME_ID, TRACKCAN_HEARTBEAT_DASH_LENGTH,
                                 TRACKCAN_HEARTBEAT_DASH_CYCLE_TIME_MS, TCAN_NODE_DASH},
    [TCAN_MSG_HEARTBEAT_DATALOGGER] = {TRACKCAN_HEARTBEAT_DATA_LOGGER_FRAME_ID,
                                       TRACKCAN_HEARTBEAT_DATA_LOGGER_LENGTH,
                                       TRACKCAN_HEARTBEAT_DATA_LOGGER_CYCLE_TIME_MS,
                                       TCAN_NODE_DATALOGGER},
    [TCAN_MSG_HEARTBEAT_ETHANOL] = {TRACKCAN_HEARTBEAT_ETHANOL_FRAME_ID,
                                    TRACKCAN_HEARTBEAT_ETHANOL_LENGTH,
                                    TRACKCAN_HEARTBEAT_ETHANOL_CYCLE_TIME_MS, TCAN_NODE_ETHANOL},
    [TCAN_MSG_DATALOGGER_STATUS] = {TRACKCAN_DATA_LOGGER_STATUS_FRAME_ID,
                                    TRACKCAN_DATA_LOGGER_STATUS_LENGTH,
                                    TRACKCAN_DATA_LOGGER_STATUS_CYCLE_TIME_MS,
                                    TCAN_NODE_DATALOGGER},
    [TCAN_MSG_LOGGER_CONTROL] = {TRACKCAN_LOGGER_CONTROL_FRAME_ID, TRACKCAN_LOGGER_CONTROL_LENGTH,
                                 TRACKCAN_LOGGER_CONTROL_CYCLE_TIME_MS, TCAN_NODE_CONTROLLER},
    [TCAN_MSG_DATALOGGER_OILPRESSURE] = {TRACKCAN_DATA_LOGGER_OIL_PRESSURE_FRAME_ID,
                                         TRACKCAN_DATA_LOGGER_OIL_PRESSURE_LENGTH,
                                         TRACKCAN_DATA_LOGGER_OIL_PRESSURE_CYCLE_TIME_MS,
                                         TCAN_NODE_DATALOGGER},
};

/*******************************************************
 * Public Function Definitions
 *******************************************************/
const tcan_msg_info_t *tcan_msg_info(tcan_msg_t msg)
{
    if ((unsigned)msg >= TCAN_MSG_COUNT) {
        return NULL;
    }
    return &l_msgs[msg];
}

bool tcan_msg_from_id(uint32_t id, tcan_msg_t *msg)
{
    for (unsigned i = 0; i < TCAN_MSG_COUNT; i++) {
        if (l_msgs[i].id == id) {
            if (msg != NULL) {
                *msg = (tcan_msg_t)i;
            }
            return true;
        }
    }
    return false;
}

bool tcan_init(tcan_t *can, const tcan_port_t *port, tcan_node_t node)
{
    if (can == NULL || port == NULL || port->send == NULL || port->receive == NULL ||
        port->now_ms == NULL) {
        return false;
    }
    memset(can, 0, sizeof(*can));
    can->port = port;
    can->node = node;
    return true;
}

void tcan_poll(tcan_t *can)
{
    tcan_frame_t frame;

    if (can == NULL) {
        return;
    }

    for (unsigned n = 0; n < TCAN_POLL_MAX_FRAMES; n++) {
        tcan_msg_t msg;

        if (can->port->receive(&frame) <= 0) {
            break;
        }
        if (!tcan_msg_from_id(frame.id, &msg)) {
            continue;
        }
        if (frame.dlc != l_msgs[msg].dlc) {
            can->rx_bad_frames++;
            continue;
        }
        memset(can->rx_data[msg], 0, TCAN_MAX_DLC);
        memcpy(can->rx_data[msg], frame.data, frame.dlc);
        can->rx_ms[msg] = can->port->now_ms();
        can->rx_seen[msg] = true;
        can->rx_frames++;
    }
}

tcan_rx_state_t tcan_rx_get(const tcan_t *can, tcan_msg_t msg, uint8_t data[TCAN_MAX_DLC])
{
    if (can == NULL || (unsigned)msg >= TCAN_MSG_COUNT || !can->rx_seen[msg]) {
        return TCAN_RX_NEVER;
    }
    if (data != NULL) {
        memcpy(data, can->rx_data[msg], TCAN_MAX_DLC);
    }
    return tcan_is_stale(can->port->now_ms(), can->rx_ms[msg], l_msgs[msg].cycle_ms)
               ? TCAN_RX_STALE
               : TCAN_RX_FRESH;
}

bool tcan_is_stale(uint32_t now_ms, uint32_t last_ms, uint16_t cycle_ms)
{
    return (uint32_t)(now_ms - last_ms) > (uint32_t)TCAN_STALE_CYCLES * cycle_ms;
}

bool tcan_tx_due(tcan_t *can, tcan_msg_t msg)
{
    uint32_t now;
    uint32_t elapsed;
    uint32_t cycle;

    if (can == NULL || (unsigned)msg >= TCAN_MSG_COUNT || l_msgs[msg].sender != can->node) {
        return false;
    }

    now = can->port->now_ms();
    cycle = l_msgs[msg].cycle_ms;

    if (!can->tx_started[msg]) {
        can->tx_started[msg] = true;
        can->tx_ms[msg] = now;
        return true;
    }

    elapsed = (uint32_t)(now - can->tx_ms[msg]);
    if (elapsed < cycle) {
        return false;
    }
    /* Keep the phase when the caller is a little late, resync when it is very late. */
    can->tx_ms[msg] = (elapsed < 2u * cycle) ? can->tx_ms[msg] + cycle : now;
    return true;
}

int tcan_tx(tcan_t *can, tcan_msg_t msg, const uint8_t *data)
{
    tcan_frame_t frame;

    if (can == NULL || data == NULL || (unsigned)msg >= TCAN_MSG_COUNT) {
        return -1;
    }
    if (l_msgs[msg].sender != can->node) {
        return -2;
    }

    memset(&frame, 0, sizeof(frame));
    frame.id = l_msgs[msg].id;
    frame.dlc = l_msgs[msg].dlc;
    memcpy(frame.data, data, frame.dlc);

    if (can->port->send(&frame) < 0) {
        can->tx_dropped++;
        return -3;
    }
    return 0;
}

uint32_t tcan_bar_x100_to_psi_x10(uint16_t bar_x100)
{
    uint64_t scaled = (uint64_t)bar_x100 * PSI_X10_PER_BAR_X100_NUM;

    return (uint32_t)((scaled + PSI_X10_PER_BAR_X100_DEN / 2u) / PSI_X10_PER_BAR_X100_DEN);
}

int32_t tcan_degc_x10_to_degf_x10(int16_t degc_x10)
{
    int32_t nine_fifths = (int32_t)degc_x10 * 9;

    /* Round half away from zero, then add the 32 degF offset (320 in tenths). */
    nine_fifths += (nine_fifths >= 0) ? 2 : -2;
    return nine_fifths / 5 + 320;
}
