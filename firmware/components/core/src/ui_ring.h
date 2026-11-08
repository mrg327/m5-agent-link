/* Status ring around the bezel: one arc segment per session, drawn by a single custom object.
 * One draw callback instead of N full-screen arc widgets keeps the render cost and RAM low, and
 * every change invalidates only the thin arc it touches (in chunks, so a long arc never turns into
 * a near full-screen bounding box). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define RING_MAX_SEGMENTS 32

/* The kind decides shape and motion as well as colour, so state never depends on colour alone. */
typedef enum {
    RING_WORK,  /* dim capsule + a comet sweeping along it */
    RING_ASK,   /* full capsule, gentle pulse */
    RING_PERM,  /* two thin round tracks, faster pulse */
    RING_DONE,  /* one sweep, then solid; dim once acknowledged */
    RING_ERROR, /* a row of round dots; one fast flicker, then solid */
    RING_IDLE   /* thin, dim, still */
} ring_kind_t;

typedef struct {
    uint32_t id;  /* session id: a new id or kind restarts the segment's entry animation */
    uint32_t rgb;
    uint8_t kind;
    bool selected, dim;
} ring_seg_t;

lv_obj_t *ring_create(lv_obj_t *parent);
/* Segments run clockwise from 12 o'clock. n == 0 draws a dim idle ring. Only changed segments repaint. */
void ring_set(lv_obj_t *ring, const ring_seg_t *segs, int n, uint32_t now_ms);
/* A static, unanimated full ring in one colour (display-off attention glow). */
void ring_set_solid(lv_obj_t *ring, lv_color_t color);
/* SCROLL position track (UI-V2 §4.4): one thin grey ring plus one coloured arc from 12 o'clock whose
 * length is `permille` of the turn. Replaces the segments until the next ring_set(). */
void ring_set_track(lv_obj_t *ring, int permille, uint32_t rgb);
/* Hold-to-confirm fill, 0..1000 permille clockwise from 12 o'clock; 0 hides it. */
void ring_set_progress(lv_obj_t *ring, int permille, uint32_t rgb);
/* Ambient mode: ring at about 20 % and still, to limit burn-in. */
void ring_set_ambient(lv_obj_t *ring, bool ambient);
/* Freezes motion at each kind's settled look (animations disabled). */
void ring_set_still(lv_obj_t *ring, bool still);
/* Advances the comet, pulses and entry animations; call every core_step. */
void ring_tick(lv_obj_t *ring, uint32_t now_ms);
/* Segment index under a screen point, or -1 when it is in the centre or off the ring. */
int ring_hit(lv_obj_t *ring, int x, int y);
