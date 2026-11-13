/* App: wires HAL input, link, model, gestures, UI and power together. Single-threaded: HAL
 * callbacks only push into queues; core_step() is the only place that touches LVGL. */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "core.h"
#include "debug.h"
#include "detail.h"
#include "feedback.h"
#include "gesture.h"
#include "hal.h"
#include "link.h"
#include "keys.h"
#include "voice.h"
#include "power.h"
#include "power_report.h"
#include "ring.h"
#include "model.h"
#include "motion.h"
#include "ota.h"
#include "prefs.h"
#include "ui.h"
#include "ui_theme.h"

#define FRAME_MAX 4096

typedef struct { hal_link_kind_t kind; uint16_t len; uint8_t data[FRAME_MAX]; } frame_t;
typedef struct { hal_link_kind_t kind; bool connected; } link_evt_t;
typedef struct { uint8_t btn; bool down; uint32_t t_ms; } btn_evt_t;

static gesture_t s_gesture;
static ring_t s_frames, s_link_evts, s_btn_evts, s_motion_evts, s_gestures;
#define FRAME_SLOTS 8
/* The frame queue and the two staging frames are 41 KB of plain data, filled by the transports' tasks (never
 * an ISR) and read by core_step: they live in PSRAM, so internal RAM stays for what must be there (BLE). */
MODEL_EXT_BSS static frame_t s_frame_buf[FRAME_SLOTS];
static size_t frame_used(const void *stored) { return offsetof(frame_t, data) + ((const frame_t *)stored)->len; }
static atomic_flag s_rx_lock = ATOMIC_FLAG_INIT; /* transports may deliver from different tasks */
static atomic_uint s_frames_dropped;
static link_evt_t s_link_buf[8];
static btn_evt_t s_btn_buf[16];
static hal_motion_evt_t s_motion_buf[16];
static gesture_evt_t s_gesture_buf[32];

static uint32_t s_frames_drawn, s_fps, s_fps_t0, s_px_flushed;
static lv_indev_t *s_touch;
static lv_display_t *s_disp_main;

uint32_t app_now_ms(void) { return (uint32_t)(hal_time_us() / 1000); }
uint32_t app_fps(void) { return s_fps; }

/* The RTC sits on I2C (~1 ms per read on the StopWatch) and the UI asks for the time many times per
 * step, so read it every 10 s and count whole seconds on the CPU clock in between. Never steps back. */
#define CLOCK_RESYNC_MS 10000u
static uint32_t s_clk_unix, s_clk_ms, s_clk_last;
static bool s_clk_valid;

uint32_t app_now_unix(void)
{
    uint32_t ms = app_now_ms();
    if (!s_clk_valid || ms - s_clk_ms >= CLOCK_RESYNC_MS) {
        s_clk_unix = hal_rtc_get_unix();
        s_clk_ms = ms;
        s_clk_valid = true;
    }
    if (!s_clk_unix) return s_clk_last = 0;
    uint32_t t = s_clk_unix + (ms - s_clk_ms) / 1000u;
    if (t < s_clk_last) t = s_clk_last;
    return s_clk_last = t;
}

void app_clock_resync(void)
{
    s_clk_valid = false;
    s_clk_last = 0;
}

static uint32_t tick_cb(void) { return app_now_ms(); }

void app_inject_gesture(const gesture_evt_t *g) { ring_push(&s_gestures, g); }
void app_set_sectors(int n) { gesture_set_sectors(&s_gesture, n); }

/* ── HAL callbacks (any context) ── */

static void on_rx(hal_link_kind_t kind, const uint8_t *frame, size_t len)
{
    MODEL_EXT_BSS static frame_t f;
    if (len > FRAME_MAX) return;
    ring_spin_lock(&s_rx_lock);
    f.kind = kind;
    f.len = (uint16_t)len;
    memcpy(f.data, frame, len);
    /* only the message itself is copied, not the 4 KB slot */
    if (!ring_push_n(&s_frames, &f, frame_used(&f))) atomic_fetch_add(&s_frames_dropped, 1);
    ring_spin_unlock(&s_rx_lock);
}

static void on_state(hal_link_kind_t kind, bool connected)
{
    link_evt_t e = {kind, connected};
    ring_push(&s_link_evts, &e);
}

static void on_button(hal_button_t btn, hal_button_edge_t edge, uint64_t t_us)
{
    btn_evt_t e = {(uint8_t)btn, edge == HAL_BTN_DOWN, (uint32_t)(t_us / 1000)};
    ring_push(&s_btn_evts, &e);
}

static void on_motion(const hal_motion_evt_t *evt) { ring_push(&s_motion_evts, evt); }

static void on_gesture(const gesture_evt_t *g, void *ctx)
{
    (void)ctx;
    ring_push(&s_gestures, g);
}

/* The recognizer needs raw pointer samples, which LVGL does not expose as events. Wrap the HAL's
 * read callback, forward each sample, and handle the recognizer's output after lv_timer_handler. */
static lv_indev_read_cb_t s_hal_touch_read;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    s_hal_touch_read(indev, data);
    bool down = data->state == LV_INDEV_STATE_PRESSED;
    /* The panel reports its own coordinates and LVGL turns them for the widgets when the UI is rotated; the
     * recognizer, the bezel dial and the keyboard work on what the user sees, so they get the turned point. */
    int32_t x = data->point.x, y = data->point.y;
    lv_display_t *disp = lv_indev_get_display(indev);
    if (disp && lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_180) {
        x = lv_display_get_horizontal_resolution(disp) - 1 - x;
        y = lv_display_get_vertical_resolution(disp) - 1 - y;
    }
    if (keys_touch(down, x, y, app_now_ms())) return; /* KEYS owns the finger: strokes, not gestures */
    if (!down && !s_gesture.pressed) return;
    gesture_touch(&s_gesture, down, x, y, app_now_ms());
}

static void on_refr_ready(lv_event_t *e)
{
    (void)e;
    s_frames_drawn++;
}

static void on_flush_start(lv_event_t *e)
{
    const lv_area_t *a = lv_event_get_param(e);
    if (a) s_px_flushed += (uint32_t)lv_area_get_size(a);
}

uint32_t core_px_flushed(void) { return s_px_flushed; }

/* ── UI actions → link ── */

static void toast_cmd_result(bool sent, const char *ok_text)
{
    if (sent) ui_toast(ok_text, RGB_ACCENT);
    else ui_toast("Offline", RGB_AMBER);
}

static void act_focus(uint32_t sid)
{
    bool sent = link_send_cmd(LINK_OP_FOCUS, sid, 0, 0);
    if (sent) feedback_tick();
    else feedback_reject();
    toast_cmd_result(sent, "Opening on PC...");
}

/* Open on PC from a request (ASK): focus, and hand the request back to the terminal at once (proto 0.11;
 * an older daemon only focuses, and the terminal asks when its window ends). Never an answer. */
static void act_open_request(uint32_t sid, uint32_t rid)
{
    bool sent = link_send_cmd(LINK_OP_FOCUS, sid, rid, 0);
    if (sent) feedback_tick();
    else feedback_reject();
    toast_cmd_result(sent, "Opening on PC...");
}

static bool send_detail(uint32_t sid, uint32_t rid, uint32_t off) { return link_send_cmd_arg(LINK_OP_DETAIL, sid, rid, off, 0); }

static void act_approve(uint32_t sid, uint32_t rid, uint32_t hold_ms)
{
    bool sent = link_send_cmd(LINK_OP_APPROVE, sid, rid, hold_ms);
    if (sent) {
        model_attention_set_answer(rid, ATT_ANSWER_APPROVE, 0);
        feedback_approved();
    } else {
        feedback_reject();
    }
    if (!sent) toast_cmd_result(false, ""); /* ASK shows the word itself (UI-V3 §4.3) */
}

/* Approve and save the standing rule the watch showed (STANDING-RULES.md): the hash names that rule. */
static void act_grant(uint32_t sid, uint32_t rid, uint32_t hold_ms, uint32_t rule_hash)
{
    bool sent = link_send_approve_rule(sid, rid, hold_ms, rule_hash);
    if (sent) {
        model_attention_set_answer(rid, ATT_ANSWER_GRANT, 0);
        feedback_approved();
    } else {
        feedback_reject();
        toast_cmd_result(false, "");
    }
}

static void act_deny(uint32_t sid, uint32_t rid)
{
    bool sent = link_send_cmd(LINK_OP_DENY, sid, rid, 0);
    if (sent) {
        model_attention_set_answer(rid, ATT_ANSWER_DENY, 0);
        feedback_denied();
    } else {
        feedback_reject();
    }
    if (!sent) toast_cmd_result(false, ""); /* ASK shows the word itself (UI-V3 §4.3) */
}

/* cmd_op_reply {rid, arg: option index}: a question's answer, or one of a permission's own choices. */
static void act_reply(uint32_t sid, uint32_t rid, uint8_t opt, uint32_t hold_ms)
{
    /* hold_ms rides along so the daemon can re-check a grant option's hold, as it does for approve */
    bool sent = link_send_cmd_arg(LINK_OP_REPLY, sid, rid, opt, hold_ms);
    if (sent) {
        model_attention_set_answer(rid, ATT_ANSWER_REPLY, opt);
        feedback_reply_sent();
    } else {
        feedback_reject();
    }
    if (!sent) toast_cmd_result(false, "");
}

/* cmd_op_answer (proto 0.18): every question of an AskUserQuestion request at once, after the last hold. */
static void act_answer(uint32_t sid, uint32_t rid, const uint32_t *picks, const uint32_t *aids, int n, uint32_t hold_ms)
{
    bool sent = link_send_answer(sid, rid, picks, aids, n, hold_ms);
    if (sent) {
        model_attention_set_answer(rid, ATT_ANSWER_QUESTIONS, 0);
        feedback_reply_sent();
    } else {
        feedback_reject();
        toast_cmd_result(false, "");
    }
}

/* The daemon stores seen and echoes it to every device; the local update makes the segment dim at once. */
static void act_ack(uint32_t sid)
{
    link_send_cmd(LINK_OP_ACK, sid, 0, 0);
    session_t d = {.sid = sid, .seen = true};
    model_upsert(&d, SF_SEEN);
}

static void act_snooze(uint32_t sid, uint32_t minutes)
{
    bool sent = link_send_cmd_arg(LINK_OP_SNOOZE, sid, 0, minutes, 0); /* 0 cancels */
    if (sent) feedback_tick();
    else feedback_reject();
    toast_cmd_result(sent, minutes ? "Snoozed 30 min" : "Unsnoozed");
}

static void act_notify_done(uint32_t sid, bool arm)
{
    bool sent = link_send_cmd_arg(LINK_OP_NOTIFY_DONE, sid, 0, arm ? 1 : 0, 0);
    if (sent) feedback_tick();
    else feedback_reject();
    toast_cmd_result(sent, arm ? "Will buzz when done" : "Won't notify");
}

/* Quick reply (issue b150): the device names the reply by index; the daemon holds the text. */
static void act_type_reply(uint32_t sid, int idx, bool queue)
{
    bool sent = idx >= 0 && (queue ? link_send_cmd_queued(LINK_OP_TYPE, sid, (uint32_t)idx, 0)
                                   : link_send_cmd_arg(LINK_OP_TYPE, sid, 0, (uint32_t)idx, 0));
    if (sent) {
        feedback_reply_sent();
        char t[80];
        const session_t *s = model_find(sid);
        const char *text = idx >= 100 ? (s && idx - 100 < s->n_rp ? s->rp[idx - 100] : "") : model_reply(idx);
        snprintf(t, sizeof t, "%s \"%s\"", queue ? "Queued" : "Sent", text);
        ui_toast(t, RGB_SUCCESS);
    } else {
        feedback_reject();
        ui_toast("Offline", RGB_AMBER);
    }
}

/* Cancel queued: the daemon drops the session's queued message and clears its qd. */
static void act_unqueue(uint32_t sid)
{
    bool sent = link_send_cmd(LINK_OP_UNQUEUE, sid, 0, 0);
    if (sent) feedback_tick();
    else feedback_reject();
    toast_cmd_result(sent, "Unqueued");
}

/* Interrupt: Esc in the session's terminal, only after the hold (the daemon checks hold_ms). */
static void act_interrupt(uint32_t sid, uint32_t hold_ms)
{
    bool sent = link_send_cmd(LINK_OP_CANCEL, sid, 0, hold_ms);
    if (sent) feedback_denied();
    else feedback_reject();
    const session_t *s = model_find(sid);
    toast_cmd_result(sent, s && s->agent == AGENT_SHELL ? "Stopping" : "Interrupting");
}

/* Run again: the daemon types the job's own command into its terminal, only after the hold. */
static void act_rerun(uint32_t sid, uint32_t hold_ms)
{
    bool sent = link_send_cmd(LINK_OP_RERUN, sid, 0, hold_ms);
    if (sent) feedback_approved();
    else feedback_reject();
    toast_cmd_result(sent, "Running again");
}

/* Voice: type recording aid's transcript, only after the hold (the daemon checks hold_ms too). With a
 * rid the transcript answers that request instead (proto 0.8); the daemon then clears it. */
static void act_dictate(uint32_t sid, uint32_t rid, uint32_t aid, uint32_t hold_ms, bool queue)
{
    bool sent = queue ? link_send_cmd_queued(LINK_OP_DICTATE, sid, aid, hold_ms)
                      : link_send_cmd_arg(LINK_OP_DICTATE, sid, rid, aid, hold_ms);
    if (sent) {
        if (rid) model_attention_set_answer(rid, ATT_ANSWER_DICTATE, 0);
        feedback_approved();
    } else {
        feedback_reject();
    }
    toast_cmd_result(sent, queue ? "Queued" : "Sent");
    voice_cancel(); /* done with it on the device */
}

static bool act_button_down(int btn) { return btn >= 0 && btn < 2 && s_gesture.btn[btn].down; }

static bool sid_snoozed(uint32_t sid) { return model_session_snoozed(model_find(sid), app_now_unix()); }

/* Entering SCROLL: focus without the "Opening on PC..." toast; the mode-change tick is the feedback. */
static void act_scroll_open(uint32_t sid)
{
    if (link_send_cmd(LINK_OP_FOCUS, sid, 0, 0)) feedback_tick();
    else ui_toast("Offline", RGB_AMBER);
}

static void act_scroll(uint32_t sid, nav_scroll_unit_t unit, int delta)
{
    static uint32_t seq;
    if (!link_send_scroll(sid, (int)unit, delta, ++seq)) ui_toast("Offline", RGB_AMBER);
}

/* The nag is an alert too: quiet mode holds it as it holds the arrival buzz. */
static uint32_t s_muted_rid, s_muted_sid; /* the request a palm cover silenced (0 = none) */

static void act_nag(void)
{
    const attention_t *first = model_attention_first();
    if (first && sid_snoozed(first->sid)) return;
    if (first && s_muted_rid && first->rid == s_muted_rid && first->sid == s_muted_sid) return; /* covered: no reminders */
    if (first && first->hush) return; /* the user is looking at its terminal (PRESENCE.md) */
    if (!model_quiet_active(app_now_unix())) feedback_attention(ATT_APPROVAL);
}

static const ui_actions_t s_actions = {.focus = act_focus,
                                       .open_request = act_open_request,
                                       .approve = act_approve,
                                       .grant = act_grant,
                                       .deny = act_deny,
                                       .reply = act_reply,
                                       .answer = act_answer,
                                       .ack = act_ack,
                                       .snooze = act_snooze,
                                       .notify_done = act_notify_done,
                                       .type_reply = act_type_reply,
                                       .unqueue = act_unqueue,
                                       .interrupt = act_interrupt,
                                       .rerun = act_rerun,
                                       .dictate = act_dictate,
                                       .scroll_open = act_scroll_open,
                                       .scroll = act_scroll,
                                       .scroll_stop = feedback_scroll_stop,
                                       .quiet_toggled = feedback_quiet_toggled,
                                       .tick = feedback_tick,
                                       .detent = feedback_detent,
                                       .hold_start = feedback_hold_ramp,
                                       .deny_hold_start = feedback_deny_ramp,
                                       .hold_stop = feedback_hold_stop,
                                       .reject = feedback_reject,
                                       .nag = act_nag,
                                       .sent = feedback_approved,
                                       .button_down = act_button_down,
                                       .sectors = app_set_sectors};

/* ── link → app ── */

static void on_link(bool welcomed)
{
    if (welcomed) app_clock_resync(); /* the welcome has just set the RTC */
    ui_set_link_state(welcomed);
    ota_on_link(welcomed, app_now_ms());
    preport_on_link(welcomed, app_now_ms());
}

static void on_attention(const attention_t *a)
{
    uint32_t rgb = a->kind == ATT_ERROR ? RGB_ERROR : a->kind == ATT_DONE ? RGB_DONE : a->kind == ATT_QUESTION ? RGB_ASK : RGB_PERMISSION;
    if (model_quiet_active(hal_rtc_get_unix())) { /* once per attention; atlas extract.py matches this line */
        /* Quiet (UI-V2 §4.5): no buzz, no glow, no nag. The card and the ring still change, and a
         * request still opens ASK when the display is already on. */
        dbg_event("ui", "quiet", "\"rid\":%u,\"kind\":%d", (unsigned)a->rid, (int)a->kind);
    } else if (sid_snoozed(a->sid)) {
        dbg_event("ui", "snoozed", "\"rid\":%u,\"kind\":%d", (unsigned)a->rid, (int)a->kind);
    } else if (a->hush) {
        /* The daemon sees the user at the keyboard with this session's terminal in front (PRESENCE.md): no
         * buzz, glow or reminders. The card, ASK and the answers are as ever. */
        dbg_event("ui", "hush", "\"rid\":%u,\"kind\":%d", (unsigned)a->rid, (int)a->kind);
    } else {
        feedback_attention(a->kind);
        if (!motion_face_down()) power_attention(rgb, app_now_ms()); /* face down nobody sees a glow */
        if (a->rid != s_muted_rid || a->sid != s_muted_sid) s_muted_rid = s_muted_sid = 0; /* a new request */
    }
    if (!power_display_on() || ui_glow_active()) return; /* a glow: the first touch reveals it */
    if (a->kind == ATT_DONE) return;
    /* Jump to the new request unless the user is already looking at an unanswered one. */
    ui_report_t r;
    ui_report(&r);
    bool busy_elsewhere = r.has_sel_sid && r.sel_sid != a->sid &&
                          (strcmp(r.screen, "attention") == 0 || strcmp(r.screen, "ask") == 0);
    if (busy_elsewhere) return;
    if (a->kind == ATT_ERROR) ui_show_session(a->sid);
    else ui_show_attention(a->sid); /* a permission or question opens ASK (UI-V2 §2) */
}

/* A session moved into done or error: buzz when it was armed with Notify, or the watch is on it. */
static void on_session(const session_t *before, const session_t *now, uint32_t present)
{
    if (!(present & SF_STATE)) return;
    if (now->state != SESS_DONE && now->state != SESS_ERROR) return;
    if (before && before->state == now->state) return;
    bool armed = before && before->ntf;
    bool job = now->agent == AGENT_SHELL; /* a job is on the watch to say when it ends (JOBS.md) */
    if (!armed && !job && now->sid != ui_watched_sid()) return;
    uint32_t unix_now = app_now_unix();
    if (model_session_snoozed(now, unix_now) || model_quiet_active(unix_now)) return;
    if ((present & SF_HUSH) && now->hush) { /* the user is looking at its terminal (PRESENCE.md) */
        dbg_event("ui", "hush", "\"sid\":%u,\"state\":%d", (unsigned)now->sid, (int)now->state);
        return;
    }
    feedback_finished(now->state == SESS_ERROR);
}

static void on_result(int op, uint32_t sid, uint32_t rid, bool ok, int err, const char *msg)
{
    if (op == LINK_OP_DETAIL) { /* a background fetch: ASK shows what it means, no toast */
        detail_on_result(sid, rid, ok, err);
        return;
    }
    if (ok) {
        /* the focus that opens SCROLL stays quiet: the screen itself is the confirmation */
        if (op == LINK_OP_FOCUS && strcmp(core_screen_name(), "scroll") != 0) ui_toast("Focused", RGB_SUCCESS);
        return;
    }
    /* The daemon refused an answer: the request is unanswered again, so ASK restores its options. */
    if (rid && (op == LINK_OP_APPROVE || op == LINK_OP_DENY || op == LINK_OP_REPLY || op == LINK_OP_DICTATE || op == LINK_OP_ANSWER))
        model_attention_set_answer(rid, ATT_ANSWER_NONE, 0);
    if (op == LINK_OP_SCROLL && sid) {
        if (err == 2) { /* unsupported: this terminal has no scroll API; the item disappears for the session */
            session_t d = {.sid = sid, .no_scroll = true};
            model_upsert(&d, SF_NO_SCROLL);
        }
        ui_scroll_failed(); /* back to WATCH, where the toast explains */
    }
    static const char *const errs[] = {"Out of date", "Denied", "Not supported", "No window", "Timed out", "Failed"};
    const char *text = err >= 0 && err < 6 ? errs[err] : msg[0] ? msg : "Failed";
    /* typing refusals say what to do ("Answer the agent first") */
    if ((op == LINK_OP_TYPE || op == LINK_OP_CANCEL || op == LINK_OP_RERUN || (op >= LINK_OP_KB_PICK && op <= LINK_OP_KB_END)) && err == 1 && msg[0]) text = msg;
    ui_toast(text, err == 3 ? RGB_AMBER : RGB_DANGER);
    feedback_reject();
}

static void on_scroll_pos(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total, uint32_t total,
                          const char *preview)
{
    ui_scroll_pos(sid, lines_above, at_bottom, has_total, total, preview);
}

/* GAUGES.md: one buzz when the 5-hour window first crosses 80 % in a window; quiet mode keeps it silent
 * (and it does not come back later), the alert mode decides as for every other buzz. */
static void on_usage(const usage_t *prev, const usage_t *now)
{
    if (!gauges_should_warn(prev, now)) return;
    bool quiet = model_quiet_active(app_now_unix());
    dbg_event("app", "usage_warn", "\"pct\":%u,\"quiet\":%s", (unsigned)now->h5.pct, quiet ? "true" : "false");
    if (!quiet) feedback_usage_warn();
}

static const link_hooks_t s_hooks = {
    .on_link = on_link, .on_attention = on_attention, .on_result = on_result, .on_scroll_pos = on_scroll_pos, .on_transcript = voice_on_transcript,
    .on_kb_cands = keys_on_cands, .on_usage = on_usage};

/* ── input dispatch ── */

static void log_gesture(const gesture_evt_t *g)
{
    switch (g->kind) {
    case G_PRESS:
    case G_RELEASE: return; /* raw edges are too chatty */
    case G_SCRUB_START:
    case G_SCRUB:
    case G_SCRUB_END:
        dbg_event("input", "scrub", "\"phase\":\"%s\",\"sector\":%d,\"angle\":%d,\"arc\":%u",
                  g->kind == G_SCRUB_START ? "start" : g->kind == G_SCRUB ? "move" : "end", g->x, g->y,
                  (unsigned)g->dur_ms);
        return;
    case G_CHORD: dbg_event("input", "chord", "\"note\":\"reserved, no action\""); return;
    default: dbg_event("input", gesture_name(g->kind), "\"x\":%d,\"y\":%d", g->x, g->y);
    }
}

static bool is_touch_gesture(gesture_kind_t k)
{
    return !(k == G_BTN_A || k == G_BTN_B || k == G_BTN_A_LONG || k == G_BTN_B_LONG || k == G_BTN_A_DOUBLE ||
             k == G_CHORD || k == G_LIFT || k == G_SHAKE);
}

/* The touch that wakes the display (or reveals a glow) is spent on that: every later touch event of
 * the same contact (long press, release, tap, swipe) is dropped until the next G_PRESS, so neither a
 * finger that stays down nor the tap/swipe emitted after its release reaches the UI. */
static bool s_swallow_touch;

static void dispatch(const gesture_evt_t *g)
{
    log_gesture(g);
    if (g->kind == G_LIFT) return;
    bool touch = is_touch_gesture(g->kind);
    if (s_swallow_touch && touch && g->kind != G_PRESS) {
        power_activity(app_now_ms()); /* still activity: the display stays on under the finger */
        return;
    }
    if (g->kind == G_PRESS) s_swallow_touch = false; /* a new contact */
    bool glow = ui_glow_active();
    if (power_activity(app_now_ms())) { /* first touch only wakes the display (a glow: reveals the request) */
        uint32_t sid = glow ? ui_pending_attention_sid() : 0;
        if (sid) ui_show_attention(sid);
        if (touch && s_gesture.pressed) s_swallow_touch = true;
        dbg_event("power", "wake_input", "\"touch\":%s", touch ? "true" : "false");
        return;
    }
    if (ota_active()) { /* the Updating overlay owns the screen: only B held does anything */
        if (g->kind == G_BTN_B_LONG) ota_cancel(app_now_ms());
        return;
    }
    ui_on_gesture(g);
}

static void drain_input(uint32_t now)
{
    btn_evt_t b;
    while (ring_pop(&s_btn_evts, &b)) gesture_button(&s_gesture, b.btn, b.down, b.t_ms);
    hal_motion_evt_t m;
    while (ring_pop(&s_motion_evts, &m)) {
        if (m.kind == HAL_MOTION_TILT) motion_sample(m.x_mg, m.y_mg, m.z_mg, now);
        if (m.kind == HAL_MOTION_LIFT) motion_lift(now);
        gesture_evt_t g = {.kind = m.kind == HAL_MOTION_LIFT ? G_LIFT : G_SHAKE};
        if (m.kind == HAL_MOTION_LIFT || m.kind == HAL_MOTION_SHAKE) ring_push(&s_gestures, &g);
    }
    gesture_tick(&s_gesture, now);
}

static void drain_link(uint32_t now)
{
    link_evt_t e;
    while (ring_pop(&s_link_evts, &e)) link_on_state(e.kind, e.connected);
    MODEL_EXT_BSS static frame_t f;
    while (ring_pop_n(&s_frames, &f, frame_used)) link_on_frame_from(f.kind, f.data, f.len);
    unsigned dropped = atomic_exchange(&s_frames_dropped, 0);
    if (dropped) dbg_event_at(DBG_DEBUG, "link", "rx_drop", "\"n\":%u", dropped);
    link_tick(now);
    /* Welcomed transport, else the first up one while connecting; -1 when offline. */
    ui_set_link_transport(link_connected() ? (int)link_active_kind() : -1);
}

/* ── motion (docs/design/MOTION.md): wake, orientation, palm cover. Never answers, selects or dismisses. ── */

/* Raising the wrist wakes the display the way a touch does: a dark display lights, a glow is revealed, the
 * ambient dim ends. It is not input to the UI, so nothing is pressed, answered or selected. */
static void on_raise(uint32_t now)
{
    bool glow = ui_glow_active();
    ui_note_motion();
    if (power_activity(now)) {
        uint32_t sid = glow ? ui_pending_attention_sid() : 0;
        if (sid) ui_show_attention(sid);
        dbg_event("power", "wake_motion", NULL);
    }
}

/* The UI is turned by 180 degrees when the preference says so, or (automatic) when the watch has been still
 * the other way up for a while. Never in the middle of a touch, a button hold or a stroke. */
static void orientation_step(void)
{
    if (!s_disp_main) return;
    orient_mode_t m = prefs_orient();
    bool want = m == ORIENT_FLIPPED || (m == ORIENT_AUTO && motion_auto_flipped());
    bool now_flipped = lv_display_get_rotation(s_disp_main) == LV_DISPLAY_ROTATION_180;
    if (want == now_flipped) return;
    if (s_gesture.pressed || s_gesture.btn[0].down || s_gesture.btn[1].down || keys_active() || voice_state() == VOICE_REC) return;
    hal_display_set_rotation_180(want);
    lv_obj_invalidate(lv_screen_active());
    dbg_event("ui", "orientation", "\"flipped\":%s,\"mode\":%d", want ? "true" : "false", (int)m);
}

/* A palm over the glass (the controller sees several contacts at once) silences the alert in progress: the
 * motor and the speaker stop and its reminders are dropped. The request itself stays, unanswered. */
static void cover_step(void)
{
    static bool was;
    bool cov = hal_touch_covered();
    if (cov && !was) {
        feedback_silence_now();
        const attention_t *first = model_attention_first();
        if (first) {
            s_muted_rid = first->rid;
            s_muted_sid = first->sid;
        }
        dbg_event("ui", "cover_mute", "\"rid\":%u", (unsigned)s_muted_rid);
    }
    was = cov;
}

/* ── public ── */

void core_set_idle_timeout_ms(uint32_t ms) { power_set_idle_timeout_ms(ms); }
void core_set_deep_after_ms(uint32_t ms) { power_set_deep_after_ms(ms); }
void core_dump_ui_state(void) { dbg_dump_ui_state(); }

bool core_display_flipped(void) { return s_disp_main && lv_display_get_rotation(s_disp_main) == LV_DISPLAY_ROTATION_180; }

const char *core_screen_name(void)
{
    static ui_report_t r;
    ui_report(&r);
    return r.screen;
}

bool core_ui_has_label(const char *text)
{
    static ui_report_t r;
    ui_report(&r);
    for (size_t i = 0; i < r.n_visible; i++)
        if (strstr(r.visible[i], text)) return true;
    return false;
}

void core_init(void)
{
    ring_init(&s_frames, s_frame_buf, sizeof s_frame_buf[0], FRAME_SLOTS);
    ring_init(&s_link_evts, s_link_buf, sizeof s_link_buf[0], 8);
    ring_init(&s_btn_evts, s_btn_buf, sizeof s_btn_buf[0], 16);
    ring_init(&s_motion_evts, s_motion_buf, sizeof s_motion_buf[0], 16);
    ring_init(&s_gestures, s_gesture_buf, sizeof s_gesture_buf[0], 32);
    gesture_init(&s_gesture, on_gesture, NULL);

    const hal_board_info_t *bi = hal_board_info();
    dbg_event("app", "boot", "\"board\":%s,\"fw\":%s,\"w\":%u,\"h\":%u", dbg_q(bi->board), dbg_q(bi->fw_version),
              (unsigned)bi->width, (unsigned)bi->height);

    model_init();
    prefs_init();
    motion_reset();
    model_set_session_hook(on_session);
    power_init();
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = hal_display_create();
    s_disp_main = disp;
    lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);
    lv_display_add_event_cb(disp, on_flush_start, LV_EVENT_FLUSH_START, NULL);
    s_touch = hal_touch_create(disp);
    if (s_touch) {
        s_hal_touch_read = lv_indev_get_read_cb(s_touch);
        lv_indev_set_read_cb(s_touch, touch_read_cb);
        lv_timer_set_period(lv_indev_get_read_timer(s_touch), 10);
    }

    ui_init(&s_actions);
    link_init(&s_hooks);
    detail_reset();
    detail_set_sender(send_detail);
    hal_buttons_set_callback(on_button);
    hal_motion_set_callback(on_motion);
    hal_motion_stream_tilt(true);
    hal_link_start(on_rx, on_state);
#if CORE_BOOT_BUZZ
    if (!power_woke_by_timer()) feedback_hello(); /* a reconnect attempt stays silent and dark */
#endif
    power_start(app_now_ms());
    preport_init(app_now_ms());
    ota_init(app_now_ms());
    s_fps_t0 = app_now_ms();
}

uint32_t core_step(void)
{
    uint32_t now = app_now_ms();
    preport_tick(now); /* first: the time since the last step goes to the state that held during it */
    drain_input(now);
    if (motion_take_raise()) on_raise(now);
    cover_step();
    drain_link(now);
    power_tick(now);
    uint32_t wait = lv_timer_handler();

    gesture_evt_t g;
    while (ring_pop(&s_gestures, &g)) dispatch(&g);
    model_attention_expire(app_now_unix());
    model_attention_expire_ms(now); /* an answer window over with no word from the daemon: back to the PC */
    detail_tick(now);
    voice_tick(now);
    ota_tick(now);
    if (ota_active()) power_activity(now); /* a firmware update keeps the display on */
    keys_tick(now);
    if (keys_active()) power_activity(now); /* the keyboard keeps the display on */
    if (voice_state() == VOICE_REC || voice_state() == VOICE_WAIT) power_activity(now); /* stay awake while dictating */
    ui_tick(now);
    orientation_step();
    dbg_tick(now);

    if (now - s_fps_t0 >= 1000) {
        s_fps = s_frames_drawn * 1000 / (now - s_fps_t0);
        s_frames_drawn = 0;
        s_fps_t0 = now;
    }
    /* Display off: nothing to animate and input is queued by interrupts, so poll slowly; the first
     * button or link frame still lands within 50 ms. */
    if (!power_display_on()) return 50;
    return wait > 10 ? 10 : wait;
}
