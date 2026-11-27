/* The Mote on the pairing passkey screen (issue 20261126-2de2, GLYPHS.md §3.14): a still orb at
 * 12 o'clock while the digits show (stillness means "waiting for you"), one flare when pairing
 * completes, nothing when it fails. Only with the Mote switched on (FUN_MOTE): it asks
 * persona_allow(PF_MOTE) for a pose while the PC waits (pairing counts as needing you) and for a
 * one-shot to flare. It never covers the digits or the Bluetooth glyph, never buzzes or sounds,
 * and repaints only the orb's box. ui.c owns the screen; this file only draws on it. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/** @brief Create the orb in @p parent, the passkey screen (466 x 466 at the origin). */
void ui_mote_pair_create(lv_obj_t *parent);
/** @brief New digits are on show: the orb holds still (a flare under way is cut). */
void ui_mote_pair_wait(void);
/**
 * @brief Pairing completed: flare once if the orb is on show and the gate allows a one-shot.
 * @return true while it flares: the caller keeps the screen up until ui_mote_pair_tick() says.
 */
bool ui_mote_pair_done(uint32_t now_ms);
/**
 * @brief Advance the orb: one call per UI tick.
 * @param[in] shown The passkey screen is up.
 * @return true when the flare is over (or cut by an alert): hide the screen now.
 */
bool ui_mote_pair_tick(uint32_t now_ms, bool shown);
