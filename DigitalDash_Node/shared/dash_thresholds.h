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

/* Battery and fuel are not judged here: the car's own charge lamp and low-fuel lamp reach the dash
 * over CAN (read through an opto-coupler) and drive the BAT lamp and the fuel box. The tank is 15.9
 * US gal (60.2 L). */

#endif /* DASH_THRESHOLDS_H */
