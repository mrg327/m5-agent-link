/* The Mote on the idle face (issue 20261126-59e0, GLYPHS.md §3.14): one object over the deck's
 * centre, so it drifts with the ambient face. mote.c decides; this file reads the fleet, the clock
 * and the persona gate, draws the light (a head and a three-step tail on the track at r 216) and
 * repaints only the small boxes it moved through. Silent: it has no haptic and no sound. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "model.h"

/** @brief Create the light in @p parent, a full-size (466 x 466) object. Drawn nothing yet. */
void ui_mote_create(lv_obj_t *parent);
/**
 * @brief Advance the light: one call per HOME tick.
 * @param[in] face      The idle (ambient) face is up on HOME.
 * @param[in] error_deg The first error capsule's centre, degrees clockwise from 12; -1 none.
 */
void ui_mote_tick(uint32_t now_ms, bool face, int error_deg);
/**
 * @brief A session changed (the model's session hook, from the glue): a finish with green tests
 * counts the day (the level), a big one cheers.
 * @param[in] before The session before the change; NULL for a new one (counts nothing).
 * @param[in] now    The session now. Must not be NULL.
 * @param[in] present The fields the change carried (SF_*).
 */
void ui_mote_on_session(const session_t *before, const session_t *now, uint32_t present);
/** @brief The light is on screen now. */
bool ui_mote_shown(void);
/** @brief Loop once off the track (the centuries egg); nothing when it is not shown. */
void ui_mote_loop(void);
