/* The orb: the Mote's firefly drawn anywhere (issue 20261126-59e0, GLYPHS.md §3.14). A core and a
 * halo of seven precomputed concentric alpha discs, so it costs a few small fills and no image or
 * blur. Apart from the idle face's behaviour (mote.c, ui_mote.c), so the mascot can appear on other
 * screens at any position and size: build a look with mote_look() (and mote_breath()), paint it
 * from a draw event with ui_orb_draw(), and invalidate ui_orb_area() round its old and new places.
 */
#pragma once
#include <stdint.h>

#include "lvgl.h"
#include "mote.h"

/**
 * @brief Paint the orb centred on (@p x, @p y), in screen coordinates.
 * @param[in] layer The draw event's layer. Must not be NULL.
 * @param[in] look  Its core and halo (mote_look()). Must not be NULL.
 * @param[in] rgb   Its colour at full brightness (MOTE_RGB for the Mote).
 */
void ui_orb_draw(lv_layer_t *layer, int x, int y, const mote_look_t *look, uint32_t rgb);
/**
 * @brief The box the orb at (@p x, @p y) covers, anti-aliasing included.
 * @param[in]  look Its core and halo. Must not be NULL.
 * @param[out] out  The box. Must not be NULL.
 */
void ui_orb_area(int x, int y, const mote_look_t *look, lv_area_t *out);
