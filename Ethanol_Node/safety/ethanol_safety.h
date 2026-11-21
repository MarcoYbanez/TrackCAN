/*******************************************************
 * @file ethanol_safety.h
 * @brief Decides what ethanol value the ECU is given, and when the driver must be warned.
 *
 * Hardware independent. Rules (owner decisions, docs/journal/2026-11-17-tcan2-*.md):
 *  - The ECU cannot detect a failed sensor, so the output is always an in-range value.
 *  - Before the first qualified reading the output is E85 (or the last good value restored after
 *    a watchdog reset). It is never a guess from a single sample.
 *  - If the sensor is lost the last qualified value is HELD, not replaced by a default.
 *  - Critical faults are latched until power cycle. A short dropout recovers by itself.
 *  - All times use wrap-safe uint32 subtraction.
 *******************************************************/
#ifndef ETHANOL_SAFETY_H
#define ETHANOL_SAFETY_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "ethanol_config.h"

/*******************************************************
 * Type Definitions
 *******************************************************/
/* Values match the State signal in can/trackcan.dbc. */
typedef enum {
    ETH_STATE_BOOT = 0,   /* no valid sample yet; output is the boot value */
    ETH_STATE_QUALIFYING, /* valid samples arriving, not yet agreeing long enough */
    ETH_STATE_OK,
    ETH_STATE_HOLDING, /* sensor lost, last qualified value held */
    ETH_STATE_CRITICAL,
} eth_state_t;

/* Values match the Status signal of Heartbeat_Ethanol in can/trackcan.dbc. */
typedef enum {
    ETH_FAULT_NONE = 0,
    ETH_FAULT_SENSOR_LOST = 1,
    ETH_FAULT_NEVER_QUALIFIED = 2,
    ETH_FAULT_DAC_MISMATCH = 3,
    ETH_FAULT_DAC_OVERVOLTAGE = 4,
    ETH_FAULT_WATCHDOG_RESET = 5,
    ETH_FAULT_DAC_I2C = 6,
} eth_fault_t;

typedef struct {
    uint32_t now_ms;
    bool sig_ok;               /* ethanol_signal_evaluate() returned ETH_SIG_OK */
    uint16_t sig_pct_x10;      /* valid when sig_ok */
    bool dac_i2c_ok;           /* the last DAC write succeeded */
    bool readback_valid;       /* a readback sample was taken this step */
    uint16_t readback_err_x10; /* |measured - commanded| in 0.1 points */
    bool overvoltage;          /* readback says the ECU line is above ETH_DAC_OVERVOLT_MV */
} eth_safety_in_t;

typedef struct {
    eth_state_t state;
    uint16_t output_pct_x10; /* what to drive the DAC with, always 0..1000 */
    bool output_is_default;
    bool critical; /* show the big error on the dash */
    bool signal_valid;
    bool dac_warning;
    bool reset_recent;
    eth_fault_t fault; /* highest-priority active fault */
} eth_safety_out_t;

typedef struct {
    bool active;
    uint32_t since;
} eth_timer_t;

typedef struct {
    eth_state_t base;
    uint16_t output;
    bool output_is_default;
    bool ever_qualified;
    bool last_good_valid;
    uint16_t last_good;
    uint32_t boot_ms;
    uint32_t last_step_ms;
    uint32_t slew_accum;

    uint8_t qual_count;
    uint16_t qual_prev;

    eth_timer_t loss;      /* time since the sensor first went invalid (OK -> Holding) */
    eth_timer_t lost_cont; /* continuous time with no valid sample (Critical) */
    eth_timer_t recover;   /* time the signal has been valid and steady */
    uint16_t recover_ref;
    eth_timer_t jump; /* a different value that keeps being reported */
    uint16_t jump_ref;

    eth_timer_t t_dac_warn;
    eth_timer_t t_dac_crit;
    eth_timer_t t_overvolt;
    eth_timer_t t_i2c;

    bool latched_sensor;
    bool latched_dac_mismatch;
    bool latched_overvolt;
    bool latched_i2c;
    bool latched_watchdog;
    bool never_qualified_crit;
    bool reset_recent;
} eth_safety_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/*
 * restored: true when a valid last-good value survived a watchdog reset (restored_pct_x10).
 * watchdog_reset: the previous reset was a watchdog reset. This is critical and latched.
 */
void eth_safety_init(eth_safety_t *s, uint32_t now_ms, bool restored, uint16_t restored_pct_x10,
                     bool watchdog_reset);

void eth_safety_step(eth_safety_t *s, const eth_safety_in_t *in, eth_safety_out_t *out);

/* The last qualified value, for the scratch-register record. Returns false if none yet. */
bool eth_safety_last_good(const eth_safety_t *s, uint16_t *pct_x10);

#endif /* ETHANOL_SAFETY_H */
