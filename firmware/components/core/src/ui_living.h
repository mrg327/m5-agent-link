/* Living marks on screen (issue 20261126-98a6, GLYPHS.md §3.11): the motion grammar applied to the
 * marks a screen binds, each step, through the persona gate.
 *
 * A screen binds its sigil (and its hero glyph) to a slot once, and says in its update what the
 * slot shows: a session and a temper_state_t. The glue hands the step's persona context to
 * ui_living_tick() after ui_tick(); every pose goes through a change-only setter, so a still mark
 * costs nothing and a breathing one repaints its own box (r + 6 each side) at most 16 times a
 * breath. Other fun features ask ui_living_allow() with the same context. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "model.h"
#include "persona.h"
#include "temper.h"

/** The marks that live; each is bound by the screen that draws it. */
typedef enum {
    UI_LIVING_HOME,    /**< the deck's sigil (r 22) and hero glyph */
    UI_LIVING_AMBIENT, /**< the ambient face's sigil */
    UI_LIVING_SLOTS
} ui_living_slot_t;

/**
 * @brief Bind a slot to its objects (at screen creation).
 * @param[in] slot   The slot.
 * @param[in] sigil  A ui_sigil object. Kept.
 * @param[in] r      Its outer radius, px.
 * @param[in] hero   The hero glyph (ui_glyph) the mark leans toward and that dips and shivers;
 *                   NULL for none. Kept. Its rest is (hero_cx, hero_cy) in its parent; a burst
 *                   object is made beside it in the same parent.
 * @param[in] lean_x The hero is to the right of the mark (the lean runs along x, not y).
 */
void ui_living_bind(ui_living_slot_t slot, lv_obj_t *sigil, int r, lv_obj_t *hero, int hero_cx,
                    int hero_cy, bool lean_x);
/**
 * @brief Say what a slot shows now (from the screen's update; change-only).
 *
 * A new session starts at rest, without its state's gesture: scrolling onto a finished session
 * does not dip it. A new state on the same session plays its gesture from the start.
 * @param[in] name The folder name (NULL or "" for none), as given to ui_sigil_set_session.
 * @param[in] sid  The session id; 0 with no name: nothing to show (TEMPER_STILL).
 * @param[in] st   What the mark shows.
 */
void ui_living_show(ui_living_slot_t slot, const char *name, uint32_t sid, temper_state_t st);
/**
 * @brief What a session's mark shows.
 * @param[in] s       The session. Must not be NULL.
 * @param[in] asks    It has an unanswered request on screen (it wants you whatever its state).
 * @param[in] snoozed It is snoozed.
 * @param[in] still   Offline, handed back, merged: the mark holds still as drawn.
 */
temper_state_t ui_living_state(const session_t *s, bool asks, bool snoozed, bool still);
/**
 * @brief Advance every bound mark: one call per core_step, after ui_tick().
 * @param[in] ctx The step's persona context (glue-filled; copied). Must not be NULL.
 */
void ui_living_tick(const persona_ctx_t *ctx, uint32_t now_ms);
/**
 * @brief Turn the rings of the mark on show once round (the centuries egg, eggs.h): 800 ms over
 * whatever it is doing. The caller has asked the gate (PF_EGGS); a mark held still does not turn.
 */
void ui_living_spin(uint32_t now_ms);
/** @brief persona_allow() for a UI feature with this step's context (nothing user-started). */
unsigned ui_living_allow(persona_feat_t f);
/** @brief This step's persona context, for a feature that sets its own fields and asks. */
const persona_ctx_t *ui_living_ctx(void);
