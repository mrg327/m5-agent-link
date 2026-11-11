/* The index ring object: segments and their animation clock, the SCROLL track, the hold progress arc
 * and the alert glow, drawn by a single custom object. One draw callback instead of N full-screen arc
 * widgets keeps the render cost and RAM low, and every change invalidates only the thin arc it
 * touches (in chunks, so a long arc never turns into a near full-screen bounding box). The painting
 * of each shape is in ui_ring_draw.c. */
#include "ui_ring_internal.h"

#include "power.h"
#include "ui_theme.h"

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
    int inset = gap / 2 + CAP_DEG; /* the arc itself; its round caps reach into the gap */
    *a0 = i * seg / 100 + inset;
    *a1 = (i + 1) * seg / 100 - inset;
}

void ring_comet_span(int a0, int a1, int lvl, int *c0, int *c1)
{
    int span = a1 - a0, len = span / 2 < COMET_DEG ? span / 2 : COMET_DEG;
    *c0 = a0 + lvl * COMET_STEP_DEG;
    *c1 = *c0 + len < a1 ? *c0 + len : a1;
}

/* Bounding box of the band r_in..r_out between a0 and a1 (degrees clockwise from 12 o'clock): the
 * corners plus every 3, 6, 9 and 12 o'clock extreme inside the range, 2 px wider for anti-aliasing. */
static void arc_area(float a0, float a1, int r_in, int r_out, lv_area_t *out)
{
    float x1 = 1e9f, y1 = 1e9f, x2 = -1e9f, y2 = -1e9f;
    float a[8];
    int n = 0;
    a[n++] = a0;
    a[n++] = a1;
    for (float q = ceilf(a0 / 90.0f) * 90.0f; q < a1 && n < 8; q += 90.0f) a[n++] = q;
    for (int i = 0; i < n; i++) {
        float rad = a[i] * (float)M_PI / 180.0f, sn = sinf(rad), cs = cosf(rad);
        for (int k = 0; k < 2; k++) {
            float r = (float)(k ? r_out : r_in), x = CX + r * sn, y = CY - r * cs;
            if (x < x1) x1 = x;
            if (x > x2) x2 = x;
            if (y < y1) y1 = y;
            if (y > y2) y2 = y;
        }
    }
    out->x1 = (int32_t)floorf(x1) - 2;
    out->y1 = (int32_t)floorf(y1) - 2;
    out->x2 = (int32_t)ceilf(x2) + 2;
    out->y2 = (int32_t)ceilf(y2) + 2;
}

/* In 30 degree chunks over the band, so a long arc never becomes a near full-screen box. */
static void inval_band(lv_obj_t *o, float a0, float a1, int r_in, int r_out)
{
    for (float a = a0; a < a1; a += 30.0f) {
        lv_area_t ar;
        arc_area(a, a + 30.0f < a1 ? a + 30.0f : a1, r_in, r_out, &ar);
        lv_obj_invalidate_area(o, &ar);
    }
}

static void inval_arc(lv_obj_t *o, int a0, int a1)
{
    if (a1 > a0) inval_band(o, (float)a0, (float)a1, INV_R_IN, INV_R_OUT);
}

/* Degrees a round cap of a capsule out/w reaches past its end angle, plus a margin. */
static float cap_deg(int out, int w)
{
    return asinf((float)w / 2.0f / (float)(out - w / 2)) * 180.0f / (float)M_PI + 0.5f;
}

static void inval_seg(lv_obj_t *o, int n, int i)
{
    int a0, a1;
    ring_seg_span(n, i, &a0, &a1);
    float cap = cap_deg(SEL_OUT, SEL_W);
    inval_band(o, (float)a0 - cap, (float)a1 + cap, INV_R_IN, INV_R_OUT);
}

static void inval_all(lv_obj_t *o)
{
    inval_arc(o, 0, 360);
}

/* Only what a comet step changes: the tail it leaves and the head it gains, each with its round cap,
 * on the capsule's own band. A jump (the wrap back to the start) repaints both whole comets. */
static void inval_comet(lv_obj_t *o, const ring_data_t *d, int i, int a0, int a1, int old, int lvl)
{
    bool sel = d->seg[i].s.selected && d->n > 1;
    int out = sel ? SEL_OUT : BAND_OUT, w = sel ? SEL_W : BAND_W;
    float cap = cap_deg(out, w);
    int o0, o1, n0, n1;
    ring_comet_span(a0, a1, old, &o0, &o1);
    ring_comet_span(a0, a1, lvl, &n0, &n1);
    if ((o0 > n0 ? o0 : n0) < (o1 < n1 ? o1 : n1)) {
        if (o0 != n0) inval_band(o, (float)(o0 < n0 ? o0 : n0) - cap, (float)(o0 < n0 ? n0 : o0) + cap, out - w, out);
        if (o1 != n1) inval_band(o, (float)(o1 < n1 ? o1 : n1) - cap, (float)(o1 < n1 ? n1 : o1) + cap, out - w, out);
    } else {
        inval_band(o, (float)o0 - cap, (float)o1 + cap, out - w, out);
        inval_band(o, (float)n0 - cap, (float)n1 + cap, out - w, out);
    }
}

static bool seg_visible(const lv_layer_t *l, int a0, int a1)
{
    for (int a = a0; a < a1; a += 30) {
        lv_area_t ar;
        const lv_area_t *c = &l->_clip_area;
        arc_area((float)a, (float)(a + 30 < a1 ? a + 30 : a1), INV_R_IN, INV_R_OUT, &ar);
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
        ring_arc(l, BAND_OUT - 1, TRACK_RING_W, 0, 360, COL_FILL2);
        int span = d->track_permille * 360 / 1000;
        if (span > 0) ring_arc(l, BAND_OUT - 1, TRACK_RING_W, 0, span < 360 ? span : 360, lv_color_hex(d->track_rgb));
        return;
    }
    if (d->n == 0) {
        ring_arc(l, BAND_OUT - 2, 3, 0, 360, COL_FILL2);
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
    bool edge = (permille > 0) != (d->prog > 0);
    d->prog = permille;
    d->prog_rgb = rgb;
    inval_arc(ring, lo * 360 / 1000 - 1, hi * 360 / 1000 + 2);
    if (edge) /* the selected capsule dims for the hold and recovers after it */
        for (int i = 0; i < d->n; i++)
            if (d->seg[i].s.selected) inval_seg(ring, d->n, i);
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
    if (d->solid || d->track || d->ambient || d->still || d->n == 0 || !power_display_on()) return; /* dark: no motion */
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
        if (s->s.kind == RING_WORK) {
            inval_comet(ring, d, i, a0, a1, old, lvl);
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
