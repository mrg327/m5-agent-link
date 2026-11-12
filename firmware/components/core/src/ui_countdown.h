/* The answer window's countdown ring (ui_countdown.c, ANSWER-WINDOW.md §5). */
#pragma once
#include "lvgl.h"
#include "model.h"
#include "ui.h"

void ui_countdown_init(lv_obj_t *screen);
/* Shows `a`'s window at now_ms, or hides the ring (a NULL, answered or window-less request). */
void ui_countdown_show(const attention_t *a, uint32_t now_ms);
void ui_countdown_report(ui_report_t *r);
/* Whole seconds left on the ring as last drawn (rounded up); -1 while it is hidden. */
int ui_countdown_left_s(void);
