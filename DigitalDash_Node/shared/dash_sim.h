/*******************************************************
 * @file dash_sim.h
 * @brief A DEMO drive: a repeating loop of idle, full-throttle pulls through the gears, cruising
 * and braking, with the engine values that go with it. Used by the M4 test image to exercise the
 * whole dash before real CAN data exists. Hardware independent, integer only, deterministic.
 *
 * The numbers are plausible, not measured: the gear ratios are the approximate 2002 WRX 5 speed
 * figures from memory (3.454 / 1.947 / 1.366 / 0.972 / 0.738, final drive 3.9, tyre
 * circumference 1.94 m) and every engine value is a simple model. Nothing here is a safety value or
 * a source for a threshold.
 *
 * Schedule: one loop is about 44 s. Loop 1 (the second loop) injects an oil pressure sag at the
 * final idle (OIL lamp). Loop 2 runs with a charging fault (charge lamp, low battery voltage) and a
 * check engine lamp at the final idle. The pattern repeats every 3 loops. Fuel drains until the
 * low-fuel lamp comes on, then the tank is refilled.
 *******************************************************/
#ifndef DASH_SIM_H
#define DASH_SIM_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

/*******************************************************
 * Macros
 *******************************************************/
/* Same bit positions as the Dash's lights field (can/trackcan.dbc, DataLogger_Lights). */
#define DASH_SIM_LIGHT_CHECK_ENGINE 0x0001u
#define DASH_SIM_LIGHT_OIL 0x0002u
#define DASH_SIM_LIGHT_FUEL_LOW 0x0020u
#define DASH_SIM_LIGHT_CHARGE 0x0100u

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef struct {
    uint16_t rpm;
    uint16_t speed_kmh_x100;
    uint8_t gear; /* 0 neutral, 1 to 5 */
    uint8_t throttle_pct;
    int16_t boost_psi_x10; /* negative is vacuum */
    uint16_t afr_x10;
    uint16_t battery_volts_x10;
    int32_t coolant_degf_x10;
    int32_t oil_temp_degf_x10;
    uint16_t oil_pressure_psi_x10;
    uint16_t fuel_pct_x10;
    uint16_t lights;
} dash_sim_out_t;

typedef struct {
    uint32_t uptime_ms;
    uint32_t phase_ms;
    uint8_t phase;
    uint8_t loop;
    uint8_t gear;
    uint32_t shift_ms;
    int32_t speed_x10000; /* km/h x 10000 */
    int32_t rpm;
    int32_t boost_x100; /* tenths of a psi x 100, for smooth integer filtering */
    int32_t afr_x100;
    int32_t battery_x100;
    int32_t coolant_x100;
    int32_t oil_temp_x100;
    int32_t oil_press_x100;
    int32_t fuel_micro_pct; /* percent x 1,000,000 */
    bool fuel_low;
} dash_sim_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

void dash_sim_init(dash_sim_t *s);

/* Advance the drive by dt_ms (clamped to 100) and report the values for this instant. */
void dash_sim_step(dash_sim_t *s, uint32_t dt_ms, dash_sim_out_t *out);

/* Engine rpm for a gear (1 to 5) at a road speed. 0 for neutral. */
uint16_t dash_sim_rpm_for(uint8_t gear, uint32_t kmh_x100);

#endif /* DASH_SIM_H */
