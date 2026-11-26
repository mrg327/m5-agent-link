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
    /* "ring" | "attention" (selected session has a pending request) | "agent" | "ask" | "scroll" |
     * "status" | "watch" | "off" */
    char screen[12];
    bool has_sel_sid;
    uint32_t sel_sid;
    bool has_rid;
    uint32_t rid;
    char visible[8][48]; /* labels of visible elements, top → bottom */
    size_t n_visible;
    const char *power;
    uint32_t fps;
} ui_report_t;

/* What a reply does, so its commit buzz matches the hold before it: a permission's own option
 * grants or denies like Approve and Deny; a question's answer does neither. */
typedef enum {
    UI_REPLY_NEUTRAL, /**< a question's answer: the "sent" buzz */
    UI_REPLY_GRANT,   /**< a permission option that grants: as Approve */
    UI_REPLY_DENY,    /**< a permission option that refuses: as Deny */
} ui_reply_kind_t;

/* What the UI asks the app to do; the app talks to the link and feedback. */
typedef struct {
    void (*focus)(uint32_t sid);
    /* ASK: Open on PC, handing request rid back to the terminal */
    void (*open_request)(uint32_t sid, uint32_t rid);
    void (*approve)(uint32_t sid, uint32_t rid, uint32_t hold_ms);
    /* ASK: approve + save the standing rule */
    void (*grant)(uint32_t sid, uint32_t rid, uint32_t hold_ms, uint32_t rule_hash);
    void (*deny)(uint32_t sid, uint32_t rid);
    /* ASK: answer a question / pick one of a permission's options */
    void (*reply)(uint32_t sid, uint32_t rid, uint8_t opt, uint32_t hold_ms, ui_reply_kind_t kind);
    /* ASK (proto 0.18): answer every question of request rid at once, after a hold: per question
     * the options picked (a bit mask) or the recording of a free answer */
    void (*answer)(uint32_t sid, uint32_t rid, const uint32_t *picks, const uint32_t *aids, int n,
                   uint32_t hold_ms);
    void (*ack)(uint32_t sid);
    void (*snooze)(uint32_t sid, uint32_t minutes); /* AGENT: snooze this session (0 cancels) */
    /* AGENT: buzz when this session finishes (false disarms) */
    void (*notify_done)(uint32_t sid, bool arm);
    /* AGENT: type quick reply idx into the terminal, or queue it (proto 0.17) */
    void (*type_reply)(uint32_t sid, int idx, bool queue);
    /* AGENT: drop the session's queued message (proto 0.17) */
    void (*unqueue)(uint32_t sid);
    /* AGENT: stop the running turn (Esc) or job (Ctrl-C), after a hold */
    void (*interrupt)(uint32_t sid, uint32_t hold_ms);
    /* AGENT: run a finished job again, after a hold (JOBS.md) */
    void (*rerun)(uint32_t sid, uint32_t hold_ms);
    /* VOICE: type recording aid's transcript, answer request rid with it, or queue it */
    void (*dictate)(uint32_t sid, uint32_t rid, uint32_t aid, uint32_t hold_ms, bool queue);
    /* entering SCROLL: bring the terminal to the front, quietly */
    void (*scroll_open)(uint32_t sid);
    /* SCROLL: one coalesced cmd_op_scroll */
    void (*scroll)(uint32_t sid, nav_scroll_unit_t unit, int delta);
    void (*scroll_stop)(void);   /* SCROLL reached the bottom or the top */
    void (*graph)(uint32_t sid); /* GRAPH: ask for the session's agent graph (proto 0.22) */
    void (*quiet_toggled)(void); /* STATUS: quiet mode turned on, extended or off */
    /* STATUS: the speaker level was stepped (prefs.h): play a preview */
    void (*volume_changed)(void);
    void (*tick)(void);   /* light haptic tick: selection by tap or button */
    void (*detent)(void); /* rim-scrub segment crossed */
    /* approve hold passed the grace period: start the rising haptic ramp */
    void (*hold_start)(void);
    void (*deny_hold_start)(void); /* deny hold: the falling ramp */
    void (*hold_stop)(void);
    void (*reject)(void);         /* nothing to do: buzz */
    void (*sent)(void);           /* KEYS: the composed text went out: the approve pattern */
    bool (*button_down)(int btn); /* KEYS: is physical button 0 (A) / 1 (B) still held */
    bool (*touching)(void);       /* a finger is on the glass (no card opens under it) */
    void (*nag)(void);            /* unanswered permission request: remind */
    void (*sectors)(int n);       /* the rim now has n selectable segments */
    /* TRACKPAD (proto 0.20, EXPERIMENTS.md): one cmd_op_pointer (btn -1 = no click); false when it
     * could not go out */
    bool (*pointer)(int dx, int dy, int btn, int n, int wheel);
    void (*moves)(bool on); /* TRACKPAD: the recognizer streams G_MOVE while on */
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
/* A request (with error: an error) on session sid, in colour rgb, as it arrives: open it (ASK, or
 * the card for an error) unless that would lose work (KEYS, VOICE, READ, GRAPH, STATUS, LABS,
 * TRACKPAD) or hide another request open in ASK. Then it waits, a dot on the rim, and opens when
 * the user is back on the deck or WATCH (issue 20261122-883f). */
void ui_offer_attention(uint32_t sid, bool error, uint32_t rgb);
/* The same, but it only waits (the display is dark or glowing): see ui_reveal_attention. */
void ui_wait_attention(uint32_t sid, bool error, uint32_t rgb);
/* Request rid on session sid was re-sent (not new): nothing alerts; ASK reopens on it only when it
 * was open on it when a new link dropped the cards, and the user has not moved since. */
void ui_resent_attention(uint32_t sid, uint32_t rid, uint32_t rgb);
/* The display woke: what waits opens now, unless the open screen keeps it waiting. */
void ui_reveal_attention(void);
/* Counts mode changes: a change the user did not make swallows a finger already down. */
uint32_t ui_mode_seq(void);
/* ASK is open on session sid. */
bool ui_asking(uint32_t sid);
/* The session the WATCH screen is showing, 0 when it is not open. */
uint32_t ui_watched_sid(void);
/* The first unanswered request, if any, else 0. */
uint32_t ui_pending_attention_sid(void);
void ui_toast(const char *text, uint32_t rgb);
/* A toast whose words the vocabulary cannot hold (they carry a version, or come from the flash
 * driver): `words` goes to the report and the log, the glass shows `icons` (one or two ICON_*) and
 * after them `content` (a short number or a version; may be NULL). GLYPHS.md rule 8. */
void ui_toast_sign(const char *words, const char *icons, const char *content, uint32_t rgb);
/* A scroll result arrived (sid 0 = unknown): SCROLL updates its counter, track and preview. */
/* A scroll failed (e.g. `unsupported`): SCROLL closes, back to WATCH. */
void ui_scroll_failed(void);
/* A pointer command failed (err a result_err value): TRACKPAD says why, e.g. the Accessibility
 * permission. */
void ui_pointer_failed(int err, const char *msg);
void ui_scroll_pos(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total,
                   uint32_t total, const char *preview);
/* Brief dim ring for alerts while the display is otherwise idle; text-free by design. */
void ui_glow(uint32_t rgb);
/* BLE pairing (PROTOCOL.md §8): the passkey the PC asks for, as the Bluetooth glyph over six
 * digits, above everything until ui_pair_hide() (or 60 s). */
void ui_pair_show(uint32_t passkey);
void ui_pair_hide(void);
void ui_glow_hide(void);
bool ui_glow_active(void);
/* The wrist was raised: counts as looking at the watch (ends the ambient dim and the nagging), like
 * a touch but without any action. */
void ui_note_motion(void);
/* The display shows the dimmed ambient face (HOME after 30 s without input, nothing waiting): the
 * power report's "dim". */
bool ui_dimmed(void);
void ui_set_animations(bool enabled);
