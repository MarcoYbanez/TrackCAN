/*******************************************************
 * @file dash.h
 * @brief Dash Node (STM32H7): TrackCAN logic. Hardware-independent; the STM32H7 FDCAN port (Zephyr
 * CAN API, M4 core) is in dash_port_stm32h7.c.
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
    /* The oil pressure has its own message (DataLogger_OilPressure, 50 Hz) and its own freshness, so a stalled slow message (temperatures,
     * fuel level) can never hide it or switch off the low-oil alarm. analog_state is the slow message: oil temperature, coolant, fuel. */
    tcan_rx_state_t oil_pressure_state;
    uint32_t oil_pressure_psi_x10;

    tcan_rx_state_t analog_state;
    int32_t oil_temp_degf_x10;
    int32_t coolant_temp_degf_x10;
    uint16_t fuel_level_pct_x10;

    tcan_rx_state_t lights_state;
    uint16_t lights; /* bit 0 check engine ... bit 7 high beam, bit 8 charge warning, see
                        can/trackcan.dbc */

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

/* Receive counters of the CAN logic, for diagnostics. */
struct dash_can_stats {
    uint32_t rx_frames;     /* accepted frames */
    uint32_t rx_bad_frames; /* known ID with a wrong length */
    uint32_t tx_dropped;    /* the port could not send */
};
void dash_can_get_stats(struct dash_can_stats *out);

/* Controller health reported by the port (the hardware), for the CAN lamp and the console. */
enum dash_can_hw_state {
    DASH_CAN_HW_UNKNOWN = 0, /* not started */
    DASH_CAN_HW_ACTIVE,      /* error active: healthy */
    DASH_CAN_HW_WARNING,     /* an error counter passed 96 */
    DASH_CAN_HW_PASSIVE, /* error passive: a counter passed 127 (for example nobody acknowledges our
                            frames) */
    DASH_CAN_HW_BUS_OFF,
    DASH_CAN_HW_STOPPED,
    DASH_CAN_HW_INIT_FAILED, /* the controller could not be started */
};
/* Which step of starting the controller failed (dash_can_hw_status.init_step), with the Zephyr
 * error code in init_err. */
enum dash_can_init_step {
    DASH_CAN_STEP_NONE = 0,
    DASH_CAN_STEP_DEVICE_NOT_READY, /* the driver's init failed or it is disabled */
    DASH_CAN_STEP_SET_MODE,
    DASH_CAN_STEP_ADD_FILTER,
    DASH_CAN_STEP_START,
};
struct dash_can_hw_status {
    uint8_t state; /* enum dash_can_hw_state */
    uint8_t tx_err;
    uint8_t rx_err;
    uint8_t init_step; /* enum dash_can_init_step of the last failed start, 0 if none */
    int16_t init_err;  /* negative errno of that step (0 for a device that is not ready) */
    /* The bit timing the driver works out for 500 kbit/s at 87.5 % from the clock it believes it
     * has (0 if not started). */
    uint32_t core_clock_hz;
    uint16_t prescaler;
    uint16_t tq_per_bit;
    uint16_t sample_point_permille;
};

/*
 * Starts the FDCAN controller (500 kbit/s, classic CAN; the bit timing comes from the devicetree).
 * loopback = true is the controller's internal loopback: it receives its own frames and touches
 * nothing on the bus, so it needs no wiring. Returns false if it could not be started; call it
 * again to retry.
 */
bool dash_port_stm32h7_start(bool loopback);
void dash_port_stm32h7_status(struct dash_can_hw_status *out);

#endif /* DASH_CAN_H */
