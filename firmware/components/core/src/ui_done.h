/* The done summary on the session card (docs/design/DONE-SUMMARY.md): one calm line in place of the stale
 * last-step text of a finished turn. See ui_done.c. */
#pragma once
#include "lvgl.h"
#include "ui_internal.h"

void ui_done_create(lv_obj_t *centre);
/* Shows the line when `allowed` and the session has a summary; returns whether it is shown. */
bool ui_done_update(const session_t *s, bool allowed, bool offline);
void ui_done_report(ui_report_t *r);
