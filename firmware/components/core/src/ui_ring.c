#include "ui_ring.h"

#include <math.h>
#include <string.h>

#define CX 233
#define CY 233
/* Index ring (UI-V2 §4.1, "capsules"): every segment is a round-capped capsule on a thin band at the
 * rim with a clear gap to its neighbours. The selected one is a wider capsule grown both ways around
 * the same centre line, with a thin white underline, and the others recede a little. Error is a row
 * of round dots and permission two thin round tracks, so the two never read alike beside each other. */
#define BAND_OUT 226 /* outer radius of a normal capsule */
#define BAND_W 8
#define SEL_OUT 229 /* the selected capsule: wider, centred on the same line (229 - 14/2 == 226 - 8/2) */
#define SEL_W 14
#define SEG_GAP_DEG 9   /* gap between capsules (shrinks when sectors get narrow) */
#define OTHERS_PCT 70   /* brightness of the non-selected capsules */
#define UNDERLINE_W 2   /* selection underline, just inside the selected capsule */
#define UNDERLINE_GAP 3
#define DOT_STEP_DEG 7  /* error: one round dot every this many degrees */
#define TRACK_W 3       /* permission: two thin tracks on the capsule's outer and inner edge */
#define SEL_TRACK_W 4
#define IDLE_W 4
#define GLOW_W 14     /* ring_set_solid: the display-off alert glow is wider than the index band */
#define PROG_OUT 212  /* hold-to-confirm fill, just inside the band */
#define PROG_W 8
#define INV_R_IN 190 /* every invalidated chunk covers this radius band */
#define INV_R_OUT 233
#define TICK_MS 30

#define WORK_PERIOD_MS 1400
#define ASK_PERIOD_MS 1400
#define PERM_PERIOD_MS 900
#define DONE_SWEEP_MS 700
#define ERROR_FLICKER_MS 600
#define COMET_DEG 14
#define COMET_STEP_DEG 4
#define PULSE_LEVELS 6
#define DONE_STEPS 12

typedef struct {
    ring_seg_t s;
    uint32_t entry_ms;
    int16_t lvl; /* quantised animation state last drawn: a repaint is only needed when it changes */
} slot_t;

typedef struct {
    slot_t seg[RING_MAX_SEGMENTS];
    int n;
    bool ambient, still, solid; /* still: show the settled look, no motion */
    lv_color_t solid_color;
    int prog;
    uint32_t prog_rgb;
    uint32_t last_tick;
    bool have_tick;
} ring_data_t;

static lv_color_t mix(uint32_t rgb, int pct);
/* mix() scaled by k percent: the non-selected capsules recede without losing their hue. */
static lv_color_t mixk(int k, uint32_t rgb, int pct) { return mix(rgb, pct * k / 100); }

static lv_color_t mix(uint32_t rgb, int pct)
{
    return lv_color_mix(lv_color_hex(rgb), lv_color_black(), (uint8_t)(255 * pct / 100));
}

static void seg_span(int n, int i, int *a0, int *a1)
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
    seg_span(n, i, &a0, &a1);
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

static void arc(lv_layer_t *l, int r_out, int w, int a0, int a1, lv_color_t c)
{
    if (a1 <= a0 || w <= 0) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = CX;
    d.center.y = CY;
    d.radius = (uint16_t)r_out;
    d.width = (uint16_t)w;
    d.color = c;
    d.rounded = a1 - a0 < 360; /* capsule ends; a full ring has none */
    if (a1 - a0 >= 360) { /* two overlapping halves: a single 0..359 arc leaves a visible seam */
        d.start_angle = 0;
        d.end_angle = 181;
        lv_draw_arc(l, &d);
        d.start_angle = 180;
        d.end_angle = 1;
    } else {
        d.start_angle = (lv_value_precise_t)((a0 + 270) % 360);
        d.end_angle = (lv_value_precise_t)((a1 + 270) % 360);
    }
    lv_draw_arc(l, &d);
}

static int tri_level(uint32_t t, uint32_t period, int levels)
{
    uint32_t p = t % period * 2;
    if (p > period) p = 2 * period - p; /* 0..period..0 */
    return (int)(p * (uint32_t)levels / period);
}

/* Animation state for a slot at time now; the value is only compared, never shown. */
static int anim_level(const ring_data_t *d, const slot_t *s, int span, uint32_t now)
{
    if (d->ambient || d->still) {
        switch (s->s.kind) {
        case RING_WORK: return span / 2 / COMET_STEP_DEG;
        case RING_ASK:
        case RING_PERM: return PULSE_LEVELS - 1;
        case RING_DONE: return DONE_STEPS;
        case RING_ERROR: return 2;
        default: return 0;
        }
    }
    uint32_t t = now - s->entry_ms;
    switch (s->s.kind) {
    case RING_WORK: return (int)(now % WORK_PERIOD_MS * (uint32_t)span / WORK_PERIOD_MS / COMET_STEP_DEG);
    case RING_ASK: return tri_level(now, ASK_PERIOD_MS, PULSE_LEVELS);
    case RING_PERM: return tri_level(now, PERM_PERIOD_MS, PULSE_LEVELS);
    case RING_DONE: return t >= DONE_SWEEP_MS ? DONE_STEPS : (int)(t * DONE_STEPS / DONE_SWEEP_MS);
    case RING_ERROR: return t >= ERROR_FLICKER_MS ? 2 : (int)(t / 100 % 2);
    default: return 0;
    }
}

/* A filled circle of diameter `diam` centred at polar (r_mid, deg clockwise from 12 o'clock). */
static void dot(lv_layer_t *l, int r_mid, int diam, int deg, lv_color_t c)
{
    float rad = (float)deg * (float)M_PI / 180.0f;
    int x = CX + (int)lroundf((float)r_mid * sinf(rad)), y = CY - (int)lroundf((float)r_mid * cosf(rad));
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {x - diam / 2, y - diam / 2, x - diam / 2 + diam - 1, y - diam / 2 + diam - 1};
    lv_draw_rect(l, &d, &a);
}

static void draw_dotted(lv_layer_t *l, int r_mid, int diam, int a0, int a1, lv_color_t c)
{
    for (int a = a0 + 3; a <= a1 - 3; a += DOT_STEP_DEG) dot(l, r_mid, diam, a, c);
}

static void draw_slot(lv_layer_t *l, const ring_data_t *d, int i)
{
    const slot_t *s = &d->seg[i];
    int a0, a1;
    seg_span(d->n, i, &a0, &a1);
    int span = a1 - a0;
    bool sel = s->s.selected && d->n > 1;
    int out = sel ? SEL_OUT : BAND_OUT, w = sel ? SEL_W : BAND_W;
    int k = d->n > 1 && !s->s.selected ? OTHERS_PCT : 100;
    int lvl = s->lvl;
    int base = s->s.dim ? 30 : 100;
    if (d->ambient) base = 20;
    uint32_t rgb = s->s.rgb;

    switch (s->s.kind) {
    case RING_WORK: {
        arc(l, out, w, a0, a1, mixk(k, rgb, d->ambient ? 20 : 38));
        if (!d->ambient) {
            int p = lvl * COMET_STEP_DEG, len = span / 2 < COMET_DEG ? span / 2 : COMET_DEG;
            int c0 = a0 + p, c1 = c0 + len;
            if (c1 > a1) c1 = a1;
            arc(l, out, w, c0, c1, mixk(k, rgb, 100));
        }
        break;
    }
    case RING_ASK:
        arc(l, out, w, a0, a1, mixk(k, rgb, d->ambient ? 20 : 55 + lvl * 45 / PULSE_LEVELS));
        break;
    case RING_PERM: { /* two thin round tracks on the outer and inner edge of the capsule */
        int tw = sel ? SEL_TRACK_W : TRACK_W;
        lv_color_t c = mixk(k, rgb, d->ambient ? 20 : 55 + lvl * 45 / PULSE_LEVELS);
        arc(l, out, tw, a0, a1, c);
        arc(l, out - w + tw, tw, a0, a1, c);
        break;
    }
    case RING_DONE:
        if (lvl < DONE_STEPS) {
            arc(l, out, w, a0, a1, mixk(k, rgb, 25));
            arc(l, out, w, a0, a0 + span * lvl / DONE_STEPS, mixk(k, rgb, 100));
        } else {
            arc(l, out, w, a0, a1, mixk(k, rgb, base));
        }
        break;
    case RING_ERROR: /* a row of round dots along the capsule's centre line */
        draw_dotted(l, out - w / 2, w, a0, a1, mixk(k, rgb, d->ambient ? 20 : (lvl == 0 ? 35 : 100)));
        break;
    default:
        arc(l, out - w / 2 + IDLE_W / 2, IDLE_W, a0, a1, mixk(k, rgb, d->ambient ? 15 : 60));
        break;
    }
    if (sel) arc(l, out - w - UNDERLINE_GAP, UNDERLINE_W, a0, a1, mix(0xe6edf3, d->ambient ? 25 : 100));
}

static void on_draw(lv_event_t *e)
{
    ring_data_t *d = lv_event_get_user_data(e);
    lv_layer_t *l = lv_event_get_layer(e);

    if (d->solid) {
        arc(l, BAND_OUT, GLOW_W, 0, 360, d->solid_color);
        return;
    }
    if (d->n == 0) {
        arc(l, BAND_OUT, 3, 0, 360, mix(0x30363d, d->ambient ? 40 : 100));
    }
    for (int i = 0; i < d->n; i++) {
        int a0, a1;
        seg_span(d->n, i, &a0, &a1);
        if (seg_visible(l, a0, a1)) draw_slot(l, d, i);
    }
    if (d->prog > 0) arc(l, PROG_OUT, PROG_W, 0, d->prog * 360 / 1000, lv_color_hex(d->prog_rgb));
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
    bool restore = d->solid;
    d->solid = false;
    if (restore || n != d->n) {
        for (int i = 0; i < n; i++) {
            bool keep = i < d->n && d->seg[i].s.id == segs[i].id && d->seg[i].s.kind == segs[i].kind;
            d->seg[i].s = segs[i];
            if (!keep) d->seg[i].entry_ms = now_ms;
            {
                int a0, a1;
                seg_span(n, i, &a0, &a1);
                d->seg[i].lvl = (int16_t)anim_level(d, &d->seg[i], a1 - a0, now_ms);
            }
        }
        d->n = n;
        inval_all(ring);
        return;
    }
    for (int i = 0; i < n; i++) {
        slot_t *s = &d->seg[i];
        if (s->s.id == segs[i].id && s->s.kind == segs[i].kind && s->s.rgb == segs[i].rgb &&
            s->s.selected == segs[i].selected && s->s.dim == segs[i].dim)
            continue;
        if (s->s.id != segs[i].id || s->s.kind != segs[i].kind) s->entry_ms = now_ms;
        s->s = segs[i];
        int a0, a1;
        seg_span(n, i, &a0, &a1);
        s->lvl = (int16_t)anim_level(d, s, a1 - a0, now_ms);
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

void ring_set_still(lv_obj_t *ring, bool still)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    if (d->still == still) return;
    d->still = still;
    for (int i = 0; i < d->n; i++) {
        int a0, a1;
        seg_span(d->n, i, &a0, &a1);
        d->seg[i].lvl = (int16_t)anim_level(d, &d->seg[i], a1 - a0, d->last_tick);
    }
    inval_all(ring);
}

void ring_set_ambient(lv_obj_t *ring, bool ambient)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    if (d->ambient == ambient) return;
    d->ambient = ambient;
    for (int i = 0; i < d->n; i++) {
        int a0, a1;
        seg_span(d->n, i, &a0, &a1);
        d->seg[i].lvl = (int16_t)anim_level(d, &d->seg[i], a1 - a0, d->last_tick);
    }
    inval_all(ring);
}

void ring_tick(lv_obj_t *ring, uint32_t now_ms)
{
    ring_data_t *d = lv_obj_get_user_data(ring);
    if (d->solid || d->ambient || d->still || d->n == 0) return;
    if (d->have_tick && now_ms - d->last_tick < TICK_MS) return;
    d->have_tick = true;
    d->last_tick = now_ms;
    for (int i = 0; i < d->n; i++) {
        slot_t *s = &d->seg[i];
        if (s->s.kind == RING_IDLE) continue;
        int a0, a1;
        seg_span(d->n, i, &a0, &a1);
        int lvl = anim_level(d, s, a1 - a0, now_ms);
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
