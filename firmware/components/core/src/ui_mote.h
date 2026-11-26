/* The Mote on the idle face (issue 20261126-59e0, GLYPHS.md §3.14): one object over the deck's
 * centre, so it drifts with the ambient face. mote.c decides; this file reads the fleet, the clock
 * and the persona gate, draws the firefly with ui_orb.h on its track at r 175 and repaints only the
 * orb's box, old and new. Silent: it has no haptic and no sound. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "model.h"

/** @brief Create the firefly in @p parent, a full-size (466 x 466) object. Drawn nothing yet. */
void ui_mote_create(lv_obj_t *parent);
/**
 * @brief Advance the firefly: one call per HOME tick.
 * @param[in] face      The idle (ambient) face is up on HOME.
 * @param[in] error_deg The first error capsule's centre, degrees clockwise from 12; -1 none.
 * @param[in] work_deg  The working sessions' slot centres, degrees clockwise from 12, in ring
 *                      order; may be NULL when @p n_work is 0. Not kept.
 * @param[in] n_work    How many; past MOTE_VISIT_MAX the rest are left out.
 */
void ui_mote_tick(uint32_t now_ms, bool face, int error_deg, const int16_t *work_deg, int n_work);
/**
 * @brief A session changed (the model's session hook, from the glue): a finish with green tests
 * counts the day (the level), a big one cheers.
 * @param[in] before The session before the change; NULL for a new one (counts nothing).
 * @param[in] now    The session now. Must not be NULL.
 * @param[in] present The fields the change carried (SF_*).
 */
void ui_mote_on_session(const session_t *before, const session_t *now, uint32_t present);
/** @brief The firefly is on screen now. */
bool ui_mote_shown(void);
/** @brief Loop once off the track (the centuries egg); nothing when it is not shown. */
void ui_mote_loop(void);
