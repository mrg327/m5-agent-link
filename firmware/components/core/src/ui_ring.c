/* The index ring object: segments and their animation clock, the SCROLL track, the hold progress arc
 * and the alert glow, drawn by a single custom object. One draw callback instead of N full-screen arc
 * widgets keeps the render cost and RAM low, and every change invalidates only the thin arc it
 * touches (in chunks, so a long arc never turns into a near full-screen bounding box). The painting
 * of each shape is in ui_ring_draw.c. */
#include "ui_ring_internal.h"

#include <math.h>
#include <string.h>

void ring_seg_span(int n, int i, int *a0, int *a1)
{
    if (n <= 1) {
        *a0 = 0;
        *a1 = 360;
        return;
    }
    int seg = 360 * 100 / n;
    int gap = SEG_GAP_DEG < seg / 300 ? SEG_GAP_DEG : seg / 300;
    *a0 = i * seg / 100 + gap / 2;
    *a1 = (i + 1) * seg / 100 - gap / 2;
}

/* Bounding box of an arc chunk (degrees clockwise from 12 o'clock) over the ring band. */
static void chunk_area(int a0, int a1, lv_area_t *out)
{
    int x1 = 9999, y1 = 9999, x2 = -9999, y2 = -9999;
    for (int a = a0;; a += 6) {
        if (a > a1) a = a1;
        float rad = (float)a * (float)M_PI / 180.0f;
        float sn = sinf(rad), cs = cosf(rad);
        for (int k = 0; k < 2; k++) {
            int r = k ? INV_R_OUT : INV_R_IN;
            int x = CX + (int)lroundf((float)r * sn), y = CY - (int)lroundf((float)r * cs);
            if (x < x1) x1 = x;
            if (x > x2) x2 = x;
            if (y < y1) y1 = y;
            if (y > y2) y2 = y;
        }
        if (a == a1) break;
    }
    out->x1 = x1 - 2;
    out->y1 = y1 - 2;
    out->x2 = x2 + 2;
    out->y2 = y2 + 2;
}

static void inval_arc(lv_obj_t *o, int a0, int a1)
{
    if (a1 <= a0) return;
    for (int a = a0; a < a1; a += 30) {
        lv_area_t ar;
        chunk_area(a, a + 30 < a1 ? a + 30 : a1, &ar);
        lv_obj_invalidate_area(o, &ar);
    }
}

static void inval_seg(lv_obj_t *o, int n, int i)
{
    int a0, a1;
    ring_seg_span(n, i, &a0, &a1);
    inval_arc(o, a0 - 1, a1 + 1);
}

static void inval_all(lv_obj_t *o)
{
    inval_arc(o, 0, 360);
}

static bool seg_visible(const lv_layer_t *l, int a0, int a1)
{
    for (int a = a0; a < a1; a += 30) {
        lv_area_t ar;
        const lv_area_t *c = &l->_clip_area;
        chunk_area(a, a + 30 < a1 ? a + 30 : a1, &ar);
        if (ar.x1 <= c->x2 && ar.x2 >= c->x1 && ar.y1 <= c->y2 && ar.y2 >= c->y1) return true;
    }
    return false;
}

static void on_draw(lv_event_t *e)
{
    ring_data_t *d = lv_event_get_user_data(e);
    lv_layer_t *l = lv_event_get_layer(e);

    if (d->solid) {
        ring_arc(l, BAND_OUT, GLOW_W, 0, 360, d->solid_color);
        return;
    }
    if (d->track) {
        ring_arc(l, BAND_OUT, TRACK_RING_W, 0, 360, ring_mix(0x8b949e, 45)); /* muted grey: visible against the bezel */
        int span = d->track_permille * 360 / 1000;
        if (span > 0) ring_arc(l, BAND_OUT, TRACK_RING_W, 0, span < 360 ? span : 360, lv_color_hex(d->track_rgb));
        return;
    }
    if (d->n == 0) {
        ring_arc(l, BAND_OUT, 3, 0, 360, ring_mix(0x30363d, d->ambient ? 40 : 100));
    }
    for (int i = 0; i < d->n; i++) {
        int a0, a1;
        ring_seg_span(d->n, i, &a0, &a1);
        if (seg_visible(l, a0, a1)) ring_draw_slot(l, d, i);
    }
    if (d->prog > 0) ring_arc(l, PROG_OUT, PROG_W, 0, d->prog * 360 / 1000, lv_color_hex(d->prog_rgb));
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

void ring_set(lv_obj_t *ring, const ring_seg_t *segs, int n, uint32_t now_ms)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    if (n > RING_MAX_SEGMENTS) n = RING_MAX_SEGMENTS;
    bool restore = d->solid || d->track;
    d->solid = d->track = false;
    if (restore || n != d->n) {
        for (int i = 0; i < n; i++) {
            bool keep = i < d->n && d->seg[i].s.id == segs[i].id && d->seg[i].s.kind == segs[i].kind;
            d->seg[i].s = segs[i];
            if (!keep) d->seg[i].entry_ms = now_ms;
            {
                int a0, a1;
                ring_seg_span(n, i, &a0, &a1);
                d->seg[i].lvl = (int16_t)ring_anim_level(d, &d->seg[i], a1 - a0, now_ms);
            }
        }
        d->n = n;
        inval_all(ring);
        return;
    }
    for (int i = 0; i < n; i++) {
        ring_slot_t *s = &d->seg[i];
        if (s->s.id == segs[i].id && s->s.kind == segs[i].kind && s->s.rgb == segs[i].rgb &&
            s->s.selected == segs[i].selected && s->s.dim == segs[i].dim)
            continue;
        if (s->s.id != segs[i].id || s->s.kind != segs[i].kind) s->entry_ms = now_ms;
        s->s = segs[i];
        int a0, a1;
        ring_seg_span(n, i, &a0, &a1);
        s->lvl = (int16_t)ring_anim_level(d, s, a1 - a0, now_ms);
        inval_seg(ring, n, i);
    }
}

void ring_set_solid(lv_obj_t *ring, lv_color_t color)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    if (d->solid && lv_color_eq(d->solid_color, color)) return;
    d->solid = true;
    d->solid_color = color;
    inval_all(ring);
}

void ring_set_track(lv_obj_t *ring, int permille, uint32_t rgb)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    permille = permille < 0 ? 0 : permille > 1000 ? 1000 : permille / 8 * 8; /* ~3° steps */
    if (d->track && d->track_permille == permille && d->track_rgb == rgb) return;
    int lo = d->track ? (d->track_permille < permille ? d->track_permille : permille) : 0;
    int hi = d->track ? (d->track_permille < permille ? permille : d->track_permille) : 1000;
    if (d->track && rgb != d->track_rgb) lo = 0, hi = 1000;
    d->track = true;
    d->solid = false;
    d->track_permille = permille;
    d->track_rgb = rgb;
    inval_arc(ring, lo * 360 / 1000 - 1, hi * 360 / 1000 + 2);
}

void ring_set_progress(lv_obj_t *ring, int permille, uint32_t rgb)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    permille = permille / 8 * 8; /* about 3° steps: no repaint per millisecond */
    if (permille == d->prog && rgb == d->prog_rgb) return;
    int lo = d->prog < permille ? d->prog : permille, hi = d->prog < permille ? permille : d->prog;
    if (rgb != d->prog_rgb) lo = 0, hi = d->prog > permille ? d->prog : permille;
    d->prog = permille;
    d->prog_rgb = rgb;
    inval_arc(ring, lo * 360 / 1000 - 1, hi * 360 / 1000 + 2);
}

/* Re-quantise every slot's animation state for the current look (still or ambient), then repaint. */
static void relevel_all(lv_obj_t *ring, ring_data_t *d)
{
    for (int i = 0; i < d->n; i++) {
        int a0, a1;
        ring_seg_span(d->n, i, &a0, &a1);
        d->seg[i].lvl = (int16_t)ring_anim_level(d, &d->seg[i], a1 - a0, d->last_tick);
    }
    inval_all(ring);
}

void ring_set_still(lv_obj_t *ring, bool still)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    if (d->still == still) return;
    d->still = still;
    relevel_all(ring, d);
}

void ring_set_ambient(lv_obj_t *ring, bool ambient)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    if (d->ambient == ambient) return;
    d->ambient = ambient;
    relevel_all(ring, d);
}

void ring_tick(lv_obj_t *ring, uint32_t now_ms)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    if (d->solid || d->track || d->ambient || d->still || d->n == 0) return;
    if (d->have_tick && now_ms - d->last_tick < TICK_MS) return;
    d->have_tick = true;
    d->last_tick = now_ms;
    for (int i = 0; i < d->n; i++) {
        ring_slot_t *s = &d->seg[i];
        if (s->s.kind == RING_IDLE) continue;
        int a0, a1;
        ring_seg_span(d->n, i, &a0, &a1);
        int lvl = ring_anim_level(d, s, a1 - a0, now_ms);
        if (lvl == s->lvl) continue;
        int old = s->lvl;
        s->lvl = (int16_t)lvl;
        if (s->s.kind == RING_WORK) { /* only the comet's old and new spots */
            int span = a1 - a0, len = span / 2 < COMET_DEG ? span / 2 : COMET_DEG;
            inval_arc(ring, a0 + old * COMET_STEP_DEG - 1, a0 + old * COMET_STEP_DEG + len + 1);
            inval_arc(ring, a0 + lvl * COMET_STEP_DEG - 1, a0 + lvl * COMET_STEP_DEG + len + 1);
        } else {
            inval_seg(ring, d->n, i);
        }
    }
}

int ring_hit(lv_obj_t *ring, int x, int y)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    float dx = (float)(x - CX), dy = (float)(y - CY);
    if (d->n == 0 || dx * dx + dy * dy < 150.0f * 150.0f) return -1;
    float deg = atan2f(dy, dx) * 180.0f / (float)M_PI;
    deg = fmodf(deg + 360.0f + 90.0f, 360.0f);
    int idx = (int)(deg / (360.0f / (float)d->n));
    return idx < d->n ? idx : d->n - 1;
}
