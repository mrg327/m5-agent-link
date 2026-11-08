/* App: wires HAL input, link, model, gestures, UI and power together. Single-threaded: HAL
 * callbacks only push into queues; core_step() is the only place that touches LVGL. */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "core.h"
#include "debug.h"
#include "feedback.h"
#include "gesture.h"
#include "hal.h"
#include "link.h"
#include "power.h"
#include "ring.h"
#include "model.h"
#include "ui.h"
#include "ui_theme.h"

#define FRAME_MAX 4096

typedef struct { hal_link_kind_t kind; uint16_t len; uint8_t data[FRAME_MAX]; } frame_t;
typedef struct { hal_link_kind_t kind; bool connected; } link_evt_t;
typedef struct { uint8_t btn; bool down; uint32_t t_ms; } btn_evt_t;

static gesture_t s_gesture;
static ring_t s_frames, s_link_evts, s_btn_evts, s_motion_evts, s_gestures;
#define FRAME_SLOTS 8
static frame_t s_frame_buf[FRAME_SLOTS]; /* 32 KiB .bss */
static atomic_flag s_rx_lock = ATOMIC_FLAG_INIT; /* transports may deliver from different tasks */
static atomic_uint s_frames_dropped;
static link_evt_t s_link_buf[8];
static btn_evt_t s_btn_buf[16];
static hal_motion_evt_t s_motion_buf[8];
static gesture_evt_t s_gesture_buf[32];

static uint32_t s_frames_drawn, s_fps, s_fps_t0;
static lv_indev_t *s_touch;

uint32_t app_now_ms(void) { return (uint32_t)(hal_time_us() / 1000); }
uint32_t app_fps(void) { return s_fps; }
static uint32_t tick_cb(void) { return app_now_ms(); }

void app_inject_gesture(const gesture_evt_t *g) { ring_push(&s_gestures, g); }
void app_set_sectors(int n) { gesture_set_sectors(&s_gesture, n); }

/* ── HAL callbacks (any context) ── */

static void on_rx(hal_link_kind_t kind, const uint8_t *frame, size_t len)
{
    static frame_t f;
    if (len > FRAME_MAX) return;
    while (atomic_flag_test_and_set_explicit(&s_rx_lock, memory_order_acquire)) {
    }
    f.kind = kind;
    f.len = (uint16_t)len;
    memcpy(f.data, frame, len);
    if (!ring_push(&s_frames, &f)) atomic_fetch_add(&s_frames_dropped, 1);
    atomic_flag_clear_explicit(&s_rx_lock, memory_order_release);
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
    if (!down && !s_gesture.pressed) return;
    gesture_touch(&s_gesture, down, data->point.x, data->point.y, app_now_ms());
}

static void on_refr_ready(lv_event_t *e)
{
    (void)e;
    s_frames_drawn++;
}

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

static void act_approve(uint32_t sid, uint32_t rid, uint32_t hold_ms)
{
    bool sent = link_send_cmd(LINK_OP_APPROVE, sid, rid, hold_ms);
    if (sent) {
        model_attention_set_answer(rid, ATT_ANSWER_APPROVE, 0);
        feedback_approved();
    } else {
        feedback_reject();
    }
    toast_cmd_result(sent, "Approved");
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
    toast_cmd_result(sent, "Denied");
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
    toast_cmd_result(sent, "Sent");
}

/* The daemon only answers ack; there is no stored state. So the device remembers it: the segment dims. */
static void act_ack(uint32_t sid)
{
    link_send_cmd(LINK_OP_ACK, sid, 0, 0);
    session_t d = {.sid = sid, .seen = true};
    model_upsert(&d, SF_SEEN);
}

static void act_snooze(uint32_t sid)
{
    bool sent = link_send_cmd_arg(LINK_OP_SNOOZE, sid, 0, 30, 0); /* minutes, stated rather than defaulted */
    if (sent) feedback_tick();
    else feedback_reject();
    toast_cmd_result(sent, "Snoozed 30 min");
}

static void act_notify_done(uint32_t sid)
{
    bool sent = link_send_cmd(LINK_OP_NOTIFY_DONE, sid, 0, 0);
    if (sent) feedback_tick();
    else feedback_reject();
    toast_cmd_result(sent, "Will buzz when done");
}

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
static void act_nag(void)
{
    if (!model_quiet_active(hal_rtc_get_unix())) feedback_attention(ATT_APPROVAL);
}

static const ui_actions_t s_actions = {.focus = act_focus,
                                       .approve = act_approve,
                                       .deny = act_deny,
                                       .reply = act_reply,
                                       .ack = act_ack,
                                       .snooze = act_snooze,
                                       .notify_done = act_notify_done,
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
                                       .sectors = app_set_sectors};

/* ── link → app ── */

static void on_link(bool welcomed) { ui_set_link_state(welcomed); }

static void on_attention(const attention_t *a)
{
    uint32_t rgb = a->kind == ATT_ERROR ? RGB_ERROR : a->kind == ATT_DONE ? RGB_DONE : a->kind == ATT_QUESTION ? RGB_ASK : RGB_PERMISSION;
    if (model_quiet_active(hal_rtc_get_unix())) {
        /* Quiet (UI-V2 §4.5): no buzz, no glow, no nag. The card and the ring still change, and a
         * request still opens ASK when the display is already on. */
        dbg_event("ui", "quiet", "\"rid\":%u,\"kind\":%d", (unsigned)a->rid, (int)a->kind);
    } else {
        feedback_attention(a->kind);
        power_attention(rgb, app_now_ms());
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

static void on_result(int op, uint32_t sid, uint32_t rid, bool ok, int err, const char *msg)
{
    if (ok) {
        /* the focus that opens SCROLL stays quiet: the screen itself is the confirmation */
        if (op == LINK_OP_FOCUS && strcmp(core_screen_name(), "scroll") != 0) ui_toast(LV_SYMBOL_OK " Focused", RGB_SUCCESS);
        return;
    }
    /* The daemon refused an answer: the request is unanswered again, so ASK restores its options. */
    if (rid && (op == LINK_OP_APPROVE || op == LINK_OP_DENY || op == LINK_OP_REPLY))
        model_attention_set_answer(rid, ATT_ANSWER_NONE, 0);
    if (op == LINK_OP_SCROLL && sid) {
        if (err == 2) { /* unsupported: this terminal has no scroll API; the item disappears for the session */
            session_t d = {.sid = sid, .no_scroll = true};
            model_upsert(&d, SF_NO_SCROLL);
        }
        ui_show_session(sid); /* back to the deck, where the toast explains */
    }
    static const char *const errs[] = {"Out of date", "Denied", "Not supported", "No window", "Timed out", "Failed"};
    const char *text = err >= 0 && err < 6 ? errs[err] : msg[0] ? msg : "Failed";
    ui_toast(text, err == 3 ? RGB_AMBER : RGB_DANGER);
    feedback_reject();
}

static void on_scroll_pos(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total, uint32_t total,
                          const char *preview)
{
    ui_scroll_pos(sid, lines_above, at_bottom, has_total, total, preview);
}

static const link_hooks_t s_hooks = {
    .on_link = on_link, .on_attention = on_attention, .on_result = on_result, .on_scroll_pos = on_scroll_pos};

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

static void dispatch(const gesture_evt_t *g)
{
    log_gesture(g);
    if (g->kind == G_LIFT) return;
    bool glow = ui_glow_active();
    if (power_activity(app_now_ms())) { /* first touch only wakes the display (a glow: reveals the request) */
        uint32_t sid = glow ? ui_pending_attention_sid() : 0;
        if (sid) ui_show_attention(sid);
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
        gesture_evt_t g = {.kind = m.kind == HAL_MOTION_LIFT ? G_LIFT : G_SHAKE};
        if (m.kind == HAL_MOTION_LIFT || m.kind == HAL_MOTION_SHAKE) ring_push(&s_gestures, &g);
    }
    gesture_tick(&s_gesture, now);
}

static void drain_link(uint32_t now)
{
    link_evt_t e;
    while (ring_pop(&s_link_evts, &e)) link_on_state(e.kind, e.connected);
    static frame_t f;
    while (ring_pop(&s_frames, &f)) link_on_frame_from(f.kind, f.data, f.len);
    unsigned dropped = atomic_exchange(&s_frames_dropped, 0);
    if (dropped) dbg_event_at(DBG_DEBUG, "link", "rx_drop", "\"n\":%u", dropped);
    link_tick(now);
    /* Welcomed transport, else the first up one while connecting; -1 when offline. */
    ui_set_link_transport(link_connected() ? (int)link_active_kind() : -1);
}

/* ── public ── */

void core_set_idle_timeout_ms(uint32_t ms) { power_set_idle_timeout_ms(ms); }
void core_set_deep_after_ms(uint32_t ms) { power_set_deep_after_ms(ms); }
void core_dump_ui_state(void) { dbg_dump_ui_state(); }

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
    ring_init(&s_motion_evts, s_motion_buf, sizeof s_motion_buf[0], 8);
    ring_init(&s_gestures, s_gesture_buf, sizeof s_gesture_buf[0], 32);
    gesture_init(&s_gesture, on_gesture, NULL);

    const hal_board_info_t *bi = hal_board_info();
    dbg_event("app", "boot", "\"board\":%s,\"fw\":%s,\"w\":%u,\"h\":%u", dbg_q(bi->board), dbg_q(bi->fw_version),
              (unsigned)bi->width, (unsigned)bi->height);

    model_init();
    power_init();
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = hal_display_create();
    lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);
    s_touch = hal_touch_create(disp);
    if (s_touch) {
        s_hal_touch_read = lv_indev_get_read_cb(s_touch);
        lv_indev_set_read_cb(s_touch, touch_read_cb);
        lv_timer_set_period(lv_indev_get_read_timer(s_touch), 10);
    }

    ui_init(&s_actions);
    link_init(&s_hooks);
    hal_buttons_set_callback(on_button);
    hal_motion_set_callback(on_motion);
    hal_link_start(on_rx, on_state);
#if CORE_BOOT_BUZZ
    if (!power_woke_by_timer()) feedback_hello(); /* a reconnect attempt stays silent and dark */
#endif
    power_start(app_now_ms());
    s_fps_t0 = app_now_ms();
}

uint32_t core_step(void)
{
    uint32_t now = app_now_ms();
    drain_input(now);
    drain_link(now);
    power_tick(now);
    uint32_t wait = lv_timer_handler();

    gesture_evt_t g;
    while (ring_pop(&s_gestures, &g)) dispatch(&g);
    model_attention_expire(hal_rtc_get_unix());
    ui_tick(now);
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
