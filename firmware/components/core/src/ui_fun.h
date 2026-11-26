/* LABS > Personality (issue 20261126-98a6): the fun switchboard until STATUS has rows. An orbit of
 * toggle discs, one glyph per fun feature and one for reduced motion, the off ones dim; the
 * selected one's glyph is the subject, white when on and grey when off. A tap on the selection or
 * A toggles it (prefs_set_fun), the dial and B move, back returns to the experiments. No word is
 * drawn; the report names each switch and its state. Opened and closed by ui_labs.c, inside the
 * LABS mode. */
#pragma once
#include <stdbool.h>

#include "lvgl.h"
#include "nav.h"
#include "ui.h"

/** @brief Create the view, hidden, in @p parent (LABS's screen). */
lv_obj_t *ui_fun_init(lv_obj_t *parent);
void ui_fun_open(void);
void ui_fun_close(void);
bool ui_fun_is_open(void);
/** @brief LABS's input while the view is open; true when used. */
bool ui_fun_act(nav_action_t a, const gesture_evt_t *g);
void ui_fun_report(ui_report_t *r);
