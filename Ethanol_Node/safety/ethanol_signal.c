/*******************************************************
 * @file ethanol_signal.c
 * @brief Turns raw edge measurements from the flex-fuel sensor into a trusted ethanol value.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "ethanol_signal.h"

#include <stddef.h>

/*******************************************************
 * Static Function Definitions
 *******************************************************/
/* Median of up to ETH_SIG_PERIODS values by insertion sort (bounded loops). */
static uint32_t median_us(const uint32_t *src, uint8_t n)
{
    uint32_t v[ETH_SIG_PERIODS];

    for (uint8_t i = 0; i < n; i++) {
        uint32_t x = src[i];
        uint8_t j = i;

        while (j > 0 && v[j - 1] > x) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
    return (n % 2u) ? v[n / 2u] : (v[n / 2u - 1u] + v[n / 2u]) / 2u;
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
eth_sig_result_t ethanol_signal_evaluate(const eth_sig_input_t *in)
{
    eth_sig_result_t r = {ETH_SIG_LOST, 0, 0};
    uint32_t med;
    uint32_t f_x10;
    uint8_t n;

    if (in == NULL || in->n == 0 || in->age_us > ETH_LOSS_US) {
        return r;
    }

    n = (in->n > ETH_SIG_PERIODS) ? ETH_SIG_PERIODS : in->n;
    if (n < ETH_SIG_MIN_PERIODS) {
        r.status = ETH_SIG_TOO_FEW;
        return r;
    }

    med = median_us(in->periods_us, n);
    if (med == 0) {
        r.status = ETH_SIG_IMPLAUSIBLE;
        return r;
    }

    /* Hz x 10 = 10^7 / period in microseconds, rounded. */
    f_x10 = (10000000u + med / 2u) / med;
    if (f_x10 < ETH_F_MIN_X10 || f_x10 > ETH_F_MAX_X10) {
        r.status = ETH_SIG_IMPLAUSIBLE;
        return r;
    }
    r.freq_x10 = (uint16_t)f_x10;

    /* Second method: count edges over a window. Extra noise edges raise the count but not the
     * median period, so the two disagree. */
    if (in->gate_us >= ETH_GATE_MIN_US) {
        uint64_t counted = ((uint64_t)in->rises_in_gate * 10000000u) / in->gate_us;
        uint64_t diff = (counted > f_x10) ? (counted - f_x10) : (f_x10 - counted);

        if (diff > ETH_XCHECK_TOL_X10) {
            r.status = ETH_SIG_MISMATCH;
            return r;
        }
    }

    r.pct_x10 = (f_x10 <= ETH_F_ZERO_X10) ? 0u : (uint16_t)(f_x10 - ETH_F_ZERO_X10);
    if (r.pct_x10 > ETH_PCT_MAX_X10) {
        r.pct_x10 = ETH_PCT_MAX_X10;
    }
    r.status = ETH_SIG_OK;
    return r;
}

int ethanol_temp_from_pulse_us(uint32_t pulse_us, int16_t *temp_x10)
{
    int64_t t;

    if (temp_x10 == NULL || pulse_us < ETH_PW_MIN_US || pulse_us > ETH_PW_MAX_US) {
        return -1;
    }
    /* degC = 41.25 * ms - 81.25  ->  tenths: (4125 * us - 8125000) / 10000, rounded. */
    t = (int64_t)4125 * (int64_t)pulse_us - 8125000;
    t = (t >= 0) ? (t + 5000) / 10000 : (t - 5000) / 10000;
    if (t < -400) {
        t = -400;
    } else if (t > 1250) {
        t = 1250;
    }
    *temp_x10 = (int16_t)t;
    return 0;
}
