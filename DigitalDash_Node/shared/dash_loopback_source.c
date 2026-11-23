/*******************************************************
 * @file dash_loopback_source.c
 * @brief Simulated DataLogger and Ethanol Nodes for the FDCAN loopback test. See
 * dash_loopback_source.h.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "dash_loopback_source.h"

#include <string.h>

/*******************************************************
 * Macros
 *******************************************************/
#define SIM_ETHANOL_PCT 85
#define SIM_FUEL_TEMP_DEGC_X10 200 /* 20.0 degC */
#define ETHANOL_STATE_OK 2

/* The FDCAN controller has 3 transmit buffers. Real Nodes are separate devices that never share a
 * clock tick, but this simulation sends all of them from one loop, so each message starts on its
 * own step (20 ms) to keep the frames per step low (the Dash adds one of its own per step). A
 * message whose cycle is a multiple of the step keeps its phase from then on. */
/*
 * Per step the Dash itself sends its engine frame, and the simulated oil pressure (every 20 ms since TCAN-28) takes a second slot, so at most ONE other
 * frame may fall on any step. The 100 ms messages (5 steps) take phases 1, 2 and 3 of every 5 steps; the three 1 s heartbeats sit on phase 0 (the Dash's
 * own, from its first call) and phase 4 (the two simulated ones, on different steps of the 50-step second). That keeps every step at 3 frames or fewer.
 */
#define START_STEP_OILPRESSURE 1u
#define START_STEP_ANALOG 1u
#define START_STEP_LIGHTS 2u
#define START_STEP_ETHANOL 3u
#define START_STEP_HB_DATALOGGER 4u
#define START_STEP_HB_ETHANOL 14u

/*******************************************************
 * Public Function Definitions
 *******************************************************/
uint16_t dash_loopback_psi_x10_to_bar_x100(uint32_t psi_x10)
{
    /* 1 psi = 0.0689476 bar, so bar x100 = psi x10 x 0.689476. */
    return (uint16_t)(((uint64_t)psi_x10 * 68948u + 50000u) / 100000u);
}

int16_t dash_loopback_degf_x10_to_degc_x10(int32_t degf_x10)
{
    int32_t n = (degf_x10 - 320) * 5; /* (F - 32) x 5 / 9, kept in tenths */

    return (int16_t)((n >= 0 ? n + 4 : n - 4) / 9);
}

bool dash_loopback_source_init(dash_loopback_source_t *s, const tcan_port_t *port)
{
    memset(s, 0, sizeof(*s));
    return tcan_init(&s->datalogger, port, TCAN_NODE_DATALOGGER) &&
           tcan_init(&s->ethanol, port, TCAN_NODE_ETHANOL);
}

void dash_loopback_source_step(dash_loopback_source_t *s, const dash_sim_out_t *drive)
{
    uint8_t buf[TCAN_MAX_DLC];
    uint32_t step = s->step++;

    if (step >= START_STEP_ANALOG && tcan_tx_due(&s->datalogger, TCAN_MSG_DATALOGGER_ANALOG)) {
        struct trackcan_data_logger_analog_t m = {
            .oil_temp = dash_loopback_degf_x10_to_degc_x10(drive->oil_temp_degf_x10),
            .coolant_temp = dash_loopback_degf_x10_to_degc_x10(drive->coolant_degf_x10),
            .fuel_level = drive->fuel_pct_x10,
        };

        if (trackcan_data_logger_analog_pack(buf, &m, sizeof(buf)) > 0) {
            (void)tcan_tx(&s->datalogger, TCAN_MSG_DATALOGGER_ANALOG, buf);
        }
    }

    if (step >= START_STEP_OILPRESSURE && tcan_tx_due(&s->datalogger, TCAN_MSG_DATALOGGER_OILPRESSURE)) {
        struct trackcan_data_logger_oil_pressure_t m = {
            .oil_pressure = dash_loopback_psi_x10_to_bar_x100(drive->oil_pressure_psi_x10),
        };

        if (trackcan_data_logger_oil_pressure_pack(buf, &m, sizeof(buf)) > 0) {
            (void)tcan_tx(&s->datalogger, TCAN_MSG_DATALOGGER_OILPRESSURE, buf);
        }
    }

    if (step >= START_STEP_LIGHTS && tcan_tx_due(&s->datalogger, TCAN_MSG_DATALOGGER_LIGHTS)) {
        buf[0] = (uint8_t)(drive->lights & 0xFFu);
        buf[1] = (uint8_t)(drive->lights >> 8);
        (void)tcan_tx(&s->datalogger, TCAN_MSG_DATALOGGER_LIGHTS, buf);
    }

    if (step >= START_STEP_HB_DATALOGGER &&
        tcan_tx_due(&s->datalogger, TCAN_MSG_HEARTBEAT_DATALOGGER)) {
        struct trackcan_heartbeat_data_logger_t hb = {.status = 0, .counter = s->hb_datalogger++};

        if (trackcan_heartbeat_data_logger_pack(buf, &hb, sizeof(buf)) > 0) {
            (void)tcan_tx(&s->datalogger, TCAN_MSG_HEARTBEAT_DATALOGGER, buf);
        }
    }

    if (step >= START_STEP_ETHANOL && tcan_tx_due(&s->ethanol, TCAN_MSG_ETHANOL_DATA)) {
        struct trackcan_ethanol_data_t m = {
            .ethanol_content = SIM_ETHANOL_PCT,
            .fuel_temp = SIM_FUEL_TEMP_DEGC_X10,
            .state = ETHANOL_STATE_OK,
            .critical = 0,
            .output_is_default = 0,
            .signal_valid = 1,
            .dac_warning = 0,
            .reset_recent = 0,
        };

        if (trackcan_ethanol_data_pack(buf, &m, sizeof(buf)) > 0) {
            (void)tcan_tx(&s->ethanol, TCAN_MSG_ETHANOL_DATA, buf);
        }
    }

    if (step >= START_STEP_HB_ETHANOL && tcan_tx_due(&s->ethanol, TCAN_MSG_HEARTBEAT_ETHANOL)) {
        struct trackcan_heartbeat_ethanol_t hb = {.status = 0, .counter = s->hb_ethanol++};

        if (trackcan_heartbeat_ethanol_pack(buf, &hb, sizeof(buf)) > 0) {
            (void)tcan_tx(&s->ethanol, TCAN_MSG_HEARTBEAT_ETHANOL, buf);
        }
    }
}
