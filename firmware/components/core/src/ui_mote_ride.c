/* The Mote riding an arc: see ui_mote_ride.h. The object is exactly the orb's box (ui_orb_area),
 * so LVGL repaints the old and the new box on a move and nothing else; the orb is painted with
 * ui_orb_draw() at the box's centre. */
#include "ui_mote_ride.h"

#include <math.h>

#include "mote_look.h"
#include "ui_orb.h"

static void ride_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    const ui_mote_ride_t *r = lv_obj_get_user_data(o);
    if (!r || !r->shown) return;
    lv_area_t c;
    lv_obj_get_coords(o, &c);
    ui_orb_draw(lv_event_get_layer(e), (c.x1 + c.x2) / 2, (c.y1 + c.y2) / 2, &r->look, MOTE_RGB);
}

void ui_mote_ride_create(ui_mote_ride_t *r, lv_obj_t *parent)
{
    *r = (ui_mote_ride_t){0};
    r->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(r->obj);
    lv_obj_remove_flag(r->obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(r->obj, 1, 1);
    lv_obj_add_flag(r->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_user_data(r->obj, r);
    lv_obj_add_event_cb(r->obj, ride_draw, LV_EVENT_DRAW_MAIN, NULL);
}

mote_look_t ui_mote_ride_look(uint16_t scale_pct, uint8_t lum)
{
    /* the brightness and the halo's strength from the shared look, held inhaled; the size is the
     * profile's tucked one, made to sit beside a thin arc */
    mote_look_t k = mote_look(100, lum, 1000);
    int core = (MOTE_LOOK_TUCKED_CORE_D * (int)scale_pct + 100) / 200;
    int halo = (MOTE_LOOK_TUCKED_HALO_D * (int)scale_pct + 100) / 200;
    if (core < 1) core = 1;
    if (halo <= core) halo = core + 1;
    k.core_r = (uint8_t)core;
    k.halo_r = (uint8_t)halo;
    return k;
}

uint16_t ui_mote_ride_flare(uint32_t t_ms, bool *over)
{
    const uint32_t rise = MOTE_LOOK_FLARE_RISE_MS, fall = MOTE_LOOK_FLARE_FALL_MS;
    const int extra = MOTE_LOOK_FLARE_SCALE_PCT - 100;
    if (over) *over = t_ms >= rise + fall;
    if (t_ms < rise) return (uint16_t)(100 + extra * (int)t_ms / (int)rise);
    if (t_ms < rise + fall) return (uint16_t)(100 + extra * (int)(rise + fall - t_ms) / (int)fall);
    return 100;
}

static bool same_look(const mote_look_t *a, const mote_look_t *b)
{
    return a->core_r == b->core_r && a->halo_r == b->halo_r && a->lum == b->lum &&
           a->halo_pct == b->halo_pct;
}

void ui_mote_ride_at(ui_mote_ride_t *r, int cx, int cy, int radius, int deg,
                     const mote_look_t *look)
{
    if (!r->obj) return;
    float a = (float)deg * 0.017453292f;
    int x = cx + (int)lroundf((float)radius * sinf(a));
    int y = cy - (int)lroundf((float)radius * cosf(a));
    if (r->shown && x == r->x && y == r->y && same_look(look, &r->look)) return;
    bool resized = !r->shown || look->core_r != r->look.core_r || look->halo_r != r->look.halo_r;
    bool restyled = r->shown && !same_look(look, &r->look);
    r->look = *look;
    r->x = (int16_t)x;
    r->y = (int16_t)y;
    lv_area_t box;
    ui_orb_area(x, y, look, &box);
    /* moving or resizing the box repaints it, old and new; a new brightness in the same box
     * repaints it once */
    if (resized) lv_obj_set_size(r->obj, lv_area_get_width(&box), lv_area_get_height(&box));
    lv_obj_set_pos(r->obj, box.x1, box.y1);
    if (!r->shown) {
        r->shown = true;
        lv_obj_remove_flag(r->obj, LV_OBJ_FLAG_HIDDEN);
    } else if (restyled && !resized) {
        lv_obj_invalidate(r->obj);
    }
}

void ui_mote_ride_hide(ui_mote_ride_t *r)
{
    if (!r->obj || !r->shown) return;
    r->shown = false;
    lv_obj_add_flag(r->obj, LV_OBJ_FLAG_HIDDEN);
}
