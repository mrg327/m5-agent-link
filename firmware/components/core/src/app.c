/* App: wires HAL input, link, model, gestures, UI and power together. Single-threaded: HAL
 * callbacks only push into queues; core_step() is the only place that touches LVGL. */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "bench.h"
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
#include "ui_living.h"
#include "ui_theme.h"

#define FRAME_MAX 4096

typedef struct {
    hal_link_kind_t kind;
    uint16_t len;
    uint8_t data[FRAME_MAX];
} frame_t;
typedef struct {
    hal_link_kind_t kind;
    bool connected;
} link_evt_t;
typedef struct {
    uint8_t btn;
    bool down;
    uint32_t t_ms;
} btn_evt_t;

static gesture_t s_gesture;
static ring_t s_frames, s_link_evts, s_btn_evts, s_motion_evts, s_gestures;
#define FRAME_SLOTS 8
/* The frame queue and the two staging frames are 41 KB of plain data, filled by the transports'
 * tasks (never an ISR) and read by core_step: they live in PSRAM, so internal RAM stays for what
 * must be there (BLE). */
MODEL_EXT_BSS static frame_t s_frame_buf[FRAME_SLOTS];
static size_t frame_used(const void *stored)
{ return offsetof(frame_t, data) + ((const frame_t *)stored)->len; }
/* Transports deliver from different tasks (usb_link, NimBLE host) and the holder copies up to 4 KB,
 * too long for a critical section: a mutex, whose priority inheritance lets a preempted holder
 * finish. */
static ring_mutex_t s_rx_lock;
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
 * step, so read it every 10 s and count whole seconds on the CPU clock in between. Never steps
 * back. */
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

void app_inject_gesture(const gesture_evt_t *g)
{
    gesture_evt_t e = *g;
    e.injected = true;
    ring_push(&s_gestures, &e);
}
void app_set_sectors(int n) { gesture_set_sectors(&s_gesture, n); }

/* ── HAL callbacks (any context) ── */

static void on_rx(hal_link_kind_t kind, const uint8_t *frame, size_t len)
{
    MODEL_EXT_BSS static frame_t f;
    if (len > FRAME_MAX) return;
    ring_mutex_lock(&s_rx_lock);
    f.kind = kind;
    f.len = (uint16_t)len;
    memcpy(f.data, frame, len);
    /* only the message itself is copied, not the 4 KB slot */
    if (!ring_push_n(&s_frames, &f, frame_used(&f))) atomic_fetch_add(&s_frames_dropped, 1);
    ring_mutex_unlock(&s_rx_lock);
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
/* BLE pairing (PROTOCOL.md §8): the board reports from its host task; core_step() takes the latest
 * event over. A passkey then HAL_PAIR_DONE in quick succession just ends it. */
static volatile uint32_t s_pair_seq;
static volatile int32_t s_pair_val;
static uint32_t s_pair_seen;
static void on_pair(int32_t passkey_or_end)
{
    s_pair_val = passkey_or_end;
    s_pair_seq = s_pair_seq + 1;
}

static void pair_step(uint32_t now)
{
    uint32_t seq = s_pair_seq;
    if (seq == s_pair_seen) return;
    s_pair_seen = seq;
    int32_t v = s_pair_val;
    if (v >= 0) {
        power_activity(now); /* the PC waits for these digits: light the display */
        ui_pair_show((uint32_t)v);
        feedback_tick();
        return;
    }
    ui_pair_hide();
    bool done = v == HAL_PAIR_DONE;
    ui_toast_sign(done ? "Paired" : "Pairing failed",
                  done ? ICON_BLUETOOTH ICON_CHECK : ICON_BLUETOOTH ICON_X, NULL,
                  done ? RGB_DONE : RGB_DANGER);
}

static lv_indev_read_cb_t s_hal_touch_read;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    s_hal_touch_read(indev, data);
    bool down = data->state == LV_INDEV_STATE_PRESSED;
    /* The panel reports its own coordinates and LVGL turns them for the widgets when the UI is
     * rotated; the recognizer, the bezel dial and the keyboard work on what the user sees, so they
     * get the turned point. */
    int32_t x = data->point.x, y = data->point.y;
    lv_display_t *disp = lv_indev_get_display(indev);
    bool turned = disp && lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_180;
    int32_t w = disp ? lv_display_get_horizontal_resolution(disp) : 0,
            h = disp ? lv_display_get_vertical_resolution(disp) : 0;
    if (turned) {
        x = w - 1 - x;
        y = h - 1 - y;
    }
    /* A bench stroke (BENCH.md §2) stands in for the panel: LVGL gets it in panel coordinates, as a
     * finger. */
    if (bench_touch(app_now_ms(), &down, &x, &y)) {
        data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->point.x = turned ? w - 1 - x : x;
        data->point.y = turned ? h - 1 - y : y;
        /* no power_activity here: the press reaches dispatch() like a finger's, so on a dark
         * display it only wakes */
    }
    /* KEYS owns the finger: strokes, not gestures */
    if (keys_touch(down, x, y, app_now_ms())) return;
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

/* Open on PC from a request (ASK): focus, and hand the request back to the terminal at once (proto
 * 0.11; an older daemon only focuses, and the terminal asks when its window ends). Never an
 * answer. */
static void act_open_request(uint32_t sid, uint32_t rid)
{
    bool sent = link_send_cmd(LINK_OP_FOCUS, sid, rid, 0);
    if (sent) feedback_tick();
    else feedback_reject();
    toast_cmd_result(sent, "Opening on PC...");
}

static bool send_detail(uint32_t sid, uint32_t rid, uint32_t off)
{ return link_send_cmd_arg(LINK_OP_DETAIL, sid, rid, off, 0); }

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

/* Approve and save the standing rule the watch showed (STANDING-RULES.md): the hash names that
 * rule. */
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

/* cmd_op_reply {rid, arg: option index}: a question's answer, or one of a permission's own
 * choices. */
static void act_reply(uint32_t sid, uint32_t rid, uint8_t opt, uint32_t hold_ms,
                      ui_reply_kind_t kind)
{
    /* hold_ms rides along so the daemon can re-check a grant option's hold, as it does for
     * approve */
    bool sent = link_send_cmd_arg(LINK_OP_REPLY, sid, rid, opt, hold_ms);
    if (sent) {
        model_attention_set_answer(rid, ATT_ANSWER_REPLY, opt);
        /* what grants ends in one long pulse, what denies in two (UI-V2 §5) */
        switch (kind) {
        case UI_REPLY_GRANT: feedback_approved(); break;
        case UI_REPLY_DENY: feedback_denied(); break;
        case UI_REPLY_NEUTRAL:
        default: feedback_reply_sent(); break;
        }
    } else {
        feedback_reject();
    }
    if (!sent) toast_cmd_result(false, "");
}

/* cmd_op_answer (proto 0.18): every question of an AskUserQuestion request at once, after the last
 * hold. */
static void act_answer(uint32_t sid, uint32_t rid, const uint32_t *picks, const uint32_t *aids,
                       int n, uint32_t hold_ms)
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

/* The daemon stores seen and echoes it to every device; the local update makes the segment dim at
 * once. */
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
        const char *text =
            idx >= 100 ? (s && idx - 100 < s->n_rp ? s->rp[idx - 100] : "") : model_reply(idx);
        snprintf(t, sizeof t, "Sent \"%s\"", text);
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

/* Voice: type recording aid's transcript, only after the hold (the daemon checks hold_ms too). With
 * a rid the transcript answers that request instead (proto 0.8); the daemon then clears it. */
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
    toast_cmd_result(sent, "Sent");
    voice_cancel(); /* done with it on the device */
}

static bool act_button_down(int btn) { return btn >= 0 && btn < 2 && s_gesture.btn[btn].down; }
static bool act_touching(void) { return s_gesture.pressed; }

static bool sid_snoozed(uint32_t sid)
{ return model_session_snoozed(model_find(sid), app_now_unix()); }

/* Entering SCROLL: focus without the "Opening on PC..." toast; the mode-change tick is the
 * feedback. */
static void act_scroll_open(uint32_t sid)
{
    if (link_send_cmd(LINK_OP_FOCUS, sid, 0, 0)) feedback_tick();
    else ui_toast("Offline", RGB_AMBER);
}

/* GRAPH: a background fetch (GRAPH.md §4), silent when it cannot go out; the screen asks again. */
static void act_graph(uint32_t sid) { (void)link_send_graph(sid); }

static void act_scroll(uint32_t sid, nav_scroll_unit_t unit, int delta)
{
    static uint32_t seq;
    if (!link_send_scroll(sid, (int)unit, delta, ++seq)) ui_toast("Offline", RGB_AMBER);
}

static bool act_pointer(int dx, int dy, int btn, int n, int wheel)
{ return link_send_pointer(dx, dy, btn, n, wheel); }
static void act_moves(bool on) { gesture_set_moves(&s_gesture, on); }
static void act_raw_keys(bool on) { gesture_set_raw_keys(&s_gesture, on); }

/* The nag is an alert too: quiet mode holds it as it holds the arrival buzz. */
static uint32_t s_muted_rid, s_muted_sid; /* the request a palm cover silenced (0 = none) */

static void act_nag(void)
{
    const attention_t *first = model_attention_first();
    if (first && sid_snoozed(first->sid)) return;
    /* covered: no reminders */
    if (first && s_muted_rid && first->rid == s_muted_rid && first->sid == s_muted_sid) return;
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
                                       .graph = act_graph,
                                       .quiet_toggled = feedback_quiet_toggled,
                                       .volume_changed = feedback_volume_preview,
                                       .tick = feedback_tick,
                                       .detent = feedback_detent,
                                       .hold_start = feedback_hold_ramp,
                                       .deny_hold_start = feedback_deny_ramp,
                                       .hold_stop = feedback_hold_stop,
                                       .reject = feedback_reject,
                                       .nag = act_nag,
                                       .sent = feedback_approved,
                                       .button_down = act_button_down,
                                       .touching = act_touching,
                                       .sectors = app_set_sectors,
                                       .pointer = act_pointer,
                                       .moves = act_moves,
                                       .raw_keys = act_raw_keys,
                                       .toy_buzz = feedback_toy_buzz,
                                       .toy_detent = feedback_toy_detent,
                                       .toy_play = feedback_toy_play,
                                       .toy_mic = feedback_toy_mic,
                                       .toy_interrupt = feedback_toy_interrupt};

/* ── link → app ── */

static void on_link(bool welcomed)
{
    if (welcomed) app_clock_resync(); /* the welcome has just set the RTC */
    ui_set_link_state(welcomed);
    ota_on_link(welcomed, app_now_ms());
    preport_on_link(welcomed, app_now_ms());
}

static void on_attention(const attention_t *a, bool is_new)
{
    if (!is_new) {
        /* A re-send (a resync, a reconnect) of a request the watch holds: the card has the new
         * time left and hush; nothing buzzes, glows or takes over again (issue 20261126-d95a). */
        dbg_event("ui", "resent", "\"rid\":%u,\"kind\":%d", (unsigned)a->rid, (int)a->kind);
        if ((a->kind == ATT_APPROVAL || a->kind == ATT_QUESTION) && !a->answer_sent &&
            power_display_on() && !ui_glow_active()) {
            ui_resent_attention(a->sid, a->rid, a->kind == ATT_QUESTION ? RGB_ASK : RGB_PERMISSION);
        }
        return;
    }
    uint32_t rgb = a->kind == ATT_ERROR      ? RGB_ERROR
                   : a->kind == ATT_DONE     ? RGB_DONE
                   : a->kind == ATT_QUESTION ? RGB_ASK
                                             : RGB_PERMISSION;
    if (model_quiet_active(hal_rtc_get_unix())) {
        /* once per attention; atlas extract.py matches this line */
        /* Quiet (UI-V2 §4.5): no buzz, no glow, no nag. The card and the ring still change, and a
         * request still opens ASK when the display is already on. */
        dbg_event("ui", "quiet", "\"rid\":%u,\"kind\":%d", (unsigned)a->rid, (int)a->kind);
    } else if (sid_snoozed(a->sid)) {
        dbg_event("ui", "snoozed", "\"rid\":%u,\"kind\":%d", (unsigned)a->rid, (int)a->kind);
    } else if (a->hush) {
        /* The daemon sees the user at the keyboard with this session's terminal in front
         * (PRESENCE.md): no buzz, glow or reminders. The card, ASK and the answers are as ever. */
        dbg_event("ui", "hush", "\"rid\":%u,\"kind\":%d", (unsigned)a->rid, (int)a->kind);
    } else {
        feedback_attention(a->kind);
        /* face down nobody sees a glow */
        if (!motion_face_down()) power_attention(rgb, app_now_ms());
        /* a new request */
        if (a->rid != s_muted_rid || a->sid != s_muted_sid) s_muted_rid = s_muted_sid = 0;
    }
    if (a->kind == ATT_DONE) return;
    bool error = a->kind == ATT_ERROR;
    if (!power_display_on() || ui_glow_active()) {
        /* nobody saw it: the touch or the raised wrist that wakes the display shows it (issue
         * 20261122-5f79) */
        ui_wait_attention(a->sid, error, rgb);
        return;
    }
    /* The deck already shows another unanswered request: the new one is a segment on the ring, and
     * opens once the user has left the deck (answered that one, say). */
    ui_report_t r;
    ui_report(&r);
    if (r.has_sel_sid && r.sel_sid != a->sid && strcmp(r.screen, "attention") == 0) {
        ui_wait_attention(a->sid, error, rgb);
        return;
    }
    /* ASK (or the card, for an error) opens unless the user composes, reads or answers another
     * request: then it waits (UI-V2 §2, issue 20261122-883f) */
    ui_offer_attention(a->sid, error, rgb);
}

/* Under DONE_BUZZ_ALL (issue 20261126-27b7): a turn shorter than this ends silently, unless it was
 * armed, a job or watched; an error always buzzes. Finishes closer together than DONE_BURST_MS buzz
 * once (the ring shows the rest). */
#define DONE_MIN_S 30
#define DONE_BURST_MS 3000u
static uint32_t s_finish_ms; /* when the last finish buzzed */
static bool s_finished;      /* one has buzzed since boot */

/* How long the turn that just ended ran, in seconds: the daemon's count (session.dur, proto 0.14),
 * which comes with the delta that enters done, else the time since the last state change. -1 =
 * unknown (no clock, or no earlier state). */
static int64_t turn_secs(const session_t *before, const session_t *now, uint32_t unix_now)
{
    if (now->sum.secs_known) return now->sum.secs;
    if (!before || !before->tst || !unix_now) return -1;
    return (int64_t)unix_now - (int64_t)before->tst;
}

/* Under DONE_BUZZ_ALL: one buzz per burst of finishes, and a dark display glows as for a request
 * (face down nobody sees it). No takeover and no reminders: the card and the ring say the rest. */
static void finished_all(bool error, uint32_t sid)
{
    uint32_t ms = app_now_ms();
    if (s_finished && ms - s_finish_ms < DONE_BURST_MS) {
        dbg_event("ui", "finish_burst", "\"sid\":%u", (unsigned)sid);
    } else {
        feedback_finished(error);
        s_finish_ms = ms;
        s_finished = true;
    }
    if (!motion_face_down()) power_attention(error ? RGB_ERROR : RGB_DONE, ms);
}

/* A session moved into done or error. It buzzes when it was armed with Notify, is a job or is open
 * in WATCH; under DONE_BUZZ_ALL also when it failed or its turn ran DONE_MIN_S or more. Quiet,
 * snooze and presence hush hold it. */
static void on_session(const session_t *before, const session_t *now, uint32_t present)
{
    if (!(present & SF_STATE)) return;
    if (now->state != SESS_DONE && now->state != SESS_ERROR) return;
    if (before && before->state == now->state) return;
    bool error = now->state == SESS_ERROR;
    bool armed = before && before->ntf;
    bool job = now->agent == AGENT_SHELL; /* a job is on the watch to say when it ends (JOBS.md) */
    bool all = model_settings()->done_buzz == DONE_BUZZ_ALL;
    uint32_t unix_now = app_now_unix();
    if (!armed && !job && now->sid != ui_watched_sid()) {
        /* no earlier state: a session first seen finished buzzes for nothing it did meanwhile */
        if (!all || !before) return;
        int64_t secs = turn_secs(before, now, unix_now);
        if (!error && secs >= 0 && secs < DONE_MIN_S) {
            dbg_event("ui", "short_turn", "\"sid\":%u,\"secs\":%d", (unsigned)now->sid, (int)secs);
            return;
        }
    }
    if (model_session_snoozed(now, unix_now) || model_quiet_active(unix_now)) return;
    if ((present & SF_HUSH) && now->hush) { /* the user is looking at its terminal (PRESENCE.md) */
        dbg_event("ui", "hush", "\"sid\":%u,\"state\":%d", (unsigned)now->sid, (int)now->state);
        return;
    }
    if (all) {
        finished_all(error, now->sid);
    } else {
        feedback_finished(error); /* as before proto 0.25 */
    }
}

static void on_result(int op, uint32_t sid, uint32_t rid, bool ok, int err, const char *msg)
{
    /* a background fetch: stale means the session is gone, GRAPH closes with it */
    if (op == LINK_OP_GRAPH) return;
    if (op == LINK_OP_DETAIL) { /* a background fetch: ASK shows what it means, no toast */
        detail_on_result(sid, rid, ok, err);
        return;
    }
    if (ok) {
        /* the focus that opens SCROLL stays quiet: the screen itself is the confirmation */
        if (op == LINK_OP_FOCUS && strcmp(core_screen_name(), "scroll") != 0)
            ui_toast("Focused", RGB_SUCCESS);
        return;
    }
    /* The daemon refused an answer: the request is unanswered again, so ASK restores its
     * options, and the request comes back on its own session wherever the user has moved since
     * (issue 20261122-4b25): the refusal below shows over it, not over another card. */
    if (rid && (op == LINK_OP_APPROVE || op == LINK_OP_DENY || op == LINK_OP_REPLY ||
                op == LINK_OP_DICTATE || op == LINK_OP_ANSWER)) {
        model_attention_set_answer(rid, ATT_ANSWER_NONE, 0);
        const attention_t *a = model_attention_find(rid);
        if (a && !ui_asking(a->sid)) {
            uint32_t rgb = a->kind == ATT_QUESTION ? RGB_ASK : RGB_PERMISSION;
            if (!power_display_on() || ui_glow_active()) ui_wait_attention(a->sid, false, rgb);
            else ui_offer_attention(a->sid, false, rgb);
        }
    }
    if (op == LINK_OP_SCROLL && sid) {
        if (err == RESULT_ERR_UNSUPPORTED) {
            /* unsupported: this terminal has no scroll API; the item disappears for the session */
            session_t d = {.sid = sid, .no_scroll = true};
            model_upsert(&d, SF_NO_SCROLL);
        }
        ui_scroll_failed(); /* back to WATCH, where the toast explains */
    }
    if (op == LINK_OP_POINTER) {
        /* TRACKPAD says why (e.g. the Accessibility permission); one toast */
        ui_pointer_failed(err, msg);
        ui_toast(err == RESULT_ERR_DENIED        ? "Not allowed on the PC"
                 : err == RESULT_ERR_UNSUPPORTED ? "Not supported"
                                                 : "Failed",
                 RGB_AMBER);
        feedback_reject();
        return;
    }
    static const char *const errs[] = {"Out of date", "Denied",    "Not supported",
                                       "No window",   "Timed out", "Failed"};
    const char *text = err >= 0 && err <= RESULT_ERR_INTERNAL ? errs[err] : msg[0] ? msg : "Failed";
    /* typing refusals say what to do ("Answer the agent first") */
    if ((op == LINK_OP_TYPE || op == LINK_OP_CANCEL || op == LINK_OP_RERUN ||
         (op >= LINK_OP_KB_PICK && op <= LINK_OP_KB_END)) &&
        err == RESULT_ERR_DENIED && msg[0])
        text = msg;
    ui_toast(text, err == RESULT_ERR_NO_WINDOW ? RGB_AMBER : RGB_DANGER);
    feedback_reject();
}

static void on_scroll_pos(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total,
                          uint32_t total, const char *preview)
{ ui_scroll_pos(sid, lines_above, at_bottom, has_total, total, preview); }

/* GAUGES.md: one buzz when the 5-hour window first crosses 80 % in a window; quiet mode keeps it
 * silent (and it does not come back later), the alert mode decides as for every other buzz. */
static void on_usage(const usage_t *prev, const usage_t *now)
{
    if (!gauges_should_warn(prev, now)) return;
    bool quiet = model_quiet_active(app_now_unix());
    dbg_event("app", "usage_warn", "\"pct\":%u,\"quiet\":%s", (unsigned)now->h5.pct,
              quiet ? "true" : "false");
    if (!quiet) feedback_usage_warn();
}

static const link_hooks_t s_hooks = {.on_link = on_link,
                                     .on_attention = on_attention,
                                     .on_result = on_result,
                                     .on_scroll_pos = on_scroll_pos,
                                     .on_transcript = voice_on_transcript,
                                     .on_kb_cands = keys_on_cands,
                                     .on_usage = on_usage};

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
                  g->kind == G_SCRUB_START ? "start"
                  : g->kind == G_SCRUB     ? "move"
                                           : "end",
                  g->x, g->y, (unsigned)g->dur_ms);
        return;
    case G_CHORD: dbg_event("input", "chord", "\"note\":\"reserved, no action\""); return;
    default: dbg_event("input", gesture_name(g->kind), "\"x\":%d,\"y\":%d", g->x, g->y);
    }
}

static bool is_touch_gesture(gesture_kind_t k)
{
    return !(k == G_BTN_A || k == G_BTN_B || k == G_BTN_A_LONG || k == G_BTN_B_LONG ||
             k == G_BTN_A_DOUBLE || k == G_CHORD || k == G_LIFT || k == G_SHAKE ||
             k == G_KEY_DOWN || k == G_KEY_UP);
}

/* The touch that wakes the display (or reveals a glow) is spent on that: every later touch event of
 * the same contact (long press, release, tap, swipe) is dropped until the next G_PRESS, so neither
 * a finger that stays down nor the tap/swipe emitted after its release reaches the UI. */
static bool s_swallow_touch;

/* The screen changed under a finger that is still down, and not by that finger (a request took
 * over, a screen closed itself): the rest of the contact is not for the new screen (issue
 * 20261122-09b8). `seq` is ui_mode_seq() from before. */
static void swallow_if_moved(uint32_t seq)
{
    if (ui_mode_seq() != seq && s_gesture.pressed) s_swallow_touch = true;
}

/* When the contact or the press behind gesture g began, on the app_now_ms clock. */
static uint32_t gesture_began(const gesture_evt_t *g)
{
    const gesture_btn_t *a = &s_gesture.btn[0], *b = &s_gesture.btn[1];
    switch (g->kind) {
    case G_BTN_A:
    case G_BTN_A_LONG:
    case G_BTN_A_DOUBLE: return a->t_down;
    case G_BTN_B:
    case G_BTN_B_LONG: return b->t_down;
    case G_CHORD: return (int32_t)(a->t_down - b->t_down) < 0 ? a->t_down : b->t_down;
    case G_KEY_DOWN:
    case G_KEY_UP: return g->x ? b->t_down : a->t_down;
    default: return s_gesture.t_down;
    }
}

static void dispatch(const gesture_evt_t *g)
{
    log_gesture(g);
    bench_note(BENCH_GESTURE, (uint32_t)g->kind);
    if (g->kind == G_LIFT) return;
    bool touch = is_touch_gesture(g->kind);
    if (s_swallow_touch && touch && g->kind != G_PRESS) {
        power_activity(app_now_ms()); /* still activity: the display stays on under the finger */
        return;
    }
    if (g->kind == G_PRESS) s_swallow_touch = false; /* a new contact */
    if (power_activity(app_now_ms())) {
        /* first touch only wakes the display and shows what buzzed while it was dark */
        ui_reveal_attention();
        if (touch && s_gesture.pressed) s_swallow_touch = true;
        dbg_event("power", "wake_input", "\"touch\":%s", touch ? "true" : "false");
        return;
    }
    if (ota_active()) { /* the Updating overlay owns the screen: only B held does anything */
        if (g->kind == G_BTN_B_LONG) ota_cancel(app_now_ms());
        return;
    }
    /* A request took over just now: what began before, or in the first moments after, was aimed
     * at the screen it replaced (issue 20261126-86cd). */
    if (!g->injected && g->kind != G_SHAKE && ui_input_settling(gesture_began(g))) {
        if (g->kind != G_PRESS && g->kind != G_RELEASE && g->kind != G_MOVE) {
            dbg_event("input", "settling", "\"gesture\":\"%s\"", gesture_name(g->kind));
        }
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

/* ── motion (docs/design/MOTION.md): wake, orientation, palm cover. Never answers, selects or
 * dismisses. ── */

/* Raising the wrist wakes the display the way a touch does: a dark display lights, a glow is
 * revealed, the ambient dim ends. It is not input to the UI, so nothing is pressed, answered or
 * selected. */
static void on_raise(uint32_t now)
{
    ui_note_motion();
    if (power_activity(now)) {
        ui_reveal_attention();
        dbg_event("power", "wake_motion", NULL);
    }
}

/* The UI is turned by 180 degrees when the preference says so, or (automatic) when the watch has
 * been still the other way up for a while. Never in the middle of a touch, a button hold or a
 * stroke. */
static void orientation_step(void)
{
    if (!s_disp_main) return;
    orient_mode_t m = prefs_orient();
    bool want = m == ORIENT_FLIPPED || (m == ORIENT_AUTO && motion_auto_flipped());
    bool now_flipped = lv_display_get_rotation(s_disp_main) == LV_DISPLAY_ROTATION_180;
    if (want == now_flipped) return;
    if (s_gesture.pressed || s_gesture.btn[0].down || s_gesture.btn[1].down || keys_active() ||
        voice_state() == VOICE_REC)
        return;
    hal_display_set_rotation_180(want);
    lv_obj_invalidate(lv_screen_active());
    dbg_event("ui", "orientation", "\"flipped\":%s,\"mode\":%d", want ? "true" : "false", (int)m);
}

/* A palm over the glass (the controller sees several contacts at once) silences the alert in
 * progress: the motor and the speaker stop and its reminders are dropped. The request itself stays,
 * unanswered. */
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

bool core_display_flipped(void)
{ return s_disp_main && lv_display_get_rotation(s_disp_main) == LV_DISPLAY_ROTATION_180; }

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
    ring_mutex_init(&s_rx_lock);
    ring_init(&s_frames, s_frame_buf, sizeof s_frame_buf[0], FRAME_SLOTS);
    ring_init(&s_link_evts, s_link_buf, sizeof s_link_buf[0], 8);
    ring_init(&s_btn_evts, s_btn_buf, sizeof s_btn_buf[0], 16);
    ring_init(&s_motion_evts, s_motion_buf, sizeof s_motion_buf[0], 16);
    ring_init(&s_gestures, s_gesture_buf, sizeof s_gesture_buf[0], 32);
    gesture_init(&s_gesture, on_gesture, NULL);

    const hal_board_info_t *bi = hal_board_info();
    dbg_event("app", "boot", "\"board\":%s,\"fw\":%s,\"w\":%u,\"h\":%u", dbg_q(bi->board),
              dbg_q(bi->fw_version), (unsigned)bi->width, (unsigned)bi->height);

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
    bench_init(disp);
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
    hal_link_set_pair_callback(on_pair);
    hal_link_start(on_rx, on_state);
#if CORE_BOOT_BUZZ
    if (!power_woke_by_timer()) feedback_hello(); /* a reconnect attempt stays silent and dark */
#endif
    power_start(app_now_ms());
    preport_init(app_now_ms());
    ota_init(app_now_ms());
    s_fps_t0 = app_now_ms();
}

/* Living marks (issue 20261126-98a6): the persona gate's view of this step, from the glue that can
 * see power, motion and the model; the marks then move by it. */
static void persona_step(uint32_t now)
{
    persona_ctx_t c = {0};
    model_counts_t n = model_counts();
    uint32_t unix_now = app_now_unix();
    c.needs_you = (uint8_t)((model_attention_first() ? 1 : 0) + (n.waiting ? 1 : 0));
    c.alert = (uint8_t)model_settings()->alert;
    c.quiet = model_quiet_active(unix_now);
    c.face_down = motion_face_down();
    c.usb = hal_battery_read().usb_present;
    c.display_on = power_display_on() && !ui_glow_active();
    c.ambient = ui_dimmed();
    c.fun = prefs_fun();
    ui_living_tick(&c, now);
}

uint32_t core_step(void)
{
    uint32_t now = app_now_ms();
    /* first: the time since the last step goes to the state that held during it */
    preport_tick(now);
    drain_input(now);
    uint32_t seq = ui_mode_seq();
    if (motion_take_raise()) on_raise(now);
    cover_step();
    drain_link(now);
    pair_step(now);
    power_tick(now);
    swallow_if_moved(seq);
    uint32_t wait = lv_timer_handler();

    gesture_evt_t g;
    while (ring_pop(&s_gestures, &g)) dispatch(&g);
    model_attention_expire(app_now_unix());
    /* an answer window over with no word from the daemon: back to the PC */
    model_attention_expire_ms(now);
    detail_tick(now);
    voice_tick(now);
    ota_tick(now);
    if (ota_active()) power_activity(now); /* a firmware update keeps the display on */
    keys_tick(now);
    if (keys_active()) power_activity(now); /* the keyboard keeps the display on */
    /* stay awake while dictating */
    if (voice_state() == VOICE_REC || voice_state() == VOICE_WAIT) power_activity(now);
    seq = ui_mode_seq();
    ui_tick(now);
    persona_step(now);
    swallow_if_moved(seq);
    orientation_step();
    dbg_tick(now);
    bench_tick(now);

    if (now - s_fps_t0 >= 1000) {
        s_fps = s_frames_drawn * 1000 / (now - s_fps_t0);
        s_frames_drawn = 0;
        s_fps_t0 = now;
    }
    /* Display off: nothing to animate and input is queued by interrupts, so poll slowly; the first
     * button or link frame still lands within 50 ms. */
    if (ota_working()) return 1; /* a delta update rebuilds the image a slice per pass */
    if (!power_display_on()) return 50;
    return wait > 10 ? 10 : wait;
}
