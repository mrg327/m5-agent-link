/* Shared by the index ring's two files only: ui_ring.c (the object, its segments, invalidation and
 * animation clock) and ui_ring_draw.c (how one capsule, dot row, track or arc is painted). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "ui_ring.h"

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
#define TRACK_RING_W 6 /* ring_set_track: the grey track and the position arc share this width */
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

/* ── painting (ui_ring_draw.c) ── */
/* rgb at pct % brightness over black. */
lv_color_t ring_mix(uint32_t rgb, int pct);
/* A round-capped arc of width w whose outer edge is at r_out; a full turn has no caps. */
void ring_arc(lv_layer_t *l, int r_out, int w, int a0, int a1, lv_color_t c);
/* Animation state for a slot at time now; the value is only compared, never shown. */
int ring_anim_level(const ring_data_t *d, const ring_slot_t *s, int span, uint32_t now);
/* One segment, in its kind's shape, selection and dimming. */
void ring_draw_slot(lv_layer_t *l, const ring_data_t *d, int i);
