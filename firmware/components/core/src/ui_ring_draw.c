/* How the index ring paints (UI-V3 §3.1): one arc primitive with round caps, a round dot (the pip), and
 * one capsule per session whose motion tells the kind: a comet on a dim capsule (working), a 1 Hz pulse
 * (question, permission), a flicker then solid (error), one sweep then solid (done), still (idle). The
 * object, its segments and the animation clock live in ui_ring.c. */
#include "ui_ring_internal.h"

#include <math.h>

#include "ui_theme.h"

lv_color_t ring_mix(uint32_t rgb, int pct) { return theme_dim(rgb, pct); }

void ring_arc(lv_layer_t *l, int r_out, int w, int a0, int a1, lv_color_t c)
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

int ring_anim_level(const ring_data_t *d, const ring_slot_t *s, int span, uint32_t now)
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

static bool needs_you(uint8_t kind) { return kind == RING_ASK || kind == RING_PERM || kind == RING_ERROR; }

void ring_draw_slot(lv_layer_t *l, const ring_data_t *d, int i)
{
    const ring_slot_t *s = &d->seg[i];
    int a0, a1;
    ring_seg_span(d->n, i, &a0, &a1);
    int span = a1 - a0;
    bool sel = s->s.selected && d->n > 1;
    int out = sel ? SEL_OUT : BAND_OUT, w = sel ? SEL_W : BAND_W;
    bool needs = needs_you(s->s.kind), holding = s->s.selected && d->prog > 0;
    int lvl = s->lvl;
    /* Full brightness for the selected session and any that needs you; the rest recede to 40 %. */
    int rest = s->s.selected || needs ? 100 : REST_PCT;
    if (s->s.dim && rest > 25) rest = 25;
    if (holding) rest = HOLD_DIM_PCT;
    if (d->ambient) rest = 20;
    bool still = holding || d->ambient;
    uint32_t rgb = s->s.rgb;

    switch (s->s.kind) {
    case RING_WORK: {
        ring_arc(l, out, w, a0, a1, theme_dim(rgb, still ? rest : s->s.selected ? 60 : REST_PCT));
        if (!still) {
            int c0, c1;
            ring_comet_span(a0, a1, lvl, &c0, &c1);
            ring_arc(l, out, w, c0, c1, theme_dim(rgb, 100));
        }
        break;
    }
    case RING_ASK:
    case RING_PERM:
        ring_arc(l, out, w, a0, a1, theme_dim(rgb, still ? rest : 55 + lvl * 45 / PULSE_LEVELS));
        break;
    case RING_ERROR: /* one quick flicker, then solid */
        ring_arc(l, out, w, a0, a1, theme_dim(rgb, still ? rest : (lvl == 0 ? 35 : 100)));
        break;
    case RING_DONE:
        if (lvl < DONE_STEPS && !still) {
            ring_arc(l, out, w, a0, a1, theme_dim(rgb, 25));
            ring_arc(l, out, w, a0, a0 + span * lvl / DONE_STEPS, theme_dim(rgb, 100));
        } else {
            ring_arc(l, out, w, a0, a1, theme_dim(rgb, rest));
        }
        break;
    default:
        ring_arc(l, out, w, a0, a1, theme_dim(rgb, rest));
        break;
    }
    if (needs && !s->s.selected && !d->ambient) dot(l, PIP_R, PIP_D, (a0 + a1) / 2, theme_dim(rgb, 100));
}
