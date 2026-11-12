/* The answer window's countdown (ANSWER-WINDOW.md §5): a thin ring just inside the index ring that
 * shortens toward 12 o'clock as the time to answer on the watch runs out. Shown with the request it
 * belongs to (ASK, and its card on the deck), nowhere else. No text: the ring is the whole readout.
 *
 * Geometry: centre line r 214.5, 3 px wide (r 213-216), round caps. That band is free on every screen: the
 * content and the needs-you pips stay inside r 210, the legends' farthest corners sit at r 212 and the
 * ring starts at r 218. The hold progress draws over the ring itself (r 218-233), so the two never meet.
 * Chrome grey; white in the last COUNTDOWN_LATE_MS, so the end is not a surprise.
 *
 * Redraw discipline (UI-V2 §8a): the arc moves in whole degrees and only the two ends that moved are
 * invalidated, so a minute's countdown costs a few small redraws a second. */
#include <math.h>

#include "ui_countdown.h"
#include "ui_internal.h"
#include "ui_theme.h"

#define CD_R 215 /* lv_draw_arc radius is the outer edge: 216 - 1 */
#define CD_W 3
#define COUNTDOWN_LATE_MS 10000

static lv_obj_t *s_obj;
static int s_half = -1; /* half the arc's span in degrees (0..180); -1 = hidden */
static bool s_late;
static uint32_t s_rid, s_left_ms;

/* A point on the arc's centre line, `deg` clockwise from 12 o'clock. */
static lv_point_t at(int deg)
{
    double r = CD_R - CD_W / 2.0, a = deg * M_PI / 180.0;
    return (lv_point_t){(int32_t)lround(UI_CX + r * sin(a)), (int32_t)lround(UI_CX - r * cos(a))};
}

/* Invalidates the part of the ring between `d0` and `d1` degrees (both sides of 12 o'clock). */
static void invalidate_span(int d0, int d1)
{
    if (d0 > d1) {
        int t = d0;
        d0 = d1;
        d1 = t;
    }
    for (int side = -1; side <= 1; side += 2) {
        lv_area_t a = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
        for (int d = d0; d <= d1; d++) {
            lv_point_t p = at(side * d);
            if (p.x < a.x1) a.x1 = p.x;
            if (p.y < a.y1) a.y1 = p.y;
            if (p.x > a.x2) a.x2 = p.x;
            if (p.y > a.y2) a.y2 = p.y;
        }
        int m = CD_W + 2; /* the round cap and anti-aliasing */
        a.x1 -= m;
        a.y1 -= m;
        a.x2 += m;
        a.y2 += m;
        /* the object covers the whole screen at (0, 0): screen and object coordinates agree */
        lv_obj_invalidate_area(s_obj, &a);
    }
}

static void on_draw(lv_event_t *e)
{
    if (s_half <= 0) return;
    lv_layer_t *l = lv_event_get_layer(e);
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = UI_CX;
    d.center.y = UI_CX;
    d.radius = CD_R + 1;
    d.width = CD_W;
    d.color = s_late ? COL_TEXT : COL_TEXT2;
    d.rounded = 1;
    int half = s_half >= 180 ? 179 : s_half; /* a whole circle would need start == end, which draws nothing */
    d.start_angle = (lv_value_precise_t)(270 - half);
    d.end_angle = (lv_value_precise_t)(270 + half);
    lv_draw_arc(l, &d);
}

void ui_countdown_init(lv_obj_t *screen)
{
    s_obj = lv_obj_create(screen);
    lv_obj_remove_style_all(s_obj);
    lv_obj_set_size(s_obj, UI_SIZE, UI_SIZE);
    lv_obj_set_pos(s_obj, 0, 0);
    lv_obj_remove_flag(s_obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_obj, on_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_obj, LV_OBJ_FLAG_HIDDEN);
}

void ui_countdown_show(const attention_t *a, uint32_t now_ms)
{
    uint32_t left = a ? model_attention_left_ms(a, now_ms) : UINT32_MAX;
    if (!a || a->answer_sent || !a->win_ms || left == UINT32_MAX) {
        if (s_half >= 0) {
            s_half = -1;
            s_rid = 0;
            lv_obj_add_flag(s_obj, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    int half = (int)((uint64_t)left * 180 / a->win_ms);
    if (half > 180) half = 180;
    bool late = left <= COUNTDOWN_LATE_MS;
    s_left_ms = left;
    if (s_half < 0 || a->rid != s_rid) { /* appears: one full redraw */
        s_rid = a->rid;
        s_half = half;
        s_late = late;
        lv_obj_remove_flag(s_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s_obj);
        return;
    }
    if (late != s_late) {
        s_late = late;
        invalidate_span(0, half > s_half ? half : s_half);
    } else if (half != s_half) {
        invalidate_span(half, s_half);
    }
    s_half = half;
}

int ui_countdown_left_s(void) { return s_half < 0 ? -1 : (int)((s_left_ms + 999) / 1000); }

void ui_countdown_report(ui_report_t *r)
{
    if (s_half < 0) return;
    ui_report_add(r, "countdown %u s", (unsigned)((s_left_ms + 999) / 1000));
}
