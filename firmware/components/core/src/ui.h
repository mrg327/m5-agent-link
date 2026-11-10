/* LVGL user interface: one Ring screen (docs/design/UI-RING.md).
 * Everything here runs on the LVGL thread (core_step). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gesture.h"
#include "lvgl.h"
#include "nav.h"

typedef struct {
    char screen[12];            /* "ring" | "attention" (selected session has a pending request) | "agent" | "ask" | "status" | "watch" | "off" */
    bool has_sel_sid;
    uint32_t sel_sid;
    bool has_rid;
    uint32_t rid;
    char visible[8][48];        /* labels of visible elements, top → bottom */
    size_t n_visible;
    const char *power;
    uint32_t fps;
} ui_report_t;

/* What the UI asks the app to do; the app talks to the link and feedback. */
typedef struct {
    void (*focus)(uint32_t sid);
    void (*approve)(uint32_t sid, uint32_t rid, uint32_t hold_ms);
    void (*deny)(uint32_t sid, uint32_t rid);
    void (*reply)(uint32_t sid, uint32_t rid, uint8_t opt, uint32_t hold_ms); /* ASK: answer a question / pick a grant option */
    void (*ack)(uint32_t sid);
    void (*snooze)(uint32_t sid, uint32_t minutes); /* AGENT: snooze this session (0 cancels) */
    void (*notify_done)(uint32_t sid, bool arm);    /* AGENT: buzz when this session finishes (false disarms) */
    void (*type_reply)(uint32_t sid, int idx);       /* AGENT: type quick reply idx into the terminal */
    void (*interrupt)(uint32_t sid, uint32_t hold_ms); /* AGENT: stop the running turn (Esc), after a hold */
    void (*quiet_toggled)(void); /* STATUS: quiet mode turned on, extended or off */
    void (*tick)(void);   /* light haptic tick: selection by tap or button */
    void (*detent)(void); /* rim-scrub segment crossed */
    void (*hold_start)(void); /* approve hold passed the grace period: start the rising haptic ramp */
    void (*deny_hold_start)(void); /* deny hold: the falling ramp */
    void (*hold_stop)(void);
    void (*reject)(void); /* nothing to do: buzz */
    void (*nag)(void);    /* unanswered permission request: remind */
    void (*sectors)(int n); /* the rim now has n selectable segments */
} ui_actions_t;

void ui_init(const ui_actions_t *actions);
/* Per-step update: re-renders on model changes, advances clock and hold-to-confirm. */
void ui_tick(uint32_t now_ms);
/* Returns true if the gesture was used by the current screen. */
bool ui_on_gesture(const gesture_evt_t *g);
void ui_report(ui_report_t *out);
void ui_set_link_state(bool connected); /* handshake done (welcomed) */
/* Transport currently carrying the link: a hal_link_kind_t value, or -1 for none. */
void ui_set_link_transport(int kind);
/* Moves the selection to a session (attention wake); counts as user attention. */
void ui_show_session(uint32_t sid);
/* A request arrived (or a glow was touched): select its session and open ASK on it. */
void ui_show_attention(uint32_t sid);
/* The session the WATCH screen is showing, 0 when it is not open. */
uint32_t ui_watched_sid(void);
/* The first unanswered request, if any, else 0. */
uint32_t ui_pending_attention_sid(void);
void ui_toast(const char *text, uint32_t rgb);
/* Brief dim ring for alerts while the display is otherwise idle; text-free by design. */
void ui_glow(uint32_t rgb);
void ui_glow_hide(void);
bool ui_glow_active(void);
void ui_set_animations(bool enabled);
