/* Shared by the ui*.c files only. */
#pragma once
#include <stdarg.h>

#include "model.h"
#include "nav.h"
#include "ui.h"

#define UI_SIZE 466
#define UI_CX 233
#define UI_CY 233
#define UI_R 233
#define UI_ACTION_Y 292 /* top of the action pill row; toasts replace the pill at the same height */

extern const ui_actions_t *g_ui_actions;

bool ui_link_connected(void);
/* Milliseconds since the link was last lost (0 while connected, or if it was never up). */
uint32_t ui_link_down_ms(void);
int ui_link_transport(void); /* -1 none, else hal_link_kind_t */
bool ui_animations_allowed(void);
void ui_report_add(ui_report_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
lv_obj_t *ui_new_screen(void);

/* Mode plumbing (ui.c): the interaction layer decides, the screens draw. */
nav_mode_t ui_mode(void);
void ui_go_home(void);          /* leave AGENT (commit, back, chord, timeout) */
void ui_go_scroll(void);        /* AGENT's "Scroll terminal": open SCROLL on the selected session */
void ui_go_status(void);        /* AGENT's "Watch...": open STATUS */
int ui_last_release_y(void);    /* where the last touch lifted, for the long-swipe rules */

/* The HOME deck (ui_home.c). */
lv_obj_t *ui_home_init(void);
void ui_home_refresh(uint32_t now_ms);
void ui_home_tick(uint32_t now_ms);
bool ui_home_act(nav_action_t a, const gesture_evt_t *g);
void ui_home_note_input(void);  /* any input anywhere resets the ambient timer and the nag */
bool ui_home_request_pending(void); /* the selected session has an unanswered approvable request on screen */
bool ui_home_askable(void);         /* the selected session has an unanswered permission or question */
uint32_t ui_home_sel_sid(void);
lv_obj_t *ui_home_ring(void);
/* "USB · dev-mac · 82%" while linked (battery only if asked), else how stale the snapshot is;
 * rgb = the row's colour. */
void ui_home_link_text(char *dst, size_t cap, bool with_battery, uint32_t *rgb);
void ui_home_set_card_shown(bool shown);
void ui_home_select_sid(uint32_t sid, const char *via);
void ui_home_report(ui_report_t *r);
void ui_home_animations_changed(void);

/* The AGENT screen (ui_agent.c). */
lv_obj_t *ui_agent_init(lv_obj_t *screen);
void ui_agent_open(uint32_t sid, uint32_t now_ms);
void ui_agent_close(void);
bool ui_agent_refresh(uint32_t now_ms); /* false: the session is gone */
bool ui_agent_act(nav_action_t a, const gesture_evt_t *g);
bool ui_agent_tick(uint32_t now_ms);    /* true: wants to close */
void ui_agent_report(ui_report_t *r);

/* The SCROLL screen (ui_scroll.c). */
lv_obj_t *ui_scroll_init(lv_obj_t *screen);
void ui_scroll_open(uint32_t sid, uint32_t now_ms);
void ui_scroll_close(void);
bool ui_scroll_refresh(uint32_t now_ms); /* false: the session is gone */
bool ui_scroll_act(nav_action_t a, const gesture_evt_t *g);
bool ui_scroll_tick(uint32_t now_ms);    /* true: wants to close; also flushes the coalescer */
void ui_scroll_report(ui_report_t *r);
void ui_scroll_on_pos(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total, uint32_t total,
                      const char *preview);

/* The ASK screen (ui_ask.c). */
lv_obj_t *ui_ask_init(lv_obj_t *screen);
void ui_ask_open(uint32_t sid, uint32_t now_ms);
void ui_ask_close(void);
bool ui_ask_refresh(uint32_t now_ms); /* false: nothing left to ask */
bool ui_ask_act(nav_action_t a, const gesture_evt_t *g);
bool ui_ask_tick(uint32_t now_ms);    /* true: wants to close */
void ui_ask_report(ui_report_t *r);

/* The STATUS screen (ui_status.c). */
lv_obj_t *ui_status_init(lv_obj_t *screen);
void ui_status_open(uint32_t now_ms);
void ui_status_close(void);
void ui_status_refresh(uint32_t now_ms);
bool ui_status_act(nav_action_t a, const gesture_evt_t *g);
bool ui_status_tick(uint32_t now_ms);    /* true: wants to close */
void ui_status_report(ui_report_t *r);
