/*******************************************************
 * @file dash_render.c
 * @brief Draws the lit RPM bar segments into a transparent ARGB8888 buffer, pixel for pixel.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "dash_render.h"

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void draw_segment(uint32_t *buf, size_t stride_px, const dash_bar_t *bar,
                         const dash_seg_t *s)
{
    for (uint8_t row = 0; row < s->h; row++) {
        uint32_t *line = buf + (size_t)(s->y + row) * stride_px + s->x;

        for (uint8_t c = 0; c < s->ncols; c++) {
            line[c] = bar->cols[s->col_off + c];
        }
    }
    /* The dim baseline one row under the segment. */
    {
        uint32_t *line = buf + (size_t)(s->y + s->h) * stride_px + s->x;

        for (uint8_t c = 0; c < s->nbase; c++) {
            line[c] = bar->base[s->base_off + c];
        }
    }
}

static void erase_segment(uint32_t *buf, size_t stride_px, const dash_seg_t *s)
{
    uint8_t width = (s->ncols > s->nbase) ? s->ncols : s->nbase;

    for (uint8_t row = 0; row <= s->h; row++) {
        uint32_t *line = buf + (size_t)(s->y + row) * stride_px + s->x;

        for (uint8_t c = 0; c < width; c++) {
            line[c] = 0x00000000u;
        }
    }
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void dash_bar_clear(uint32_t *buf, size_t stride_px, const dash_bar_t *bar, uint8_t *lit)
{
    for (uint16_t y = 0; y < bar->height; y++) {
        uint32_t *line = buf + (size_t)y * stride_px;

        for (uint16_t x = 0; x < bar->width; x++) {
            line[x] = 0x00000000u;
        }
    }
    *lit = 0;
}

int dash_bar_set_lit(uint32_t *buf, size_t stride_px, const dash_bar_t *bar, uint8_t *lit,
                     uint8_t new_lit)
{
    uint8_t old = *lit;

    if (new_lit > bar->count) {
        new_lit = bar->count;
    }
    if (new_lit == old) {
        return 0;
    }
    if (new_lit > old) {
        for (uint8_t i = old; i < new_lit; i++) {
            draw_segment(buf, stride_px, bar, &bar->segs[i]);
        }
    } else {
        for (uint8_t i = new_lit; i < old; i++) {
            erase_segment(buf, stride_px, &bar->segs[i]);
        }
    }
    *lit = new_lit;
    return 1;
}
