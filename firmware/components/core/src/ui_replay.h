/* While you were away on the bezel (issue 20261126-9f99, GLYPHS.md §3.13): when you come back to
 * the watch after the display was dark for 15 minutes or more, the ring replays the last hour in
 * 20 steps of 3 minutes over 2 s, with a thin white arc at r 216 growing round as the hour's hand,
 * while the centre shows now at once. LABS > Personality, off by default (FUN_REPLAY).
 *
 * It asks the persona gate for a one-shot, so anything that needs you, reduced motion, the feature
 * off or a dark display skips it. Any touch, key or raise during it ends it at once, and input is
 * never held for it: it only draws. No sound, no buzz. The history is replay.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/**
 * @brief Record the ring and run the replay on it: one call per step, from HOME's tick.
 * @param[in] ring     HOME's index ring (ui_ring.h); its parent also holds the time arc.
 * @param[in] now_ms   The step's clock.
 * @param[in] input_ms When HOME last saw a touch, key or raise; a change ends the replay.
 */
void ui_replay_tick(lv_obj_t *ring, uint32_t now_ms, uint32_t input_ms);
/** @brief The bezel is replaying (another ring overlay, such as the Mote, steps aside). */
bool ui_replay_playing(void);
