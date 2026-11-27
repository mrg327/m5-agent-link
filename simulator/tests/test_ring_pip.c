/* Where a needs-you pip sits on its capsule (ui_ring_internal.h ring_pip_deg). Issue 20261126-54d7:
 * with four sessions the fourth's capsule is centred on 315°, and its pip at r 205 landed under
 * key A's hint (r 187, ring r 20), which painted over it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "power.h"
#include "ui_kit.h"
#include "ui_ring_internal.h"
#include "ui_theme.h"

/* ── stubs: the ring's painting is linked, never run ── */
const lv_font_t font_figtree_20_400;
lv_color_t theme_dim(uint32_t rgb, int pct)
{
    (void)pct;
    return lv_color_hex(rgb);
}
bool power_display_on(void) { return true; }

static int g_fail;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail = 1;                                                  \
        }                                                                \
    } while (0)

#define HINT_R 20

/* Pixels between a pip's centre at angle deg and a hint's centre. */
static double gap_px(int deg, int hx, int hy)
{
    double rad = deg * M_PI / 180.0;
    double x = CX + PIP_R * sin(rad), y = CY - PIP_R * cos(rad);
    return hypot(x - hx, y - hy);
}

static double nearest_hint_px(int deg)
{
    double a = gap_px(deg, UI_KEY_A_X, UI_KEY_Y), b = gap_px(deg, UI_KEY_B_X, UI_KEY_Y);
    return a < b ? a : b;
}

int main(void)
{
    /* closer than this, the hint covers the pip: its farthest point is the triangle's base corner
     * (issue 20261126-38fa), half the base along the ring and half the size outward */
    const double reach = hypot(PIP_D * PIP_TRI_BASE_PCT / 200.0, PIP_D / 2.0);
    const double clear = HINT_R + reach;
    for (int n = 2; n <= RING_MAX_SEGMENTS; n++) {
        for (int i = 0; i < n; i++) {
            int a0, a1;
            ring_seg_span(n, i, &a0, &a1);
            int mid = (a0 + a1) / 2, p = ring_pip_deg(a0, a1);
            CHECK(p >= a0 && p <= a1);                         /* on its own capsule */
            CHECK(nearest_hint_px(p) >= nearest_hint_px(mid)); /* never closer than before */
            /* up to 12 sessions a capsule always has the room: clear of both hints */
            if (n <= 12 && nearest_hint_px(p) <= clear) {
                fprintf(stderr, "n %d seg %d: pip at %d deg is %.1f px from a hint\n", n, i, p,
                        nearest_hint_px(p));
                CHECK(0);
            }
            /* a pip far from both hints stays at its capsule's midpoint */
            if (abs(mid - PIP_KEY_A_DEG) >= PIP_KEEP_OUT_DEG &&
                abs(mid - PIP_KEY_B_DEG) >= PIP_KEEP_OUT_DEG) {
                CHECK(p == mid);
            }
        }
    }
    /* the case the issue found: four sessions, the fourth centred under key A */
    int a0, a1;
    ring_seg_span(4, 3, &a0, &a1);
    CHECK((a0 + a1) / 2 == PIP_KEY_A_DEG);
    int p = ring_pip_deg(a0, a1);
    CHECK(abs(p - PIP_KEY_A_DEG) >= PIP_KEEP_OUT_DEG);
    CHECK(gap_px(p, UI_KEY_A_X, UI_KEY_Y) > clear + 5); /* the triangle too, with room */
    if (!g_fail) printf("ring pips clear of the key hints\n");
    return g_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
