/* The orb: see ui_orb.h. The halo follows the shared profile's falloff (mote_look.h: alpha 0.78 x
 * (1 - t)^1.2 from the core's edge to the halo's), drawn as concentric rings of one colour, each
 * band about 3 px wide at the falloff's alpha at its middle, so the deck shows through it. The core
 * goes on top. */
#include "ui_orb.h"

#include "mote_look.h"
#include "ui_theme.h"

#define BAND_PX 3    /* about this wide a band: finer is not seen, wider steps */
#define BANDS_MIN 3  /* a small halo still falls off */
#define BANDS_MAX 12 /* size L at its flare: 7 px bands */

static void disc(lv_layer_t *l, int x, int y, int r, lv_color_t c, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = c;
    d.bg_opa = opa;
    lv_area_t a = {x - r, y - r, x + r, y + r};
    lv_draw_rect(l, &d, &a);
}

/* The profile's halo alpha (0..255) at t in thousandths, 0 at the core's edge, 1000 at the halo's.
 */
static int32_t falloff(int32_t t_pm)
{
    if (t_pm <= 0) return mote_look_falloff[0];
    if (t_pm >= 1000) return mote_look_falloff[MOTE_LOOK_FALLOFF_STEPS];
    int32_t pos = t_pm * MOTE_LOOK_FALLOFF_STEPS, i = pos / 1000, f = pos % 1000;
    return (mote_look_falloff[i] * (1000 - f) + mote_look_falloff[i + 1] * f + 500) / 1000;
}

/* A ring from r - w to r, every pixel blended once: stacked discs blend each pixel several times,
 * and in RGB565 each blend's rounding eats red and blue first, which tinted a faint halo green. */
static void ring(lv_layer_t *l, int x, int y, int r, int w, lv_color_t c, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = c;
    d.border_opa = opa;
    d.border_width = w;
    lv_area_t a = {x - r, y - r, x + r, y + r};
    lv_draw_rect(l, &d, &a);
}

void ui_orb_draw(lv_layer_t *layer, int x, int y, const mote_look_t *look, uint32_t rgb)
{
    if (!look->core_r || !look->lum) return;
    lv_color_t c = theme_dim(rgb, look->lum);
    int span = look->halo_r - look->core_r;
    if (span > 0 && look->halo_pct) {
        int n = (span + BAND_PX - 1) / BAND_PX;
        if (n < BANDS_MIN) n = BANDS_MIN;
        if (n > BANDS_MAX) n = BANDS_MAX;
        for (int j = 0; j < n;
             j++) { /* band j from the core out, at its middle t = (j + 0.5) / n */
            int r0 = look->core_r + span * j / n, r1 = look->core_r + span * (j + 1) / n;
            int32_t opa = falloff((int32_t)(2 * j + 1) * 500 / n) * look->halo_pct / 100;
            if (opa < LV_OPA_MIN || r1 <= r0) continue;
            ring(layer, x, y, r1, r1 - r0, c, (lv_opa_t)(opa > 255 ? 255 : opa));
        }
    }
    disc(layer, x, y, look->core_r, c, LV_OPA_COVER);
}

void ui_orb_area(int x, int y, const mote_look_t *look, lv_area_t *out)
{
    int r = (look->halo_r > look->core_r ? look->halo_r : look->core_r) + 2;
    *out = (lv_area_t){x - r, y - r, x + r, y + r};
}
