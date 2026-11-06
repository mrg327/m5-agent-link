#include "ui_ring.h"

#include <math.h>

#include "ui_theme.h"

#define RING_OUTER_R 230
#define RING_WIDTH 10
#define RING_GAP_DEG 4

typedef struct {
    lv_color_t colors[RING_MAX_SEGMENTS];
    int n;
} ring_data_t;

static void on_draw(lv_event_t *e)
{
    ring_data_t *d = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center.x = a.x1 + lv_area_get_width(&a) / 2;
    dsc.center.y = a.y1 + lv_area_get_height(&a) / 2;
    dsc.radius = RING_OUTER_R;
    dsc.width = RING_WIDTH;

    if (d->n == 0) {
        dsc.color = lv_color_hex(0x21262d);
        dsc.start_angle = 0;
        dsc.end_angle = 359;
        lv_draw_arc(layer, &dsc);
        return;
    }
    int seg = 360 / d->n;
    int gap = d->n > 1 ? RING_GAP_DEG : 0;
    for (int i = 0; i < d->n; i++) {
        int start = 270 + i * seg + gap / 2;
        int end = start + seg - gap - (d->n == 1 ? 1 : 0);
        dsc.color = d->colors[i];
        dsc.start_angle = (lv_value_precise_t)(start % 360);
        dsc.end_angle = (lv_value_precise_t)(end % 360);
        lv_draw_arc(layer, &dsc);
    }
}

lv_obj_t *ring_create(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 466, 466);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    ring_data_t *d = lv_malloc_zeroed(sizeof *d); /* lives as long as the screen: never freed */
    lv_obj_add_event_cb(o, on_draw, LV_EVENT_DRAW_MAIN, d);
    lv_obj_set_user_data(o, d);
    return o;
}

void ring_set(lv_obj_t *ring, const lv_color_t *colors, int n)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    if (n > RING_MAX_SEGMENTS) n = RING_MAX_SEGMENTS;
    bool same = d->n == n;
    for (int i = 0; same && i < n; i++) same = lv_color_eq(d->colors[i], colors[i]);
    if (same) return; /* avoid invalidating the whole screen for nothing */
    for (int i = 0; i < n; i++) d->colors[i] = colors[i];
    d->n = n;
    lv_obj_invalidate(ring);
}

void ring_set_solid(lv_obj_t *ring, lv_color_t color)
{
    ring_set(ring, &color, 1);
}

int ring_hit(lv_obj_t *ring, int x, int y)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    float dx = (float)(x - 233), dy = (float)(y - 233);
    float dist = sqrtf(dx * dx + dy * dy);
    if (d->n == 0 || dist < 150.0f) return -1; /* generous: the band itself is only 10 px thick */
    float deg = atan2f(dy, dx) * 180.0f / (float)M_PI; /* 0 = 3 o'clock, clockwise positive */
    deg = fmodf(deg + 360.0f + 90.0f, 360.0f);         /* 0 = 12 o'clock */
    int idx = (int)(deg / (360.0f / (float)d->n));
    return idx < d->n ? idx : d->n - 1;
}
