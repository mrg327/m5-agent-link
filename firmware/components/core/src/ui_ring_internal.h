/* Shared by the index ring's two files only: ui_ring.c (the object, its segments, invalidation and
 * animation clock) and ui_ring_draw.c (how one capsule, dot row, track or arc is painted). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "ui_ring.h"

#define CX 233
#define CY 233
/* Index ring (UI-V3 §3.1): one round-capped capsule per session on a centre line at r 225, 8 degrees
 * apart. 6 px wide at rest, 12 px when selected (grown both ways around the same line). The selected
 * session and any that needs you are at full brightness, the rest at 40 %. A session that needs you
 * also carries a pip just inside its capsule, so it never depends on colour alone. */
#define BAND_OUT 228 /* outer radius of a normal capsule: centre line 225, width 6 */
#define BAND_W 6
#define SEL_OUT 231 /* the selected capsule: width 12 around the same centre line */
#define SEL_W 12
#define SEG_GAP_DEG 8  /* gap between capsules (shrinks when sectors get narrow) */
#define CAP_DEG 1      /* a round cap reaches about this far past its arc's end: spans are inset by it */
#define REST_PCT 40    /* brightness of a session that does not need you */
#define HOLD_DIM_PCT 20 /* the selected capsule while a hold arc is drawn over it */
#define PIP_R 207      /* needs-you pip: centre radius and diameter */
#define PIP_D 5
#define GLOW_W 14      /* ring_set_solid: the display-off alert glow is wider than the index band */
#define TRACK_RING_W 4 /* ring_set_track: the grey track and the position arc share this width */
#define PROG_OUT 230   /* hold arc: 10 px over the selected capsule */
#define PROG_W 10
#define INV_R_IN (PIP_R - PIP_D / 2 - 1) /* every invalidated chunk covers this radius band: all the ring draws */
#define INV_R_OUT SEL_OUT
#define TICK_MS 30

#define WORK_PERIOD_MS 1400
#define ASK_PERIOD_MS 1000 /* needs-you pulse: 1 Hz */
#define PERM_PERIOD_MS 1000
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
} ring_slot_t;

typedef struct {
    ring_slot_t seg[RING_MAX_SEGMENTS];
    int n;
    bool ambient, still, solid; /* still: show the settled look, no motion */
    lv_color_t solid_color;
    bool track; /* SCROLL position track instead of segments */
    int track_permille;
    uint32_t track_rgb;
    int prog;
    uint32_t prog_rgb;
    uint32_t last_tick;
    bool have_tick;
} ring_data_t;

/* ── geometry (ui_ring.c) ── */
/* Degrees (clockwise from 12 o'clock) spanned by segment i of n, gap included. */
void ring_seg_span(int n, int i, int *a0, int *a1);
/* Degrees spanned by the comet of a working segment a0..a1 at animation level lvl. */
void ring_comet_span(int a0, int a1, int lvl, int *c0, int *c1);

/* ── painting (ui_ring_draw.c) ── */
/* rgb at pct % brightness over black. */
lv_color_t ring_mix(uint32_t rgb, int pct);
/* A round-capped arc of width w whose outer edge is at r_out; a full turn has no caps. */
void ring_arc(lv_layer_t *l, int r_out, int w, int a0, int a1, lv_color_t c);
/* Animation state for a slot at time now; the value is only compared, never shown. */
int ring_anim_level(const ring_data_t *d, const ring_slot_t *s, int span, uint32_t now);
/* One segment, in its kind's shape, selection and dimming. */
void ring_draw_slot(lv_layer_t *l, const ring_data_t *d, int i);
