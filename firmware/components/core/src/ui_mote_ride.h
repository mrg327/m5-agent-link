/* The Mote riding an arc (issue 20261126-9fe3, GLYPHS.md §3.13, §3.14): the orb at the tucked
 * size, sitting on the head of an arc that something else draws (the update's progress, the
 * replay's hour hand). It moves only when the arc does and never breathes. One small object the
 * size of the orb's box (ui_orb_area), so a move repaints that box, old and new, and nothing else.
 * The screen that owns the arc asks the persona gate (PF_MOTE) and calls this; the arc itself is
 * always drawn, so the orb is never the only sign of the progress. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "mote.h"

#define UI_MOTE_RIDE_LIT 100 /* % of full: the rider is lit (MOTE_LOOK_LEVEL_LIT) */
#define UI_MOTE_RIDE_DIM 25  /* % of full: stalled, the ember's level (MOTE_LOOK_LEVEL_EMBER) */

/** One rider: zero it, then ui_mote_ride_create(). */
typedef struct {
    lv_obj_t *obj;    /**< the orb's box, a child of the screen the arc is on */
    mote_look_t look; /**< as drawn */
    int16_t x, y;     /**< its centre, in the parent's coordinates */
    bool shown;       /**< drawn now */
} ui_mote_ride_t;

/** @brief Create the rider in @p parent, hidden, just above the objects created before it. */
void ui_mote_ride_create(ui_mote_ride_t *r, lv_obj_t *parent);
/**
 * @brief The rider's look: the profile's tucked size (core 6 px, halo 16 px across) at @p
 * scale_pct of it, lit to @p lum, the halo held inhaled (no breath).
 */
mote_look_t ui_mote_ride_look(uint16_t scale_pct, uint8_t lum);
/**
 * @brief The one flare (on success): its size at @p t_ms since it began, % of the tucked size,
 * up to the profile's flare scale over its rise and back over its fall; 100 once it is over.
 * @param[out] over Set true once the flare is over. May be NULL.
 */
uint16_t ui_mote_ride_flare(uint32_t t_ms, bool *over);
/**
 * @brief Put the orb on the head of an arc: centre (@p cx, @p cy) in the parent's coordinates,
 * centre-line radius @p radius, the head @p deg whole degrees clockwise from 12 o'clock.
 * Change-only: the same place and look repaint nothing.
 */
void ui_mote_ride_at(ui_mote_ride_t *r, int cx, int cy, int radius, int deg,
                     const mote_look_t *look);
/** @brief Take the orb away (its box repaints once); nothing when it is not shown. */
void ui_mote_ride_hide(ui_mote_ride_t *r);
