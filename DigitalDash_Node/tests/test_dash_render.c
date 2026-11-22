/*******************************************************
 * @file test_dash_render.c
 * @brief Host tests: the C renderer reproduces the original fully lit tach frames exactly.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>
#include <string.h>

#include "dash_golden.h"
#include "dash_render.h"

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

#define MAX_PX (461u * 39u)

/*******************************************************
 * Static Variables
 *******************************************************/
static int l_checks;
static int l_failures;

/*******************************************************
 * Static Function Definitions
 *******************************************************/
/* FNV-1a of the buffer composited on black, as RGB bytes row by row (the same recipe as the
 * generator). */
static uint32_t flat_hash(const uint32_t *buf, const dash_bar_t *bar)
{
    uint32_t h = 0x811C9DC5u;

    for (uint16_t y = 0; y < bar->height; y++) {
        for (uint16_t x = 0; x < bar->width; x++) {
            uint32_t p = buf[(size_t)y * bar->width + x];
            uint32_t a = p >> 24;
            uint8_t rgb[3] = {(uint8_t)(((p >> 16) & 0xFFu) * a / 255u),
                              (uint8_t)(((p >> 8) & 0xFFu) * a / 255u),
                              (uint8_t)((p & 0xFFu) * a / 255u)};

            for (int k = 0; k < 3; k++) {
                h = (h ^ rgb[k]) * 0x01000193u;
            }
        }
    }
    return h;
}

static int all_zero(const uint32_t *buf, const dash_bar_t *bar)
{
    for (size_t i = 0; i < (size_t)bar->width * bar->height; i++) {
        if (buf[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void test_bar(const char *name, const dash_bar_t *bar, uint32_t golden_hash, unsigned gw,
                     unsigned gh)
{
    static uint32_t a[MAX_PX];
    static uint32_t b[MAX_PX];
    uint8_t lit_a = 0;
    uint8_t lit_b = 0;

    (void)name;
    CHECK(bar->width == gw && bar->height == gh);
    CHECK((size_t)bar->width * bar->height <= MAX_PX);

    /* Fully lit equals the original frame, pixel for pixel (same checksum as the PNG). */
    dash_bar_clear(a, bar->width, bar, &lit_a);
    CHECK(all_zero(a, bar));
    CHECK(dash_bar_set_lit(a, bar->width, bar, &lit_a, bar->count) == 1);
    CHECK(lit_a == bar->count);
    CHECK(flat_hash(a, bar) == golden_hash);

    /* Asking for more than the bar has is clamped, and nothing changes the second time. */
    CHECK(dash_bar_set_lit(a, bar->width, bar, &lit_a, 200) == 0);
    CHECK(lit_a == bar->count);

    /* History does not matter: going 0 -> 30 -> all gives the same picture as going straight to
     * all. */
    dash_bar_clear(b, bar->width, bar, &lit_b);
    dash_bar_set_lit(b, bar->width, bar, &lit_b, (uint8_t)(bar->count / 2));
    dash_bar_set_lit(b, bar->width, bar, &lit_b, bar->count);
    CHECK(memcmp(a, b, (size_t)bar->width * bar->height * sizeof(uint32_t)) == 0);

    /* Going back down erases exactly: all -> 0 leaves a fully transparent region. */
    dash_bar_set_lit(a, bar->width, bar, &lit_a, 0);
    CHECK(lit_a == 0 && all_zero(a, bar));

    /* Partial states equal a fresh draw of that count, whichever way they were reached. */
    for (uint8_t n = 1; n < bar->count; n = (uint8_t)(n + 7)) {
        dash_bar_clear(a, bar->width, bar, &lit_a);
        dash_bar_set_lit(a, bar->width, bar, &lit_a, n);
        dash_bar_set_lit(b, bar->width, bar, &lit_b, bar->count);
        dash_bar_set_lit(b, bar->width, bar, &lit_b, n); /* all -> n */
        CHECK(memcmp(a, b, (size_t)bar->width * bar->height * sizeof(uint32_t)) == 0);
    }

    /* The first segment alone: its column 0 pixel is opaque and the pixel beyond the bar's last
     * segment stays clear. */
    dash_bar_clear(a, bar->width, bar, &lit_a);
    dash_bar_set_lit(a, bar->width, bar, &lit_a, 1);
    CHECK((a[(size_t)bar->segs[0].y * bar->width + bar->segs[0].x] >> 24) == 0xFFu);
    CHECK(a[(size_t)bar->segs[bar->count - 1].y * bar->width + bar->segs[bar->count - 1].x] == 0u);
}

static void test_geometry_sanity(void)
{
    /* 60 main and 16 top segments, laid out left to right with no overlap, inside their regions. */
    const dash_bar_t *bars[2] = {&dash_main_bar, &dash_top_bar};

    CHECK(dash_main_bar.count == 60 && dash_top_bar.count == 16);
    CHECK(dash_main_bar.screen_x == 10 && dash_main_bar.screen_y == 20);
    CHECK(dash_top_bar.screen_x == 10 && dash_top_bar.screen_y == 8);
    for (int b = 0; b < 2; b++) {
        for (uint8_t i = 0; i < bars[b]->count; i++) {
            const dash_seg_t *s = &bars[b]->segs[i];

            CHECK(s->x + s->ncols <= bars[b]->width);
            CHECK(s->x + s->nbase <= bars[b]->width);
            CHECK(s->y + s->h < bars[b]->height + 1u);
            if (i > 0) {
                const dash_seg_t *p = &bars[b]->segs[i - 1];
                CHECK(s->x >=
                      p->x + p->ncols); /* the next segment starts after the previous one ends */
            }
        }
    }
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    test_geometry_sanity();
    test_bar("main", &dash_main_bar, DASH_GOLDEN_MAIN_HASH, DASH_GOLDEN_MAIN_W, DASH_GOLDEN_MAIN_H);
    test_bar("top", &dash_top_bar, DASH_GOLDEN_TOP_HASH, DASH_GOLDEN_TOP_W, DASH_GOLDEN_TOP_H);

    printf("%d checks, %d failures\n", l_checks, l_failures);
    return l_failures == 0 ? 0 : 1;
}
