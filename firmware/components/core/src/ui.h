/* LVGL user interface: glance, session list, session detail / attention card.
 * Everything here runs on the LVGL thread (core_step). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gesture.h"
#include "lvgl.h"

typedef struct {
    char screen[12];            /* "glance" | "list" | "detail" | "attention" | "off" */
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
    void (*ack)(uint32_t sid);
    void (*tick)(void); /* light haptic tick for selection changes */
} ui_actions_t;

void ui_init(const ui_actions_t *actions);
/* Per-step update: re-renders on model changes, advances clock and hold-to-confirm. */
void ui_tick(uint32_t now_ms);
/* Returns true if the gesture was used by the current screen. */
bool ui_on_gesture(const gesture_evt_t *g);
/* True if a tap would navigate or act irreversibly, so the app holds it back until the
 * double-tap window has passed (double-tap means "focus" on list and detail). */
bool ui_tap_defers(const gesture_evt_t *g);

void ui_report(ui_report_t *out);
void ui_set_link_state(bool connected); /* handshake done (welcomed) */
/* Transport currently carrying the link: a hal_link_kind_t value, or -1 for none. */
void ui_set_link_transport(int kind);
void ui_show_glance(void);
void ui_show_session(uint32_t sid); /* detail (or attention card) for a session */
void ui_toast(const char *text, uint32_t rgb);
/* Brief dim ring for alerts while the display is otherwise idle; text-free by design. */
void ui_glow(uint32_t rgb);
void ui_glow_hide(void);
bool ui_glow_active(void);
void ui_set_animations(bool enabled);
