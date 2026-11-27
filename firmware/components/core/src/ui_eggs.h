/* The easter eggs on screen (issue 20261126-7770, GLYPHS.md §3.15): eggs.c decides when; this
 * file asks the persona gate (PF_EGGS, a one-shot), slips the deck's clock and hands a century to
 * the Mote (a loop) or to the sigil (a spin). Silent: no haptic, no sound. */
#pragma once
#include <stdint.h>

#include "lvgl.h"
#include "model.h"

/**
 * @brief Advance the eggs: one call per HOME tick.
 * @param[in] clock The deck's clock label when it is on show (ui_home_card_clock()), else NULL.
 */
void ui_eggs_tick(uint32_t now_ms, lv_obj_t *clock);
/**
 * @brief A session changed (the model's session hook, from the glue): a turn seen finishing
 * counts, and the 100th and 1000th wait to play.
 * @param[in] before The session before the change; NULL for a new one (counts nothing).
 * @param[in] now    The session now. Must not be NULL.
 * @param[in] present The fields the change carried (SF_*).
 */
void ui_eggs_on_session(const session_t *before, const session_t *now, uint32_t present);
