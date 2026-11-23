/*******************************************************
 * @file datalogger.h
 * @brief DataLogger Node (Teensy 4.1, Zephyr): TrackCAN port and cyclic transmit/receive.
 *
 * Including this file's .c in the app starts a dedicated CAN thread, so the existing main
 * loop does not need to change. The application publishes sensor values with
 * datalogger_can_set_sample() and reads what the other Nodes send with
 * datalogger_can_get().
 *******************************************************/
#ifndef DATALOGGER_CAN_H
#define DATALOGGER_CAN_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "trackcan_core.h"

/*******************************************************
 * Type Definitions
 *******************************************************/

/* Values this Node publishes, already in bus units. */
struct datalogger_sample {
    uint16_t oil_pressure_bar_x100;
    int16_t oil_temp_degc_x10;
    int16_t coolant_temp_degc_x10;
    uint16_t fuel_level_pct_x10;
    uint16_t lights; /* bit 0 check engine ... bit 7 high beam, bit 8 charge warning, see
                        can/trackcan.dbc */
};

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* Thread-safe. Takes effect on the next transmit of each message. */
void datalogger_can_set_sample(const struct datalogger_sample *sample);

/* Thread-safe copy of the latest payload for a message another Node sends. */
tcan_rx_state_t datalogger_can_get(tcan_msg_t msg, uint8_t data[TCAN_MAX_DLC]);

/*
 * A tap on the bus for the SD logger: called for every frame this Node sends (tx = true, only when
 * the controller accepted it) and every frame it receives (tx = false). It runs in the CAN thread,
 * in one case with a spinlock held, so it must not block: queue the frame and return. Pass NULL to
 * remove it.
 */
typedef void (*datalogger_can_tap_t)(bool tx, const tcan_frame_t *frame);
void datalogger_can_set_tap(datalogger_can_tap_t tap);

/*
 * Controller diagnostics for the logger's status line. state is the Zephyr can_state value (0 error-active, 1 warning, 2 error-passive,
 * 3 bus-off, 4 stopped). Returns 0, or a negative errno if the controller is not ready.
 */
int datalogger_can_status(int *state, unsigned *tx_err, unsigned *rx_err);

#endif /* DATALOGGER_CAN_H */
