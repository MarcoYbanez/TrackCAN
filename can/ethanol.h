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
#include <stdint.h>

#include "trackcan_core.h"

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* Creates the CAN task. Call before vTaskStartScheduler(). Returns 0 on success. */
int ethanol_can_start(void);

/* Lock-free; callable from any task. content_pct: 0..100, fuel_temp_degc_x10: 0.1 degC. */
void ethanol_can_set_sample(uint8_t content_pct, int16_t fuel_temp_degc_x10);

#endif /* ETHANOL_CAN_H */
