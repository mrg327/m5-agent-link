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
#include "ui.h"

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
static gesture_evt_t s_deferred_tap;
static bool s_has_deferred;
static uint32_t s_deferred_at;
#define DEFER_SLACK_MS 30
static lv_indev_t *s_touch;

uint32_t app_now_ms(void) { return (uint32_t)(hal_time_us() / 1000); }
uint32_t app_fps(void) { return s_fps; }
static uint32_t tick_cb(void) { return app_now_ms(); }

void app_inject_gesture(const gesture_evt_t *g) { ring_push(&s_gestures, g); }

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
    if (sent) ui_toast(ok_text, 0x58a6ff);
    else ui_toast("Offline", 0xd29922);
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
        model_attention_mark_sent(rid);
        feedback_confirm();
    }
    toast_cmd_result(sent, "Approved");
}

static void act_deny(uint32_t sid, uint32_t rid)
{
    bool sent = link_send_cmd(LINK_OP_DENY, sid, rid, 0);
    if (sent) {
        model_attention_mark_sent(rid);
        feedback_confirm();
    }
    toast_cmd_result(sent, "Denied");
}

static void act_ack(uint32_t sid) { link_send_cmd(LINK_OP_ACK, sid, 0, 0); }

static const ui_actions_t s_actions = {act_focus, act_approve, act_deny, act_ack, feedback_tick};

/* ── link → app ── */

static void on_link(bool welcomed) { ui_set_link_state(welcomed); }

static void on_attention(const attention_t *a)
{
    uint32_t rgb = a->kind == ATT_ERROR ? 0xf85149 : a->kind == ATT_DONE ? 0x3fb950 : 0xd29922;
    feedback_attention(a->kind);
    power_attention(rgb, app_now_ms());
    if (!power_display_on()) return;
    ui_report_t r;
    ui_report(&r);
    bool busy_elsewhere = r.has_sel_sid && r.sel_sid != a->sid && strcmp(r.screen, "attention") == 0;
    if (!busy_elsewhere && a->kind != ATT_DONE) ui_show_session(a->sid);
}

static void on_result(int op, uint32_t sid, bool ok, int err, const char *msg)
{
    (void)sid;
    if (ok) {
        if (op == LINK_OP_FOCUS) ui_toast(LV_SYMBOL_OK " Focused", 0x3fb950);
        return;
    }
    static const char *const errs[] = {"Out of date", "Denied", "Not supported", "No window", "Timed out", "Failed"};
    const char *text = err >= 0 && err < 6 ? errs[err] : msg[0] ? msg : "Failed";
    ui_toast(text, err == 3 ? 0xd29922 : 0xf85149);
    feedback_reject();
}

static const link_hooks_t s_hooks = {.on_link = on_link, .on_attention = on_attention, .on_result = on_result};

/* ── input dispatch ── */

static void dispatch_now(const gesture_evt_t *g)
{
    if (g->kind == G_LIFT) return;
    if (g->kind == G_SHAKE || g->kind == G_BTN_B_LONG) {
        ui_show_glance();
        return;
    }
    ui_on_gesture(g);
}

static void flush_deferred(void)
{
    if (!s_has_deferred) return;
    s_has_deferred = false;
    dispatch_now(&s_deferred_tap);
}

/* A tap that would navigate is held back for the double-tap window, so a double-tap on a row
 * focuses it instead of first opening it. */
static void dispatch(const gesture_evt_t *g)
{
    bool raw = g->kind == G_PRESS || g->kind == G_RELEASE;
    if (!raw) dbg_event("input", gesture_name(g->kind), "\"x\":%d,\"y\":%d", g->x, g->y);
    if (power_activity(app_now_ms())) { /* first touch only wakes the display */
        s_has_deferred = false;
        return;
    }
    if (g->kind == G_DOUBLE_TAP) s_has_deferred = false;
    else if (!raw) flush_deferred();
    if (g->kind == G_TAP && ui_tap_defers(g)) {
        s_deferred_tap = *g;
        s_has_deferred = true;
        s_deferred_at = app_now_ms();
        return;
    }
    dispatch_now(g);
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
    if (s_has_deferred && !s_gesture.pressed && now - s_deferred_at >= GESTURE_DOUBLE_TAP_MS + DEFER_SLACK_MS)
        flush_deferred();
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
    feedback_hello();
#endif
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
    return wait > 10 ? 10 : wait;
}
