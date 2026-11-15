/* Usage and context gauges on glass (docs/design/GAUGES.md, GLYPHS.md §5 GAUGES); see ui_gauges.c. Each screen
 * creates its piece once, updates it from its own refresh and adds it to its report. Every piece is an arc or a
 * glyph: the words are only in the reports. */
#pragma once
#include "lvgl.h"
#include "ui_internal.h"

/* The session card (ui_home_card.c): the context window as an arc along the bottom of the bezel, between the key
 * hints (r 196, 30 degrees either side of 6 o'clock: screen y 396 to 436). It keeps that place wherever the card
 * puts the parent it gave. `shown` is false for anything but the card look, a merged slot or a card that shows a
 * request. */
void ui_gauges_card_create(lv_obj_t *centre);
void ui_gauges_card_update(const session_t *s, bool shown, bool offline);
void ui_gauges_card_report(ui_report_t *r);

/* The empty and ambient faces (ui_home_card.c): the plan's two windows as a pair of arcs from 12 o'clock (see
 * STATUS below), shown while the 5-hour window is known; `dim` on the ambient face. The report says "5h 42%". */
void ui_gauges_face_create(lv_obj_t *centre);
void ui_gauges_face_update(bool shown, bool dim);
void ui_gauges_face_report(ui_report_t *r);

/* STATUS (ui_status.c): the same pair of arcs, and the two figures on one row under the clock.
 * - The arcs are about the centre of `root` (which must be the full screen), centre line r 194, 6 px wide: the 5
 *   hours from 11 to 118 degrees anticlockwise of 12 o'clock, the week the same clockwise, a 20 px glyph at the
 *   start of each (y 39). They and their beads stay inside r 187 to 201.
 * - The figures ("42%", "18%") are centred on y 166, either side of x 233, in the 24 px font: they take the place
 *   of the two rows of words (y 152 and 180).
 * Returns whether they are shown: STATUS then sets its clock smaller and higher, above the figures. */
void ui_gauges_status_create(lv_obj_t *root);
bool ui_gauges_status_render(uint32_t now);
void ui_gauges_status_report(ui_report_t *r);

/* AGENT (ui_agent.c): one row centred on (233, 108) of `root`, 32 px high: the model's name, the permission mode
 * as a glyph (none for the default mode) and the pull request as a glyph with its number, in the colour of its
 * review state. The report still says "Opus 4.7 · Plan · PR 128 approved". */
void ui_gauges_agent_create(lv_obj_t *root);
void ui_gauges_agent_update(const session_t *s, bool shown);
void ui_gauges_agent_report(ui_report_t *r);
