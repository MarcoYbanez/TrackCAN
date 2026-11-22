/*******************************************************
 * @file dash_render.h
 * @brief Draws the lit RPM bar segments into a transparent ARGB8888 buffer, pixel for pixel.
 *
 * Hardware and LVGL independent. The buffer is a bar's region (dash_bar_t): lit segments are
 * opaque, everything else is transparent, so the background image's dim segments show through.
 * Segments light left to right.
 *******************************************************/
#ifndef DASH_RENDER_H
#define DASH_RENDER_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stddef.h>
#include <stdint.h>

#include "dash_geometry.h"

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* Makes the whole region transparent and sets *lit to 0. */
void dash_bar_clear(uint32_t *buf, size_t stride_px, const dash_bar_t *bar, uint8_t *lit);

/*
 * Lights segments 0 .. new_lit - 1 and darkens the rest, touching only the segments that change.
 * *lit holds the count currently drawn and is updated. new_lit above the segment count is clamped.
 * Returns true if anything changed.
 */
int dash_bar_set_lit(uint32_t *buf, size_t stride_px, const dash_bar_t *bar, uint8_t *lit,
                     uint8_t new_lit);

#endif /* DASH_RENDER_H */
