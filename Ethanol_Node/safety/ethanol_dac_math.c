/*******************************************************
 * @file ethanol_dac_math.c
 * @brief Ethanol value to DAC code, and ADC readback to millivolts. Integer math only.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "ethanol_dac_math.h"

/*******************************************************
 * Public Function Definitions
 *******************************************************/
uint16_t eth_dac_code_from_pct_x10(uint16_t pct_x10)
{
    uint32_t pct = (pct_x10 > ETH_PCT_MAX_X10) ? ETH_PCT_MAX_X10 : pct_x10;

    /* code = 410 + pct/100 * 3275, with pct in tenths: + pct_x10 * 3275 / 1000, rounded. */
    return (uint16_t)(ETH_DAC_CODE_MIN + (pct * ETH_DAC_CODE_SPAN + 500u) / 1000u);
}

uint32_t eth_dac_code_to_mv(uint16_t code)
{
    uint32_t c = (code > ETH_ADC_FULL_SCALE) ? ETH_ADC_FULL_SCALE : code;

    return (c * ETH_DAC_FULL_SCALE_MV + ETH_ADC_FULL_SCALE / 2u) / ETH_ADC_FULL_SCALE;
}

uint32_t eth_readback_mv(uint16_t adc_raw)
{
    uint64_t adc_mv;
    uint64_t line_mv;

    if (adc_raw > ETH_ADC_FULL_SCALE) {
        adc_raw = ETH_ADC_FULL_SCALE;
    }
    adc_mv = ((uint64_t)adc_raw * ETH_ADC_VREF_MV) / ETH_ADC_FULL_SCALE;
    /* Undo the divider: Vline = Vadc * (R1 + R2) / R2. */
    line_mv = (adc_mv * (ETH_DIVIDER_R1_OHM + ETH_DIVIDER_R2_OHM)) / ETH_DIVIDER_R2_OHM;
    return (uint32_t)line_mv;
}

uint16_t eth_mismatch_x10(uint32_t measured_mv, uint32_t commanded_mv)
{
    uint32_t diff_mv =
        (measured_mv > commanded_mv) ? (measured_mv - commanded_mv) : (commanded_mv - measured_mv);
    uint32_t x10 = (diff_mv * 10u) / ETH_DAC_MV_PER_POINT;

    return (x10 > 0xFFFFu) ? 0xFFFFu : (uint16_t)x10;
}
