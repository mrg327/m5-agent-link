/* Shared by the ui*.c files only. */
#pragma once
#include <stdarg.h>

#include "model.h"
#include "nav.h"
#include "ui.h"

#define UI_SIZE 466
#define UI_CX 233

extern const ui_actions_t *g_ui_actions;

bool ui_link_connected(void);
/* Milliseconds since the link was last lost (0 while connected, or if it was never up). */
uint32_t ui_link_down_ms(void);
int ui_link_transport(void); /* -1 none, else hal_link_kind_t */
bool ui_animations_allowed(void);
void ui_report_add(ui_report_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
lv_obj_t *ui_new_screen(void);
/* Slides a mode's content up UI_MOTION_RISE px into place over UI_MOTION_MS (ease-out). No-op when
 * ui_animations_allowed() is false. Only a translate: no opacity fade, so the cost is the moved pixels. */
void ui_motion_enter(lv_obj_t *content);

/* Mode plumbing (ui.c): the interaction layer decides, the screens draw. */
nav_mode_t ui_mode(void);
void ui_go_back(void);          /* one level back; the ASK reader closes first */
void ui_go_home(void);          /* leave AGENT (commit, back, chord, timeout) */
void ui_go_watch(void);
void ui_go_keys(void);  /* the Reply list's "Type...": open KEYS on the selected session */
void ui_go_voice(uint32_t rid, bool queue); /* "Dictate...": open VOICE on the selected session; rid != 0 answers that request; queue: the daemon holds it until the session can take input */
/* ASK's "Other" (proto 0.18): VOICE takes a free answer to question qi of request rid; its hold hands the
 * recording back to ASK (ui_ask_other_done) instead of sending it, unless it is the last question's. */
void ui_go_voice_answer(uint32_t rid, int qi, bool last);
void ui_go_read(void);    /* READ: the selected session's last message (LAST-MESSAGE.md) */
void ui_go_replies(void); /* AGENT, straight into the Reply list of the selected session */
/* The KEYS screen (ui_keys.c). */
lv_obj_t *ui_keys_init(lv_obj_t *screen);
void ui_keys_open(uint32_t sid, uint32_t now_ms);
void ui_keys_close(void);
bool ui_keys_refresh(uint32_t now_ms); /* false: the session is gone */
bool ui_keys_act(nav_action_t a, const gesture_evt_t *g);
void ui_keys_tick(uint32_t now_ms);
void ui_keys_report(ui_report_t *r);
/* The VOICE screen (ui_voice.c). */
lv_obj_t *ui_voice_init(lv_obj_t *screen);
/* qi >= 0: a free answer to question qi of request rid (proto 0.18); last: it is the request's last question */
void ui_voice_open(uint32_t sid, uint32_t rid, bool queue, int qi, bool last, uint32_t now_ms);
uint32_t ui_voice_rid(void); /* the request the dictation answers; 0 = typed into the terminal */
void ui_voice_close(void);
bool ui_voice_refresh(uint32_t now_ms); /* false: the session is gone */
bool ui_voice_act(nav_action_t a, const gesture_evt_t *g);
bool ui_voice_tick(uint32_t now_ms);    /* true: the transcript was sent, close */
void ui_voice_report(ui_report_t *r);
int ui_last_release_y(void);    /* where the last touch lifted, for the long-swipe rules */

/* The HOME deck (ui_home.c). */
lv_obj_t *ui_home_init(void);
void ui_home_refresh(uint32_t now_ms);
void ui_home_tick(uint32_t now_ms);
bool ui_home_ambient(void);
bool ui_home_act(nav_action_t a, const gesture_evt_t *g);
void ui_home_note_input(void);  /* any input anywhere resets the ambient timer and the nag */
bool ui_home_request_pending(void); /* the selected session has an unanswered approvable request on screen */
bool ui_home_askable(void);         /* the selected session has an unanswered permission or question */
bool ui_home_read_hit(int x, int y); /* the point is on the card's Read button (LAST-MESSAGE.md) */
uint32_t ui_home_sel_sid(void);
const attention_t *ui_home_sel_request(void); /* the selected session's unanswered permission or question */
lv_obj_t *ui_home_ring(void);
void ui_home_rim_mode(bool dial); /* true: a rim drag dials through a list; false: it picks session segments */
void ui_home_set_card_shown(bool shown);
void ui_home_select_sid(uint32_t sid, const char *via);
void ui_home_report(ui_report_t *r);
void ui_home_animations_changed(void);

/* The AGENT screen (ui_agent.c). */
lv_obj_t *ui_agent_init(lv_obj_t *screen);
void ui_agent_open(uint32_t sid, uint32_t now_ms);
void ui_agent_show_replies(void); /* the open AGENT shows the Reply list at once */
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
bool ui_scroll_tick(uint32_t now_ms);    /* true: wants to close; also flushes the coalescer and glides */
bool ui_scroll_dragged(void);            /* a drag step was emitted during the current touch: a sideways flick is not a back */
bool ui_scroll_caught(void);             /* the current touch stopped a glide */
void ui_scroll_report(ui_report_t *r);
void ui_scroll_on_pos(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total, uint32_t total,
                      const char *preview);

/* The ASK screen (ui_ask.c). */
lv_obj_t *ui_ask_init(lv_obj_t *screen);
void ui_ask_open(uint32_t sid, uint32_t now_ms);
void ui_ask_close(void);
bool ui_ask_refresh(uint32_t now_ms); /* false: nothing left to ask */
bool ui_ask_act(nav_action_t a, const gesture_evt_t *g);
bool ui_ask_back(void);               /* true: the reader was open and is now closed */
bool ui_ask_tick(uint32_t now_ms);    /* true: wants to close */
void ui_ask_report(ui_report_t *r);
/* The request ASK shows and can still be answered (its countdown runs), or NULL. */
const attention_t *ui_ask_request(void);
/* VOICE's hold on an "Other" answer: recording aid answers question qi of request rid. */
void ui_ask_other_done(uint32_t rid, int qi, uint32_t aid, uint32_t hold_ms);

/* The READ screen (ui_read.c): the agent's last message, paged, then Dictate / Reply. */
lv_obj_t *ui_read_init(lv_obj_t *screen);
void ui_read_open(uint32_t sid, uint32_t now_ms);
void ui_read_close(void);
bool ui_read_refresh(uint32_t now_ms); /* false: the session or its message is gone */
bool ui_read_act(nav_action_t a, const gesture_evt_t *g);
bool ui_read_back(void);               /* true: the end picker was open and the last page is back */
void ui_read_tick(uint32_t now_ms);
void ui_read_report(ui_report_t *r);

/* The STATUS screen (ui_status.c). */
lv_obj_t *ui_status_init(lv_obj_t *screen);
void ui_status_open(uint32_t now_ms);
void ui_status_close(void);
void ui_status_refresh(uint32_t now_ms);
bool ui_status_act(nav_action_t a, const gesture_evt_t *g);
bool ui_status_tick(uint32_t now_ms);    /* true: wants to close */
void ui_status_report(ui_report_t *r);

/* The WATCH screen (ui_watch.c): a live monitor for the session the deck has selected. */
lv_obj_t *ui_watch_init(lv_obj_t *screen);
void ui_watch_open(uint32_t sid, uint32_t now_ms);
void ui_watch_close(void);
bool ui_watch_refresh(uint32_t now_ms); /* false: the session is gone */
bool ui_watch_act(nav_action_t a, const gesture_evt_t *g);
void ui_watch_tick(uint32_t now_ms);    /* the clock; WATCH never closes by itself */
uint32_t ui_watch_sid(void);
void ui_watch_report(ui_report_t *r);
