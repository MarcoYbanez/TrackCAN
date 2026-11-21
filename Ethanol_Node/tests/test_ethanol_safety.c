/*******************************************************
 * @file test_ethanol_safety.c
 * @brief Host tests for the Ethanol Node safety logic.
 *
 * Expected values are worked out by hand from the sensor and DAC definitions, not produced by
 * the code under test. Scenarios use a simulated clock in 50 ms steps.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>
#include <string.h>

#include "ethanol_boot.h"
#include "ethanol_dac_math.h"
#include "ethanol_health.h"
#include "ethanol_safety.h"
#include "ethanol_scratch.h"
#include "ethanol_signal.h"

/*******************************************************
 * Macros
 *******************************************************/
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        l_checks++;                                                                                \
        if (!(cond)) {                                                                             \
            l_failures++;                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
        }                                                                                          \
    } while (0)

#define STEP_MS 50u

/*******************************************************
 * Static Variables
 *******************************************************/
static int l_checks;
static int l_failures;

/*******************************************************
 * Simulation helpers
 *******************************************************/
typedef struct {
    eth_safety_t s;
    eth_safety_out_t o;
    uint32_t now;
} sim_t;

static void sim_init(sim_t *m, uint32_t start_ms, bool restored, uint16_t restored_pct, bool wd)
{
    memset(m, 0, sizeof(*m));
    m->now = start_ms;
    eth_safety_init(&m->s, start_ms, restored, restored_pct, wd);
}

static void sim_step_in(sim_t *m, eth_safety_in_t in)
{
    m->now += STEP_MS;
    in.now_ms = m->now;
    eth_safety_step(&m->s, &in, &m->o);
}

static eth_safety_in_t base_in(bool sig_ok, uint16_t pct)
{
    eth_safety_in_t in;

    memset(&in, 0, sizeof(in));
    in.sig_ok = sig_ok;
    in.sig_pct_x10 = pct;
    in.dac_i2c_ok = true;
    return in;
}

/* Run with a fixed sensor input for duration_ms. */
static void sim_run(sim_t *m, uint32_t duration_ms, bool sig_ok, uint16_t pct)
{
    for (uint32_t t = 0; t < duration_ms; t += STEP_MS) {
        sim_step_in(m, base_in(sig_ok, pct));
    }
}

/* Bring a node to a qualified E50 (500). */
static void sim_qualified(sim_t *m, uint32_t start_ms)
{
    sim_init(m, start_ms, false, 0, false);
    sim_run(m, 600, true, 500);
}

/*******************************************************
 * Signal tests
 *******************************************************/
static void fill_periods(eth_sig_input_t *in, uint32_t period_us, uint8_t n)
{
    memset(in, 0, sizeof(*in));
    for (uint8_t i = 0; i < n; i++) {
        in->periods_us[i] = period_us;
    }
    in->n = n;
    in->age_us = 1000;
}

static void test_signal_mapping(void)
{
    eth_sig_input_t in;
    eth_sig_result_t r;

    /* Ethanol % = Hz - 50. 100 Hz = 10000 us = E50; 135 Hz = 7407 us = E85; 50 Hz = E0. */
    fill_periods(&in, 10000, 8);
    r = ethanol_signal_evaluate(&in);
    CHECK(r.status == ETH_SIG_OK && r.freq_x10 == 1000 && r.pct_x10 == 500);

    fill_periods(&in, 7407, 8);
    r = ethanol_signal_evaluate(&in);
    CHECK(r.status == ETH_SIG_OK && r.freq_x10 == 1350 && r.pct_x10 == 850);

    fill_periods(&in, 20000, 8);
    r = ethanol_signal_evaluate(&in);
    CHECK(r.status == ETH_SIG_OK && r.pct_x10 == 0);

    fill_periods(&in, 6667, 8); /* 150 Hz = E100 */
    r = ethanol_signal_evaluate(&in);
    CHECK(r.status == ETH_SIG_OK && r.pct_x10 == 1000);

    fill_periods(&in, 22222, 8); /* 45.0 Hz: inside the tolerance window, clamps to 0 % */
    r = ethanol_signal_evaluate(&in);
    CHECK(r.status == ETH_SIG_OK && r.pct_x10 == 0);
}

static void test_signal_rejects_bad_input(void)
{
    eth_sig_input_t in;

    /* Lost: no recent edge. Exactly 100 ms is still fine, one microsecond more is lost. */
    fill_periods(&in, 10000, 8);
    in.age_us = 100000;
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_OK);
    in.age_us = 100001;
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_LOST);

    /* No periods at all (boot, first edge not yet seen): never a value. */
    fill_periods(&in, 10000, 0);
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_LOST);

    fill_periods(&in, 10000, 3);
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_TOO_FEW);

    /* 180 Hz (contamination band) and 40 Hz are implausible, never clamped into a value. */
    fill_periods(&in, 5556, 8);
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_IMPLAUSIBLE);
    fill_periods(&in, 25000, 8);
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_IMPLAUSIBLE);

    /* A zero period must not divide by zero. */
    fill_periods(&in, 0, 8);
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_IMPLAUSIBLE);

    CHECK(ethanol_signal_evaluate(NULL).status == ETH_SIG_LOST);
}

static void test_signal_median_and_crosscheck(void)
{
    eth_sig_input_t in;
    eth_sig_result_t r;

    /* Two glitch periods among eight do not move the median: still 100 Hz. */
    fill_periods(&in, 10000, 8);
    in.periods_us[2] = 500;
    in.periods_us[5] = 500;
    r = ethanol_signal_evaluate(&in);
    CHECK(r.status == ETH_SIG_OK && r.pct_x10 == 500);

    /* Edge counting over 500 ms: 50 rises = 100 Hz agrees. 100 rises = 200 Hz is noise. */
    fill_periods(&in, 10000, 8);
    in.gate_us = 500000;
    in.rises_in_gate = 50;
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_OK);
    in.rises_in_gate = 100;
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_MISMATCH);

    /* Tolerance is 10 Hz: 55 rises (110 Hz) passes, 56 (112 Hz) fails. */
    in.rises_in_gate = 55;
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_OK);
    in.rises_in_gate = 56;
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_MISMATCH);

    /* A window shorter than 300 ms is too short to judge. */
    in.gate_us = 200000;
    in.rises_in_gate = 1000;
    CHECK(ethanol_signal_evaluate(&in).status == ETH_SIG_OK);
}

static void test_temperature(void)
{
    int16_t t = 0;

    /* degC = 41.25 * ms - 81.25: 1 ms = -40.0, 3 ms = 42.5, 5 ms = 125.0. */
    CHECK(ethanol_temp_from_pulse_us(1000, &t) == 0 && t == -400);
    CHECK(ethanol_temp_from_pulse_us(3000, &t) == 0 && t == 425);
    CHECK(ethanol_temp_from_pulse_us(5000, &t) == 0 && t == 1250);
    CHECK(ethanol_temp_from_pulse_us(800, &t) == -1);
    CHECK(ethanol_temp_from_pulse_us(5200, &t) == -1);
    CHECK(ethanol_temp_from_pulse_us(3000, NULL) == -1);
}

/*******************************************************
 * DAC math tests
 *******************************************************/
static void test_dac_math(void)
{
    /* 0 % = 0.5 V = code 410, 100 % = 4.5 V = code 3685, E85 = 410 + 0.85 * 3275 = 3194. */
    CHECK(eth_dac_code_from_pct_x10(0) == 410);
    CHECK(eth_dac_code_from_pct_x10(1000) == 3685);
    CHECK(eth_dac_code_from_pct_x10(850) == 3194);
    CHECK(eth_dac_code_from_pct_x10(500) == 2048);
    CHECK(eth_dac_code_from_pct_x10(1200) == 3685); /* clamped, never above 4.5 V */
    CHECK(eth_dac_code_from_pct_x10(0xFFFF) == 3685);

    /* Code to millivolts in the 0-5 V range: E85 is 3.9 V. */
    CHECK(eth_dac_code_to_mv(410) == 501);
    CHECK(eth_dac_code_to_mv(3194) == 3900);
    CHECK(eth_dac_code_to_mv(3685) == 4499); /* 3685 / 4095 * 5 V = 4.4994 V */

    /* Readback behind the 22k/10k divider (x3.2). Raw 1512 of 4095 at 3.3 V = 1218 mV, so the
     * line is 3897 mV, 3 mV under 3900 from quantization. */
    CHECK(eth_readback_mv(0) == 0);
    CHECK(eth_readback_mv(1512) == 3897);
    CHECK(eth_readback_mv(4095) == 10560);
    CHECK(eth_readback_mv(60000) == 10560);

    /* 40 mV per ethanol point; result in tenths of a point. */
    CHECK(eth_mismatch_x10(3897, 3900) == 0);
    CHECK(eth_mismatch_x10(4020, 3900) == 30);  /* 120 mV = 3.0 points */
    CHECK(eth_mismatch_x10(3500, 3900) == 100); /* 400 mV = 10.0 points */
}

/*******************************************************
 * Health and scratch tests
 *******************************************************/
static void cycle(eth_health_t *h, uint32_t reader, uint32_t dac)
{
    for (uint32_t i = 0; i < reader; i++) {
        eth_health_checkin(h, ETH_ENTITY_READER);
    }
    for (uint32_t i = 0; i < dac; i++) {
        eth_health_checkin(h, ETH_ENTITY_DAC);
    }
}

static void test_health(void)
{
    eth_health_t h;

    eth_health_init(&h);
    CHECK(h.min_per_cycle[ETH_ENTITY_READER] == 2 && h.max_per_cycle[ETH_ENTITY_READER] == 6);
    CHECK(h.min_per_cycle[ETH_ENTITY_DAC] == 1 && h.max_per_cycle[ETH_ENTITY_DAC] == 4);

    /* Nominal cycles feed the watchdog. */
    cycle(&h, 4, 2);
    CHECK(eth_health_evaluate(&h) && eth_health_should_feed(&h));

    /* One failed cycle is tolerated; the second stops the feed, and it never resumes. */
    cycle(&h, 0, 2);
    CHECK(!eth_health_evaluate(&h) && eth_health_should_feed(&h));
    cycle(&h, 0, 2);
    CHECK(!eth_health_evaluate(&h) && !eth_health_should_feed(&h));
    cycle(&h, 4, 2);
    CHECK(eth_health_evaluate(&h) && !eth_health_should_feed(&h));

    /* A failed cycle followed by a good one resets the count. */
    eth_health_init(&h);
    cycle(&h, 4, 0); /* the DAC task stalled */
    CHECK(!eth_health_evaluate(&h));
    cycle(&h, 4, 2);
    CHECK(eth_health_evaluate(&h));
    cycle(&h, 4, 0);
    CHECK(!eth_health_evaluate(&h) && eth_health_should_feed(&h));

    /* A runaway task (too many check-ins) fails alive supervision too. */
    eth_health_init(&h);
    cycle(&h, 40, 2);
    CHECK(!eth_health_evaluate(&h));

    /* Check-in counters wrap safely. */
    eth_health_init(&h);
    h.checkins[ETH_ENTITY_READER] = 0xFFFFFFFEu;
    h.last_seen[ETH_ENTITY_READER] = 0xFFFFFFFEu;
    h.checkins[ETH_ENTITY_DAC] = 0xFFFFFFFFu;
    h.last_seen[ETH_ENTITY_DAC] = 0xFFFFFFFFu;
    cycle(&h, 4, 2);
    CHECK(eth_health_evaluate(&h));
}

static void test_scratch(void)
{
    eth_scratch_record_t rec = {850, ETH_REASON_MALLOC_FAILED, 3};
    eth_scratch_record_t out;
    uint32_t w[ETH_SCRATCH_WORDS];

    /* Hand-computed: w1 = 850 (0x0352) | ~0x0352 << 16 = 0xFCAD0352; w2 = 2 | 3 << 8 = 0x0302;
     * w3 = 0x45544831 ^ 0xFCAD0352 ^ 0x0302 ^ 0xA5A5A5A5 = 0x1C5CEDC4. */
    eth_scratch_pack(&rec, w);
    CHECK(w[0] == 0x45544831u && w[1] == 0xFCAD0352u && w[2] == 0x00000302u && w[3] == 0x1C5CEDC4u);

    memset(&out, 0, sizeof(out));
    CHECK(eth_scratch_unpack(w, &out));
    CHECK(out.last_good_pct_x10 == 850 && out.reason == ETH_REASON_MALLOC_FAILED &&
          out.reset_count == 3);

    /* Any corrupted word is rejected. */
    for (unsigned i = 0; i < ETH_SCRATCH_WORDS; i++) {
        uint32_t bad[ETH_SCRATCH_WORDS];

        memcpy(bad, w, sizeof(bad));
        bad[i] ^= 0x00000100u;
        CHECK(!eth_scratch_unpack(bad, &out));
    }

    /* An erased record (power-up) is rejected. */
    memset(w, 0, sizeof(w));
    CHECK(!eth_scratch_unpack(w, &out));

    /* An out-of-range value is rejected even with a valid checksum. */
    rec.last_good_pct_x10 = 1001;
    eth_scratch_pack(&rec, w);
    CHECK(!eth_scratch_unpack(w, &out));
}

static void test_boot_classification(void)
{
    eth_scratch_record_t rec = {640, ETH_REASON_NONE, 2};
    eth_boot_decision_t d;

    /* Power-up (or a UF2 flash, reset button, bootrom reboot): not a fault, nothing restored,
     * and any stale record is wiped. This is the case that would otherwise boot into Critical. */
    d = eth_boot_decide(false, false, &rec);
    CHECK(!d.watchdog_fault && !d.restored && d.clear_scratch && d.reset_count == 0);

    /* A stale-looking but valid record on a non-timeout boot must NOT be restored. */
    d = eth_boot_decide(false, true, &rec);
    CHECK(!d.watchdog_fault && !d.restored && d.clear_scratch);

    /* Our watchdog timed out and the record is valid: fault, restore the last good value. */
    d = eth_boot_decide(true, true, &rec);
    CHECK(d.watchdog_fault && d.restored && d.pct_x10 == 640 && d.reset_count == 3);
    CHECK(!d.clear_scratch);

    /* The counter saturates. */
    rec.reset_count = 255;
    d = eth_boot_decide(true, true, &rec);
    CHECK(d.reset_count == 255);

    /* Timed out but the record is corrupt or missing: fault, nothing to restore. */
    d = eth_boot_decide(true, false, &rec);
    CHECK(d.watchdog_fault && !d.restored && d.reset_count == 1 && !d.clear_scratch);
    d = eth_boot_decide(true, false, NULL);
    CHECK(d.watchdog_fault && !d.restored);
    d = eth_boot_decide(true, true, NULL); /* "valid" with no record: refuse to restore */
    CHECK(d.watchdog_fault && !d.restored);
}

/*******************************************************
 * Safety state machine scenarios
 *******************************************************/
static void test_boot_default_and_qualification(void)
{
    sim_t m;

    /* Boot: the output is E85 and flagged as a default, never an unmeasured guess. */
    sim_init(&m, 1000, false, 0, false);
    sim_run(&m, 100, false, 0);
    CHECK(m.o.output_pct_x10 == 850 && m.o.output_is_default);
    CHECK(m.o.state == ETH_STATE_BOOT && !m.o.critical && m.o.fault == ETH_FAULT_NONE);

    /* 10 agreeing samples are needed: the 9th is not enough, the 10th qualifies. */
    sim_init(&m, 1000, false, 0, false);
    for (int i = 0; i < 9; i++) {
        sim_step_in(&m, base_in(true, 500));
    }
    CHECK(m.o.output_pct_x10 == 850 && m.o.output_is_default && m.o.state == ETH_STATE_QUALIFYING);
    sim_step_in(&m, base_in(true, 500));
    CHECK(m.o.output_pct_x10 == 500 && !m.o.output_is_default && m.o.state == ETH_STATE_OK);
    CHECK(!m.o.critical);

    /* Samples that disagree by more than 2.0 points never qualify. */
    sim_init(&m, 1000, false, 0, false);
    for (int i = 0; i < 30; i++) {
        sim_step_in(&m, base_in(true, (i % 2) ? 600 : 500));
    }
    CHECK(m.o.output_is_default && m.o.state != ETH_STATE_OK);

    /* A lost sample restarts the count. */
    sim_init(&m, 1000, false, 0, false);
    for (int i = 0; i < 9; i++) {
        sim_step_in(&m, base_in(true, 500));
    }
    sim_step_in(&m, base_in(false, 0));
    sim_step_in(&m, base_in(true, 500));
    CHECK(m.o.output_is_default);
}

static void test_never_qualified_critical(void)
{
    sim_t m;

    /* No sensor: critical shortly after 2 s (owner: under 3 s), output stays the E85 default. */
    sim_init(&m, 0, false, 0, false);
    sim_run(&m, 2000, false, 0);
    CHECK(!m.o.critical);
    sim_step_in(&m, base_in(false, 0));
    CHECK(m.o.critical && m.o.state == ETH_STATE_CRITICAL);
    CHECK(m.o.fault == ETH_FAULT_NEVER_QUALIFIED);
    CHECK(m.o.output_pct_x10 == 850 && m.o.output_is_default);

    /* A slow sensor that finally qualifies clears this one (it is not latched). */
    sim_run(&m, 600, true, 500);
    CHECK(!m.o.critical && m.o.state == ETH_STATE_OK && m.o.output_pct_x10 == 500);
    CHECK(m.o.fault == ETH_FAULT_NONE);
}

static void test_cut_wire_holds_last_value(void)
{
    sim_t m;

    sim_qualified(&m, 0);
    CHECK(m.o.state == ETH_STATE_OK && m.o.output_pct_x10 == 500);

    /* Wire cut. Under 2 s: output unchanged, still OK, but the signal is flagged invalid. */
    sim_run(&m, 1900, false, 0);
    CHECK(m.o.state == ETH_STATE_OK && !m.o.signal_valid && m.o.output_pct_x10 == 500);

    /* From 2 s: Holding, a warning only, the value is HELD (not E0, not the E85 default). */
    sim_run(&m, 200, false, 0);
    CHECK(m.o.state == ETH_STATE_HOLDING && !m.o.critical && m.o.fault == ETH_FAULT_SENSOR_LOST);
    CHECK(m.o.output_pct_x10 == 500 && !m.o.output_is_default);

    /* From 10 s: Critical, latched, and the output is still the held value. */
    sim_run(&m, 8000, false, 0);
    CHECK(m.o.critical && m.o.state == ETH_STATE_CRITICAL && m.o.fault == ETH_FAULT_SENSOR_LOST);
    CHECK(m.o.output_pct_x10 == 500 && !m.o.output_is_default);

    /* The sensor returns and is steady for a long time: the output follows, but the critical
     * stays latched until power cycle. */
    sim_run(&m, 20000, true, 500);
    CHECK(m.o.critical && m.o.output_pct_x10 == 500);
}

static void test_short_dropout_recovers(void)
{
    sim_t m;

    sim_qualified(&m, 0);
    sim_run(&m, 1500, false, 0);
    CHECK(m.o.state == ETH_STATE_OK);
    sim_run(&m, 200, true, 500);
    CHECK(m.o.state == ETH_STATE_OK && m.o.signal_valid && !m.o.critical);

    /* A dropout in the Holding band (2 s to 10 s) recovers only after 10 s of steady signal. */
    sim_run(&m, 4000, false, 0);
    CHECK(m.o.state == ETH_STATE_HOLDING);
    sim_run(&m, 9000, true, 520);
    CHECK(m.o.state == ETH_STATE_HOLDING && m.o.output_pct_x10 == 500);
    sim_run(&m, 1500, true, 520);
    CHECK(m.o.state == ETH_STATE_OK && m.o.output_pct_x10 == 520 && !m.o.critical);

    /* An unsteady returning signal never recovers. */
    sim_qualified(&m, 0);
    sim_run(&m, 3000, false, 0);
    for (int i = 0; i < 400; i++) {
        sim_step_in(&m, base_in(true, (i % 2) ? 700 : 300));
    }
    CHECK(m.o.state == ETH_STATE_HOLDING && !m.o.critical);
    CHECK(m.o.output_pct_x10 == 500);
}

static void test_noise_and_refuel(void)
{
    sim_t m;

    /* One wild sample (E90) moves the output by only a fraction of a point. */
    sim_qualified(&m, 0);
    sim_step_in(&m, base_in(true, 900));
    CHECK(m.o.output_pct_x10 >= 500 && m.o.output_pct_x10 <= 503);
    sim_run(&m, 200, true, 500);
    CHECK(m.o.output_pct_x10 == 500);

    /* A 1 s burst moves it at most 5 points (the 5 points per second rate limit). */
    sim_qualified(&m, 0);
    sim_run(&m, 1000, true, 900);
    CHECK(m.o.output_pct_x10 > 500 && m.o.output_pct_x10 <= 551);
    sim_run(&m, 3000, true, 500);
    CHECK(m.o.output_pct_x10 <= 551);

    /* Small sensor noise (within 1.0 point) is followed directly. */
    sim_qualified(&m, 0);
    sim_step_in(&m, base_in(true, 508));
    CHECK(m.o.output_pct_x10 == 508);

    /* A real change (refuel mixing in): a steady new value is accepted after 5 s. */
    sim_qualified(&m, 0);
    sim_run(&m, 4900, true, 800);
    CHECK(m.o.output_pct_x10 < 800);
    sim_run(&m, 200, true, 800);
    CHECK(m.o.output_pct_x10 == 800 && m.o.state == ETH_STATE_OK && !m.o.critical);
}

static void test_dac_faults(void)
{
    sim_t m;
    eth_safety_in_t in;

    /* Mismatch of 12 points (0.48 V) for 500 ms: latched critical. */
    sim_qualified(&m, 0);
    for (int i = 0; i < 12; i++) {
        in = base_in(true, 500);
        in.readback_valid = true;
        in.readback_err_x10 = 120;
        sim_step_in(&m, in);
    }
    CHECK(m.o.critical && m.o.fault == ETH_FAULT_DAC_MISMATCH && m.o.dac_warning);

    /* A transient of 100 ms is not enough. */
    sim_qualified(&m, 0);
    for (int i = 0; i < 2; i++) {
        in = base_in(true, 500);
        in.readback_valid = true;
        in.readback_err_x10 = 120;
        sim_step_in(&m, in);
    }
    sim_step_in(&m, base_in(true, 500));
    CHECK(!m.o.critical);

    /* 5 points (0.2 V): a warning only, never critical. */
    sim_qualified(&m, 0);
    for (int i = 0; i < 40; i++) {
        in = base_in(true, 500);
        in.readback_valid = true;
        in.readback_err_x10 = 50;
        sim_step_in(&m, in);
    }
    CHECK(m.o.dac_warning && !m.o.critical);

    /* 1 point (0.04 V): ignored. */
    sim_qualified(&m, 0);
    for (int i = 0; i < 40; i++) {
        in = base_in(true, 500);
        in.readback_valid = true;
        in.readback_err_x10 = 10;
        sim_step_in(&m, in);
    }
    CHECK(!m.o.dac_warning && !m.o.critical);

    /* Over-voltage on the ECU line for 200 ms: latched, and the highest-priority fault. */
    sim_qualified(&m, 0);
    for (int i = 0; i < 6; i++) {
        in = base_in(false, 0);
        in.readback_valid = true;
        in.overvoltage = true;
        sim_step_in(&m, in);
    }
    CHECK(m.o.critical && m.o.fault == ETH_FAULT_DAC_OVERVOLTAGE);
    sim_run(&m, 20000, true, 500);
    CHECK(m.o.critical && m.o.fault == ETH_FAULT_DAC_OVERVOLTAGE);

    /* DAC writes failing for 1 s: critical. For 500 ms only: not. */
    sim_qualified(&m, 0);
    for (int i = 0; i < 10; i++) {
        in = base_in(true, 500);
        in.dac_i2c_ok = false;
        sim_step_in(&m, in);
    }
    CHECK(!m.o.critical);
    sim_step_in(&m, base_in(true, 500));
    for (int i = 0; i < 21; i++) {
        in = base_in(true, 500);
        in.dac_i2c_ok = false;
        sim_step_in(&m, in);
    }
    CHECK(m.o.critical && m.o.fault == ETH_FAULT_DAC_I2C);
}

static void test_watchdog_restore(void)
{
    sim_t m;

    /* After a watchdog reset the last good value is restored, not the E85 default, and the
     * dash is warned immediately. */
    sim_init(&m, 0, true, 700, true);
    sim_step_in(&m, base_in(false, 0));
    CHECK(m.o.output_pct_x10 == 700 && !m.o.output_is_default);
    CHECK(m.o.critical && m.o.fault == ETH_FAULT_WATCHDOG_RESET && m.o.reset_recent);

    /* A fresh qualified reading replaces the restored value. */
    sim_run(&m, 600, true, 300);
    CHECK(m.o.output_pct_x10 == 300 && m.o.critical);

    /* A corrupt restore value is refused. */
    sim_init(&m, 0, true, 1500, true);
    sim_step_in(&m, base_in(false, 0));
    CHECK(m.o.output_pct_x10 == 850 && m.o.output_is_default);

    /* A power-up (no restore) is never critical by itself. */
    sim_init(&m, 0, false, 0, false);
    sim_step_in(&m, base_in(false, 0));
    CHECK(!m.o.critical && !m.o.reset_recent);
}

static void test_fault_priority_and_bounds(void)
{
    sim_t m;
    eth_safety_in_t in;
    uint16_t good = 0;

    /* Over-voltage outranks a lost sensor. */
    sim_qualified(&m, 0);
    for (int i = 0; i < 300; i++) {
        in = base_in(false, 0);
        in.readback_valid = true;
        in.overvoltage = (i > 100);
        sim_step_in(&m, in);
    }
    CHECK(m.o.fault == ETH_FAULT_DAC_OVERVOLTAGE && m.o.critical);

    /* Whatever the sensor says, the output stays within 0..1000. */
    sim_qualified(&m, 0);
    sim_run(&m, 2000, true, 1000);
    CHECK(m.o.output_pct_x10 <= 1000);
    sim_run(&m, 8000, true, 0);
    CHECK(m.o.output_pct_x10 <= 1000);

    /* Last good value is available once qualified. */
    sim_init(&m, 0, false, 0, false);
    CHECK(!eth_safety_last_good(&m.s, &good));
    sim_run(&m, 600, true, 640);
    CHECK(eth_safety_last_good(&m.s, &good) && good == 640);
    CHECK(!eth_safety_last_good(NULL, &good));
}

static void test_time_wrap(void)
{
    sim_t m;

    /* Start 2 s before the 32-bit millisecond counter wraps; everything must behave the same. */
    sim_init(&m, 0xFFFFFFFFu - 2000u, false, 0, false);
    sim_run(&m, 600, true, 500);
    CHECK(m.o.state == ETH_STATE_OK && m.o.output_pct_x10 == 500);

    sim_run(&m, 1900, false, 0);
    CHECK(m.o.state == ETH_STATE_OK);
    sim_run(&m, 200, false, 0);
    CHECK(m.o.state == ETH_STATE_HOLDING);
    sim_run(&m, 8000, false, 0);
    CHECK(m.o.critical && m.o.output_pct_x10 == 500);

    /* Never-qualified timing across the wrap. */
    sim_init(&m, 0xFFFFFFFFu - 500u, false, 0, false);
    sim_run(&m, 2000, false, 0);
    CHECK(!m.o.critical);
    sim_step_in(&m, base_in(false, 0));
    CHECK(m.o.critical && m.o.fault == ETH_FAULT_NEVER_QUALIFIED);
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    test_signal_mapping();
    test_signal_rejects_bad_input();
    test_signal_median_and_crosscheck();
    test_temperature();
    test_dac_math();
    test_health();
    test_scratch();
    test_boot_classification();
    test_boot_default_and_qualification();
    test_never_qualified_critical();
    test_cut_wire_holds_last_value();
    test_short_dropout_recovers();
    test_noise_and_refuel();
    test_dac_faults();
    test_watchdog_restore();
    test_fault_priority_and_bounds();
    test_time_wrap();

    printf("%d checks, %d failures\n", l_checks, l_failures);
    return l_failures == 0 ? 0 : 1;
}
