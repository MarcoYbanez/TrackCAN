/*******************************************************
 * @file ethanol_signal.h
 * @brief Turns raw edge measurements from the flex-fuel sensor into a trusted ethanol value.
 *
 * Hardware independent. The caller (main.c) captures edge timing in an interrupt and passes a
 * snapshot in. No division by zero, no float, no undefined casts: bad input gives a status,
 * never a value.
 *******************************************************/
#ifndef ETHANOL_SIGNAL_H
#define ETHANOL_SIGNAL_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdint.h>

#include "ethanol_config.h"

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef enum {
    ETH_SIG_OK = 0,
    ETH_SIG_LOST,        /* no recent edge */
    ETH_SIG_TOO_FEW,     /* not enough periods yet (boot, or a gap) */
    ETH_SIG_IMPLAUSIBLE, /* outside the plausibility window */
    ETH_SIG_MISMATCH,    /* period method and edge-count method disagree: noise */
} eth_sig_status_t;

typedef struct {
    uint32_t periods_us[ETH_SIG_PERIODS]; /* most recent rising-edge to rising-edge periods */
    uint8_t n;                            /* how many of periods_us are valid */
    uint32_t age_us;                      /* time since the last rising edge */
    uint32_t gate_us;                     /* length of the edge-count window */
    uint32_t rises_in_gate;               /* rising edges counted in that window */
} eth_sig_input_t;

typedef struct {
    eth_sig_status_t status;
    uint16_t freq_x10; /* median frequency, 0.1 Hz; valid when status is OK or MISMATCH */
    uint16_t pct_x10;  /* ethanol, 0.1 %, 0..1000; valid only when status is OK */
} eth_sig_result_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/
eth_sig_result_t ethanol_signal_evaluate(const eth_sig_input_t *in);

/* Fuel temperature from the pulse width: returns 0 and sets *temp_x10 (0.1 degC), or -1 if the
 * pulse width is implausible. */
int ethanol_temp_from_pulse_us(uint32_t pulse_us, int16_t *temp_x10);

#endif /* ETHANOL_SIGNAL_H */
