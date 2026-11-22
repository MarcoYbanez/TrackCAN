/*******************************************************
 * @file dash_loopback_source.h
 * @brief Simulated DataLogger and Ethanol Nodes for the FDCAN loopback test. Hardware independent.
 *
 * Builds the real TrackCAN frames (the generated packers and the shared core's cycle timing,
 * exactly as the Nodes do) from a demo drive and sends them through a port. In the loopback image
 * the port is the M4's FDCAN controller in internal loopback, so the frames go out of the transmit
 * FIFO, back into the receive FIFO and through the whole Dash decode path to the screen with
 * nothing wired. The data is still a simulation: the image sets the DEMO flag.
 *******************************************************/
#ifndef DASH_LOOPBACK_SOURCE_H
#define DASH_LOOPBACK_SOURCE_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "dash_sim.h"
#include "trackcan_core.h"

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef struct {
    tcan_t datalogger;
    tcan_t ethanol;
    uint8_t hb_datalogger;
    uint8_t hb_ethanol;
    uint32_t step; /* calls so far: the messages start on different steps so they never burst */
} dash_loopback_source_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* Returns false if the port is unusable. */
bool dash_loopback_source_init(dash_loopback_source_t *s, const tcan_port_t *port);

/* Call every few ms: sends each simulated message when its cycle is due, with the values of the
 * demo drive. */
void dash_loopback_source_step(dash_loopback_source_t *s, const dash_sim_out_t *drive);

/* Conversions to what the bus carries (bar x100, degC x10), rounded. Exposed for the tests. */
uint16_t dash_loopback_psi_x10_to_bar_x100(uint32_t psi_x10);
int16_t dash_loopback_degf_x10_to_degc_x10(int32_t degf_x10);

#endif /* DASH_LOOPBACK_SOURCE_H */
