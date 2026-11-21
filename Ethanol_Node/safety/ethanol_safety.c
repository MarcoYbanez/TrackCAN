/*******************************************************
 * @file ethanol_safety.c
 * @brief Decides what ethanol value the ECU is given, and when the driver must be warned.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "ethanol_safety.h"

#include <stddef.h>
#include <string.h>

/*******************************************************
 * Static Function Definitions
 *******************************************************/
/* True once `cond` has held continuously for `ms`. Wrap-safe. */
static bool held_for(eth_timer_t *t, bool cond, uint32_t now, uint32_t ms)
{
    if (!cond) {
        t->active = false;
        return false;
    }
    if (!t->active) {
        t->active = true;
        t->since = now;
    }
    return (uint32_t)(now - t->since) >= ms;
}

static uint16_t abs_diff_u16(uint16_t a, uint16_t b)
{
    return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static void accept_qualified(eth_safety_t *s, uint16_t pct)
{
    s->output = pct;
    s->output_is_default = false;
    s->last_good = pct;
    s->last_good_valid = true;
    s->ever_qualified = true;
    s->never_qualified_crit = false;
    s->base = ETH_STATE_OK;
    s->loss.active = false;
    s->recover.active = false;
    s->jump.active = false;
}

/* Boot and qualification: N consecutive samples that agree with each other. */
static void step_qualifying(eth_safety_t *s, const eth_safety_in_t *in)
{
    if (!in->sig_ok) {
        s->qual_count = 0;
        return;
    }

    if (s->qual_count > 0 && abs_diff_u16(in->sig_pct_x10, s->qual_prev) <= ETH_QUALIFY_AGREE_X10) {
        s->qual_count++;
    } else {
        s->qual_count = 1;
    }
    s->qual_prev = in->sig_pct_x10;
    s->base = ETH_STATE_QUALIFYING;

    if (s->qual_count >= ETH_QUALIFY_SAMPLES) {
        accept_qualified(s, in->sig_pct_x10);
    }
}

/* Normal operation: follow noise, rate-limit jumps, accept a steady new value. */
static void step_ok(eth_safety_t *s, const eth_safety_in_t *in, uint32_t dt_ms)
{
    uint16_t sig;
    uint16_t diff;

    if (!in->sig_ok) {
        if (!s->loss.active) {
            s->loss.active = true;
            s->loss.since = in->now_ms;
        }
        s->recover.active = false;
        if ((uint32_t)(in->now_ms - s->loss.since) >= ETH_LOSS_WARN_MS) {
            s->base = ETH_STATE_HOLDING;
        }
        return;
    }

    /* A valid sample ends a dropout shorter than the warning time. */
    s->loss.active = false;
    sig = in->sig_pct_x10;
    diff = abs_diff_u16(sig, s->output);

    if (diff <= ETH_NOISE_DEADBAND_X10) {
        s->output = sig;
        s->jump.active = false;
    } else {
        uint32_t step;
        uint16_t move;

        /* Follow at the physical rate limit. */
        s->slew_accum += ETH_SLEW_X10_PER_S * dt_ms;
        step = s->slew_accum / 1000u;
        s->slew_accum %= 1000u;
        move = (step > diff) ? diff : (uint16_t)step;
        s->output = (sig > s->output) ? (uint16_t)(s->output + move) : (uint16_t)(s->output - move);

        /* A different value that stays steady is real (a refuel mixing in): accept it. */
        if (!s->jump.active || abs_diff_u16(sig, s->jump_ref) > ETH_JUMP_STABLE_X10) {
            s->jump.active = true;
            s->jump.since = in->now_ms;
            s->jump_ref = sig;
        } else if ((uint32_t)(in->now_ms - s->jump.since) >= ETH_JUMP_PERSIST_MS) {
            s->output = sig;
            s->jump.active = false;
        }
    }
    s->last_good = s->output;
    s->last_good_valid = true;
}

/* Sensor lost: hold the last qualified value. Leave only after a long, steady recovery. */
static void step_holding(eth_safety_t *s, const eth_safety_in_t *in)
{
    if (!in->sig_ok) {
        s->recover.active = false;
        return;
    }

    if (!s->recover.active ||
        abs_diff_u16(in->sig_pct_x10, s->recover_ref) > ETH_QUALIFY_AGREE_X10) {
        s->recover.active = true;
        s->recover.since = in->now_ms;
    }
    s->recover_ref = in->sig_pct_x10;

    if ((uint32_t)(in->now_ms - s->recover.since) >= ETH_RECOVER_STABLE_MS) {
        accept_qualified(s, in->sig_pct_x10);
    }
}

static eth_fault_t highest_fault(const eth_safety_t *s)
{
    if (s->latched_overvolt) {
        return ETH_FAULT_DAC_OVERVOLTAGE;
    }
    if (s->latched_dac_mismatch) {
        return ETH_FAULT_DAC_MISMATCH;
    }
    if (s->latched_i2c) {
        return ETH_FAULT_DAC_I2C;
    }
    if (s->latched_watchdog) {
        return ETH_FAULT_WATCHDOG_RESET;
    }
    if (s->never_qualified_crit) {
        return ETH_FAULT_NEVER_QUALIFIED;
    }
    if (s->latched_sensor || s->base == ETH_STATE_HOLDING) {
        return ETH_FAULT_SENSOR_LOST;
    }
    return ETH_FAULT_NONE;
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void eth_safety_init(eth_safety_t *s, uint32_t now_ms, bool restored, uint16_t restored_pct_x10,
                     bool watchdog_reset)
{
    memset(s, 0, sizeof(*s));
    s->base = ETH_STATE_BOOT;
    s->boot_ms = now_ms;
    s->last_step_ms = now_ms;

    if (restored && restored_pct_x10 <= ETH_PCT_MAX_X10) {
        s->output = restored_pct_x10;
        s->last_good = restored_pct_x10;
        s->last_good_valid = true;
        s->output_is_default = false;
    } else {
        s->output = ETH_DEFAULT_PCT_X10;
        s->output_is_default = true;
    }

    if (watchdog_reset) {
        s->latched_watchdog = true;
        s->reset_recent = true;
    }
}

void eth_safety_step(eth_safety_t *s, const eth_safety_in_t *in, eth_safety_out_t *out)
{
    uint32_t dt_ms = (uint32_t)(in->now_ms - s->last_step_ms);

    s->last_step_ms = in->now_ms;

    /* 1. Output-stage faults, independent of the sensor. */
    if (held_for(&s->t_overvolt, in->readback_valid && in->overvoltage, in->now_ms,
                 ETH_DAC_OVERVOLT_MS)) {
        s->latched_overvolt = true;
    }
    if (held_for(&s->t_dac_crit,
                 in->readback_valid && in->readback_err_x10 >= ETH_DAC_MISMATCH_CRIT_X10,
                 in->now_ms, ETH_DAC_MISMATCH_MS)) {
        s->latched_dac_mismatch = true;
    }
    if (held_for(&s->t_i2c, !in->dac_i2c_ok, in->now_ms, ETH_DAC_I2C_FAIL_CRIT_MS)) {
        s->latched_i2c = true;
    }

    /* 2. Sensor handling. */
    if (s->base == ETH_STATE_BOOT || s->base == ETH_STATE_QUALIFYING) {
        step_qualifying(s, in);
        if (!s->ever_qualified &&
            (uint32_t)(in->now_ms - s->boot_ms) > ETH_NEVER_QUALIFIED_CRITICAL_MS) {
            s->never_qualified_crit = true;
        }
    } else if (s->base == ETH_STATE_OK) {
        step_ok(s, in, dt_ms);
    } else {
        step_holding(s, in);
    }

    /* No valid sample for ETH_LOSS_CRITICAL_MS in a row latches Critical until power cycle, but
     * the value stays held. A valid sample restarts this timer, so a short dropout that then
     * needs ETH_RECOVER_STABLE_MS of steady signal to recover does not latch Critical. */
    if ((s->base == ETH_STATE_OK || s->base == ETH_STATE_HOLDING) &&
        held_for(&s->lost_cont, !in->sig_ok, in->now_ms, ETH_LOSS_CRITICAL_MS)) {
        s->latched_sensor = true;
    }

    /* 3. Report. */
    out->output_pct_x10 = s->output;
    out->output_is_default = s->output_is_default;
    out->signal_valid = in->sig_ok;
    out->dac_warning = held_for(
        &s->t_dac_warn, in->readback_valid && in->readback_err_x10 >= ETH_DAC_MISMATCH_WARN_X10,
        in->now_ms, ETH_DAC_MISMATCH_MS);
    out->reset_recent = s->reset_recent;
    out->fault = highest_fault(s);
    out->critical = s->latched_sensor || s->latched_dac_mismatch || s->latched_overvolt ||
                    s->latched_i2c || s->latched_watchdog || s->never_qualified_crit;
    out->state = out->critical ? ETH_STATE_CRITICAL : s->base;
}

bool eth_safety_last_good(const eth_safety_t *s, uint16_t *pct_x10)
{
    if (s == NULL || pct_x10 == NULL || !s->last_good_valid) {
        return false;
    }
    *pct_x10 = s->last_good;
    return true;
}
