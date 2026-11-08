/* Shared by the ui*.c files only. */
#pragma once
#include <stdarg.h>

#include "model.h"
#include "ui.h"

#define UI_SIZE 466
#define UI_CX 233
#define UI_CY 233
#define UI_R 233
#define UI_HOLD_MS 1200 /* hold-to-approve duration; the daemon's floor is 1000 ms */
#define UI_ACTION_Y 292 /* top of the action pill row; toasts replace the pill at the same height */

extern const ui_actions_t *g_ui_actions;

bool ui_link_connected(void);
/* Milliseconds since the link was last lost (0 while connected, or if it was never up). */
uint32_t ui_link_down_ms(void);
int ui_link_transport(void); /* -1 none, else hal_link_kind_t */
bool ui_animations_allowed(void);
void ui_report_add(ui_report_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
lv_obj_t *ui_new_screen(void);

/* The Ring screen (ui_home.c). */
lv_obj_t *ui_home_init(void);
void ui_home_refresh(uint32_t now_ms);
void ui_home_tick(uint32_t now_ms);
bool ui_home_gesture(const gesture_evt_t *g);
void ui_home_select_sid(uint32_t sid, const char *via);
void ui_home_report(ui_report_t *r);
void ui_home_animations_changed(void);
