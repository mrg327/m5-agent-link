/* The done summary (docs/design/DONE-SUMMARY.md, GLYPHS.md §5 DONE): what a finished turn did, as a row of marks
 * in place of the stale last-step text: files with a count badge, a two-colour bar for the lines, the flask with
 * a tick or a cross, the turn's time as a tally. The words stay in the report. See ui_done.c. */
#pragma once
#include "lvgl.h"
#include "ui_internal.h"

/* The session card's row (ui_home_card.c). It draws centred on y 250 of the parent it is given (the card moves
 * that parent to place it) and is 56 px high. */
void ui_done_create(lv_obj_t *centre);
/* Shows the row when `allowed` and the session has a summary; returns whether it is shown. `offline`: grey. */
bool ui_done_update(const session_t *s, bool allowed, bool offline);
void ui_done_report(ui_report_t *r);

/* WATCH's own row, centred on (UI_CX, cy) of `root`. Its report is one entry: the whole line of words, as wide
 * as WATCH's footer was. */
#define UI_DONE_WATCH_W 352
void ui_done_watch_create(lv_obj_t *root, int cy);
bool ui_done_watch_update(const session_t *s, bool allowed);
void ui_done_watch_report(ui_report_t *r);
