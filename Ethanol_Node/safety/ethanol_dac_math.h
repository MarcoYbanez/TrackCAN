/*******************************************************
 * @file ethanol_dac_math.h
 * @brief Ethanol value to DAC code, and ADC readback to millivolts. Integer math only.
 *******************************************************/
#ifndef ETHANOL_DAC_MATH_H
#define ETHANOL_DAC_MATH_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdint.h>

#include "ethanol_config.h"

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* 12-bit DAC code for an ethanol value (0.1 %). 0 % -> 410 (0.5 V), 100 % -> 3685 (4.5 V).
 * Values above 100 % are clamped. */
uint16_t eth_dac_code_from_pct_x10(uint16_t pct_x10);

/* Output voltage in millivolts the DAC produces for a code in its 0-5 V range. */
uint32_t eth_dac_code_to_mv(uint16_t code);

/* ECU-line voltage in millivolts from a 12-bit ADC reading taken behind the divider. */
uint32_t eth_readback_mv(uint16_t adc_raw);

/* Difference between the measured and the commanded voltage, in 0.1 ethanol points
 * (40 mV per point). */
uint16_t eth_mismatch_x10(uint32_t measured_mv, uint32_t commanded_mv);

#endif /* ETHANOL_DAC_MATH_H */
