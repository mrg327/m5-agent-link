/* Status ring around the bezel: one arc segment per session, drawn by a single custom object.
 * One draw callback instead of N full-screen arc widgets keeps the render cost and RAM low, and
 * every change invalidates only the thin arc it touches (in chunks, so a long arc never turns into
 * a near full-screen bounding box). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define RING_MAX_SEGMENTS 32

/* The kind decides motion and, for one that needs you, the pip's shape as well as colour, so state
 * never depends on colour alone (UI-V3 §3.1). */
typedef enum {
    RING_WORK,  /* dim capsule + a comet sweeping along it */
    RING_ASK,   /* 1 Hz pulse; pip: a dot */
    RING_PERM,  /* 1 Hz pulse; pip: a bar along the ring */
    RING_DONE,  /* one sweep, then solid; dim once acknowledged */
    RING_ERROR, /* one fast flicker, then solid; pip: a triangle pointing at the centre */
    RING_IDLE   /* dim, still */
} ring_kind_t;

typedef struct {
    uint32_t id; /* session id: a new id or kind restarts the segment's entry animation */
    uint32_t rgb;
    uint8_t kind;
    bool selected, dim;
} ring_seg_t;

lv_obj_t *ring_create(lv_obj_t *parent);
/* Segments run clockwise from 12 o'clock. n == 0 draws a dim idle ring. Only changed segments
 * repaint. */
void ring_set(lv_obj_t *ring, const ring_seg_t *segs, int n, uint32_t now_ms);
/* A static, unanimated full ring in one colour (display-off attention glow). */
void ring_set_solid(lv_obj_t *ring, lv_color_t color);
/* SCROLL position track (UI-V2 §4.4): one thin grey ring plus one coloured arc from 12 o'clock
 * whose length is `permille` of the turn. Replaces the segments until the next ring_set(). */
void ring_set_track(lv_obj_t *ring, int permille, uint32_t rgb);
/* Hold-to-confirm fill, 0..1000 permille clockwise from 12 o'clock; 0 hides it. */
void ring_set_progress(lv_obj_t *ring, int permille, uint32_t rgb);
/* Ambient mode: ring at about 20 % and still, to limit burn-in. */
void ring_set_ambient(lv_obj_t *ring, bool ambient);
/* Freezes motion at each kind's settled look (animations disabled). */
void ring_set_still(lv_obj_t *ring, bool still);
/* Advances the comet, pulses and entry animations; call every core_step. Holds still while the
 * display is off. */
void ring_tick(lv_obj_t *ring, uint32_t now_ms);
/* The needs-you mark of `kind` inside a draw callback, centred at polar (r_mid, deg clockwise from
 * 12 o'clock) about the screen centre: a dot of diameter `size` (RING_ASK), a bar along the ring
 * 1.8 size long and 0.8 size wide (RING_PERM), a triangle 1.6 size wide pointing at the centre
 * (RING_ERROR). Nothing for any other kind. The ring's pips are size 10; the waiting-request mark
 * (ui.c) uses it too. */
void ring_pip_paint(lv_layer_t *l, uint8_t kind, int r_mid, int deg, int size, lv_color_t c);
/* While you were away (ui_replay.c): segment i is drawn as `kind` in `rgb` (dimmed when `dim`),
 * settled and still, instead of as set, until ring_override_end(); RING_HOLLOW draws it as an
 * empty track (the session had not started), RING_LIVE ends its override. Selection stays the
 * live one's. Change-only: a changed segment repaints its own arc. */
#define RING_LIVE 0xFE
#define RING_HOLLOW 0xFF
void ring_override(lv_obj_t *ring, int i, uint8_t kind, uint32_t rgb, bool dim);
/* Every segment as set again; the overridden ones repaint. */
void ring_override_end(lv_obj_t *ring);
/* Copies up to `max` of the segments as last set (never the override) into out; returns how many.
 */
int ring_get(lv_obj_t *ring, ring_seg_t *out, int max);
/* Invalidates, on object o (full screen at 0, 0), the band r_in..r_out between degrees a0 and a1
 * clockwise from 12 o'clock, in small chunks: for an overlay on the ring. */
void ring_invalidate_arc(lv_obj_t *o, int a0, int a1, int r_in, int r_out);
/* Segment index under a screen point, or -1 when it is in the centre or off the ring. */
int ring_hit(lv_obj_t *ring, int x, int y);
