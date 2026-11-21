/*******************************************************
 * @file ethanol.h
 * @brief Ethanol Node (Adafruit RP2040 CAN Bus Feather, Pico SDK + FreeRTOS): TrackCAN port.
 *
 * Safety rule: the PWM-to-DAC output to the ECU must never depend on CAN. Everything here
 * runs in its own low-priority task, never blocks on the bus, and a missing or failed CAN
 * controller only makes this task retry. The other tasks are unaffected.
 *******************************************************/
#ifndef ETHANOL_CAN_H
#define ETHANOL_CAN_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "trackcan_core.h"

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* Creates the CAN task. Call before vTaskStartScheduler(). Returns 0 on success. */
int ethanol_can_start(void);

/* What the Ethanol Node reports on CAN. Filled by main.c from the safety logic. */
typedef struct {
    uint8_t content_pct;   /* what the ECU is being given, 0..100 */
    int16_t fuel_temp_x10; /* 0.1 degC, or INT16_MIN when invalid */
    uint8_t state;         /* 0 Boot, 1 Qualifying, 2 Ok, 3 Holding, 4 Critical */
    bool critical;
    bool output_is_default;
    bool signal_valid;
    bool dac_warning;
    bool reset_recent;
    uint8_t fault_code; /* Heartbeat_Ethanol status, see can/trackcan.dbc */
} ethanol_can_status_t;

/* Callable from any task (copied under a critical section). */
void ethanol_can_set_status(const ethanol_can_status_t *status);

#endif /* ETHANOL_CAN_H */
