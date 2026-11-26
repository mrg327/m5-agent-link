/* The orb: see ui_orb.h. The halo is seven discs, outermost first, each a little smaller and more
 * opaque; on black they add up to a coverage of about 8, 18, 30, 43, 57, 71 and 86 % from the rim
 * in, a smooth falloff that reaches nothing at halo_r. The core goes on top. */
#include "ui_orb.h"

#include "ui_theme.h"

#define HALO_DISCS 7

/* Each disc's opacity of 255: 1 - (1 - T_j) / (1 - T_j-1) for the coverages above. */
static const uint8_t s_disc_opa[HALO_DISCS] = {20, 28, 37, 47, 63, 83, 132};

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

void ui_orb_draw(lv_layer_t *layer, int x, int y, const mote_look_t *look, uint32_t rgb)
{
    if (!look->core_r || !look->lum) return;
    int span = look->halo_r - look->core_r;
    if (span > 0 && look->halo_pct) {
        lv_color_t c = theme_dim(rgb, look->lum * look->halo_pct / 100);
        for (int j = 0; j < HALO_DISCS; j++) {
            disc(layer, x, y, look->core_r + span * (HALO_DISCS - j) / HALO_DISCS, c,
                 s_disc_opa[j]);
        }
    }
    disc(layer, x, y, look->core_r, theme_dim(rgb, look->lum), LV_OPA_COVER);
}

void ui_orb_area(int x, int y, const mote_look_t *look, lv_area_t *out)
{
    int r = (look->halo_r > look->core_r ? look->halo_r : look->core_r) + 2;
    *out = (lv_area_t){x - r, y - r, x + r, y + r};
}
