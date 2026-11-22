/*******************************************************
 * @file dash_thresholds.h
 * @brief The warning limits of the gauge boxes, in ONE place. Change them here and rebuild (TCAN-5
 * phase 1).
 *
 * Units match dash_view.h: tenths (deg F x10, psi x10, volts x10, percent x10). A box turns red at
 * or beyond its limit. DASH_LIMIT_NONE_HIGH / DASH_LIMIT_NONE_LOW mean "no limit". Sources:
 * docs/journal/ 2026-11-21-tcan5-warning-threshold-research.md.
 *******************************************************/
#ifndef DASH_THRESHOLDS_H
#define DASH_THRESHOLDS_H

#include "dash_view.h"

/* Oil pressure: one fixed limit, red BELOW 15.0 psi (a value of 14.9 or less). Owner decision. For
 * reference the factory minimum is 14 psi at 800 rpm and 43 psi at 5,000 rpm at 80 C oil (service
 * manual ME(STi)-26). */
#define DASH_OIL_PRESSURE_LOW_X10 149
#define DASH_OIL_PRESSURE_HYST_X10 10 /* back above 15.9 psi to clear. My choice, not sourced. */

/* Coolant: red at 220 F. Owner decision. (The factory gauge calls 70-100 C = 158-212 F normal,
 * IDI-14.) */
#define DASH_COOLANT_HIGH_X10 2200
#define DASH_COOLANT_HYST_X10 20 /* clears below 218 F. My choice. */

/* Oil temperature: red at 235 F, the figure motorsport forums use. Owner decision, no factory
 * source. */
#define DASH_OIL_TEMP_HIGH_X10 2350
#define DASH_OIL_TEMP_HYST_X10 20 /* clears below 233 F. My choice. */

/* FULL-SCREEN "LOW OIL PRESSURE" error: the one riskier-data case the owner has named so far
 * (2026-11-22). It is the SEVERE level, separate from the red oil pressure box above (15 psi). It
 * fires when the engine is really running (RPM from a real measurement at or above
 * DASH_OIL_CRIT_MIN_RPM) and the pressure is at or below 10 psi (the owner's choice, 2026-11-22;
 * it is under the factory minimum of 14 psi at 800 rpm, service manual ME(STi)-26, and above the
 * car's own oil pressure warning switch point of 2.1 psi, IDI-5, see
 * docs/journal/2026-11-21-tcan5-warning-threshold-research.md), and has stayed there for
 * DASH_OIL_CRIT_HOLD_MS, so the pressure building up after a start and a one-sample blip do not
 * trigger it. It clears once the pressure is back above DASH_OIL_CRIT_CLEAR_PSI_X10. The RPM
 * minimum, the hold time and the clear level are my choices, not sourced. With no RPM measurement
 * (a live image before TCAN-7) it cannot fire, because "engine running" is unknown. WATCH ON THE
 * CAR: a cold engine must get above 10 psi within DASH_OIL_CRIT_HOLD_MS of the RPM passing the
 * minimum, or this fires at every start. */
#define DASH_OIL_CRIT_PSI_X10 100
#define DASH_OIL_CRIT_CLEAR_PSI_X10 110 /* clears above 11.0 psi */
#define DASH_OIL_CRIT_MIN_RPM 500       /* above cranking (about 250), below idle (about 850) */
#define DASH_OIL_CRIT_HOLD_MS 1000

/* Battery and fuel are not judged here: the car's own charge lamp and low-fuel lamp reach the dash
 * over CAN (read through an opto-coupler) and drive the BAT lamp and the fuel box. The tank is 15.9
 * US gal (60.2 L). */

#endif /* DASH_THRESHOLDS_H */
