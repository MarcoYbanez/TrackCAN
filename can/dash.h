/*******************************************************
 * @file dash.h
 * @brief Dash Node (STM32H7): TrackCAN logic. Hardware-independent; the STM32H7 FDCAN port is
 *        a stub, see dash_port_stm32h7.c.
 *
 * The bus carries bar and degC. The Dash converts to PSI and degF for display.
 *******************************************************/
#ifndef DASH_CAN_H
#define DASH_CAN_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "trackcan_core.h"

/*******************************************************
 * Type Definitions
 *******************************************************/

/* Everything the display layer needs, already in display units. Check the *_state fields:
 * a value is only meaningful while its state is TCAN_RX_FRESH. */
struct dash_display {
    tcan_rx_state_t analog_state;
    uint32_t oil_pressure_psi_x10;
    int32_t oil_temp_degf_x10;
    int32_t coolant_temp_degf_x10;
    uint16_t fuel_level_pct_x10;

    tcan_rx_state_t lights_state;
    uint8_t lights; /* bit 0 check engine ... bit 7 high beam, see can/trackcan.dbc */

    tcan_rx_state_t ethanol_state;
    uint8_t ethanol_pct;
    int32_t fuel_temp_degf_x10;
    bool fuel_temp_valid; /* false when the Node reports the temperature as invalid */

    /* Ethanol Node health. ethanol_alert is the "something is very wrong" flag: show the big
     * error screen. It is set when the Node reports Critical, when its messages stop (stale),
     * or when none arrived within DASH_ETHANOL_BOOT_ALERT_MS of the Dash starting. */
    uint8_t ethanol_node_state;  /* 0 Boot, 1 Qualifying, 2 Ok, 3 Holding, 4 Critical */
    bool ethanol_output_default; /* the ECU is being given the E85 default, not a measurement */
    bool ethanol_alert;
    uint8_t ethanol_fault_code; /* Heartbeat_Ethanol status: 0 none ... 6, see can/trackcan.dbc */
};

#define DASH_ETHANOL_BOOT_ALERT_MS 2000u

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* The STM32H7 port for dash_can_init(). Currently a stub that sends and receives nothing. */
const tcan_port_t *dash_port_stm32h7(void);

bool dash_can_init(const tcan_port_t *port);

/* Call periodically (every few ms): receives pending frames and sends what is due. */
void dash_can_step(void);

/* rpm: 1 rpm per bit. speed_kmh_x100: 0.01 km/h per bit. From the timer input capture. */
void dash_can_set_engine(uint16_t rpm, uint16_t speed_kmh_x100);

void dash_can_get_display(struct dash_display *out);

#endif /* DASH_CAN_H */
