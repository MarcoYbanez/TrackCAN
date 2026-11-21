/*******************************************************
 * @file ethanol_config.h
 * @brief Every threshold of the Ethanol Node safety logic, with units and reasons.
 *
 * The Node gives the ECU an ethanol-content voltage. The ECU cannot tell that the sensor
 * failed, so the Node must always send an in-range value and tell the driver through the dash.
 * Units: *_X10 = tenths (0.1 % ethanol, 0.1 Hz). Times in ms unless the name says _US.
 * Values marked PROPOSED are first estimates to confirm on the bench (docs/user-todo).
 *******************************************************/
#ifndef ETHANOL_CONFIG_H
#define ETHANOL_CONFIG_H

/*******************************************************
 * Ethanol value
 *******************************************************/
#define ETH_PCT_MAX_X10 1000u
/* Output when there is no qualified measurement. Fuel-only tune: overstating ethanol runs rich
 * and cannot run lean (owner decision, journal 2026-11-17). */
#define ETH_DEFAULT_PCT_X10 850u

/*******************************************************
 * Sensor signal. Ethanol % = frequency in Hz - 50 (50 Hz = E0, 150 Hz = E100).
 * Source: ECU-maker documentation (Haltech, HP Tuners) and resellers; the Continental sheet
 * gives no specification. Plausibility window is wider than 50-150 Hz for sensor tolerance.
 *******************************************************/
#define ETH_F_ZERO_X10 500u /* 50.0 Hz = 0 % */
#define ETH_F_MIN_X10 450u  /* 45.0 Hz */
#define ETH_F_MAX_X10 1550u /* 155.0 Hz: above this (contamination band) is implausible */
#define ETH_LOSS_US 100000u /* no rising edge for 100 ms (5 periods at 50 Hz) = lost */
#define ETH_SIG_PERIODS 8u  /* most recent periods used for the median */
#define ETH_SIG_MIN_PERIODS 4u
#define ETH_GATE_US                                                                                \
    300000u                     /* edge-count window for the cross-check (a longer window would    \
                                   delay qualification after the sensor starts) */
#define ETH_GATE_MIN_US 250000u /* shorter windows skip the cross-check */
#define ETH_XCHECK_TOL_X10 100u /* median frequency and counted frequency must agree within 10 Hz  \
                                 */

/* Fuel temperature: pulse width 1 ms = -40 degC, 5 ms = 125 degC (reseller sources). */
#define ETH_PW_MIN_US 900u
#define ETH_PW_MAX_US 5100u
#define ETH_TEMP_INVALID_X10 (-32768)

/*******************************************************
 * Safety state machine
 *******************************************************/
#define ETH_QUALIFY_SAMPLES 10u   /* consecutive agreeing samples before leaving the default */
#define ETH_QUALIFY_AGREE_X10 20u /* agree = within 2.0 points of the previous sample */
#define ETH_NEVER_QUALIFIED_CRITICAL_MS                                                            \
    2000u                            /* owner: critical in under 3 s; measure sensor start-up */
#define ETH_LOSS_WARN_MS 2000u       /* invalid this long: hold the value, show Holding */
#define ETH_LOSS_CRITICAL_MS 10000u  /* invalid this long: latch Critical, still hold the value */
#define ETH_RECOVER_STABLE_MS 10000u /* valid and steady this long: leave Holding */
#define ETH_NOISE_DEADBAND_X10 10u   /* changes up to 1.0 point follow immediately */
#define ETH_SLEW_X10_PER_S 50u       /* larger changes follow at 5 points per second */
#define ETH_JUMP_PERSIST_MS 5000u    /* a steady different value this long is accepted */
#define ETH_JUMP_STABLE_X10 30u      /* "steady" = within 3.0 points of where it started */

/*******************************************************
 * DAC output (DFR0971 / GP8403, 0-5 V range, 12-bit)
 * 0.5 V (code 410) = 0 %, 4.5 V (code 3685) = 100 %: 4 V per 100 %, so 40 mV = 1 point.
 *******************************************************/
#define ETH_DAC_CODE_MIN 410u
#define ETH_DAC_CODE_SPAN 3275u
#define ETH_DAC_FULL_SCALE_MV 5000u
#define ETH_DAC_MV_PER_POINT 40u

/* Readback through a resistor divider (R1 = 22 kOhm top, R2 = 10 kOhm bottom, ratio 0.3125). */
#define ETH_DIVIDER_R1_OHM 22000u
#define ETH_DIVIDER_R2_OHM 10000u
#define ETH_ADC_VREF_MV 3300u
#define ETH_ADC_FULL_SCALE 4095u
/* Set to 1 only after the divider is fitted (docs/user-todo B1). An unwired pin would raise
 * a false critical on every start. */
#ifndef ETH_DAC_READBACK_ENABLED
#define ETH_DAC_READBACK_ENABLED 0
#endif
#define ETH_READBACK_ADC_GPIO 27u /* ADC1 */

#define ETH_DAC_MISMATCH_WARN_X10 30u  /* PROPOSED: 3.0 points = 0.12 V: warning only */
#define ETH_DAC_MISMATCH_CRIT_X10 100u /* PROPOSED: 10.0 points = 0.40 V: critical */
#define ETH_DAC_MISMATCH_MS 500u
#define ETH_DAC_OVERVOLT_MV 5250u /* more than 5 V on the ECU line */
#define ETH_DAC_OVERVOLT_MS 200u
#define ETH_DAC_I2C_FAIL_CRIT_MS 1000u /* DAC writes failing this long: ECU output unverifiable */
#define ETH_DAC_RANGE_REFRESH_MS 2000u /* re-assert the 0-5 V range (in case only the DAC reset)   \
                                        */
#define ETH_I2C_TIMEOUT_US 5000u       /* every I2C transfer is bounded */

/*******************************************************
 * Watchdog and health monitor (modeled on AUTOSAR WdgM alive supervision)
 *******************************************************/
#define ETH_WATCHDOG_MS 500u /* hardware timeout; the SDK already applies erratum RP2040-E1 */
#define ETH_HEALTH_PERIOD_MS 200u
#define ETH_HEALTH_FAIL_CYCLES 2u /* consecutive failed cycles before the feed stops */
#define ETH_READER_PERIOD_MS 50u
#define ETH_DAC_PERIOD_MS 100u

#endif /* ETHANOL_CONFIG_H */
