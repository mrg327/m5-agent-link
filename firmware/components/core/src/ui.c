/* Ring screen host: toast, alert glow, link state, report for the debug channel. */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define TOAST_MS 1800

const ui_actions_t *g_ui_actions;
static lv_obj_t *s_home, *s_agent, *s_ask, *s_scroll, *s_status, *s_watch, *s_voice, *s_keys;
static nav_mode_t s_mode = MODE_HOME;
static int s_rel_y;
static bool s_link_up, s_animate = true;
static uint32_t s_seen_rev;
static uint32_t s_now_ms;

#define TOAST_H 40
#define TOAST_MAX_W 290
#define TOAST_DOT 10
#define TOAST_MASK_H 26 /* covers the 22 px legends */
#define TOAST_MASK_W 314 /* a pill: its outer point at the row centre stays inside r 205, so the ring is never touched */
static lv_obj_t *s_toast, *s_toast_label, *s_toast_dot, *s_toast_mask;
static uint32_t s_toast_until_ms;
static lv_obj_t *s_glow, *s_glow_ring;

lv_obj_t *ui_new_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, UI_SIZE, UI_SIZE);
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static void create_overlays(void)
{
    lv_obj_t *top = lv_layer_top();
    /* The legends share the toast's row: a black pill under the toast covers them cleanly while it shows. */
    s_toast_mask = lv_obj_create(top);
    lv_obj_remove_style_all(s_toast_mask);
    lv_obj_set_size(s_toast_mask, TOAST_MASK_W, TOAST_MASK_H);
    lv_obj_set_style_radius(s_toast_mask, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(s_toast_mask, (UI_SIZE - TOAST_MASK_W) / 2, UI_Y_TOAST - TOAST_MASK_H / 2);
    lv_obj_set_style_bg_color(s_toast_mask, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_toast_mask, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_toast_mask, LV_OBJ_FLAG_HIDDEN);

    /* One slot (UI-V3 §2.5): a neutral capsule with a dot in the toast's colour; the text is always white. */
    s_toast = lv_obj_create(top);
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_style_bg_color(s_toast, COL_FILL2, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(s_toast, 18, 0);
    lv_obj_set_style_pad_column(s_toast, 10, 0);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, TOAST_H);
    lv_obj_set_style_max_width(s_toast, TOAST_MAX_W, 0);
    lv_obj_set_flex_flow(s_toast, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_toast, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_toast_dot = lv_obj_create(s_toast);
    lv_obj_remove_style_all(s_toast_dot);
    lv_obj_set_size(s_toast_dot, TOAST_DOT, TOAST_DOT);
    lv_obj_set_style_radius(s_toast_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_toast_dot, LV_OPA_COVER, 0);
    s_toast_label = lv_label_create(s_toast);
    lv_obj_set_style_text_font(s_toast_label, FONT_BODY_STRONG, 0);
    lv_obj_set_style_text_color(s_toast_label, COL_TEXT, 0);
    lv_label_set_long_mode(s_toast_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_toast_label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toast_label, TOAST_MAX_W - 36 - TOAST_DOT - 10, 0);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    s_glow = lv_obj_create(top);
    lv_obj_remove_style_all(s_glow);
    lv_obj_set_size(s_glow, UI_SIZE, UI_SIZE);
    lv_obj_set_style_bg_color(s_glow, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_glow, LV_OPA_COVER, 0);
    s_glow_ring = ring_create(s_glow);
    lv_obj_add_flag(s_glow, LV_OBJ_FLAG_HIDDEN);
}

void ui_init(const ui_actions_t *actions)
{
    g_ui_actions = actions;
    s_home = ui_home_init();
    s_agent = ui_agent_init(s_home);
    s_ask = ui_ask_init(s_home);
    s_scroll = ui_scroll_init(s_home);
    s_status = ui_status_init(s_home);
    s_watch = ui_watch_init(s_home);
    s_voice = ui_voice_init(s_home);
    s_keys = ui_keys_init(s_home);
    create_overlays();
    lv_screen_load(s_home);
    ui_home_refresh(0);
    dbg_event("ui", "screen", "\"screen\":\"ring\"");
}

void ui_set_animations(bool enabled)
{
    s_animate = enabled;
    ui_home_animations_changed();
}
bool ui_animations_allowed(void) { return s_animate; }

bool ui_link_connected(void) { return s_link_up; }

static bool s_was_up;
static uint32_t s_link_down_at;
uint32_t ui_link_down_ms(void) { return s_link_up || !s_was_up ? 0 : s_now_ms - s_link_down_at; }

void ui_set_link_state(bool connected)
{
    if (s_link_up == connected) return;
    if (connected) s_was_up = true;
    else s_link_down_at = s_now_ms;
    s_link_up = connected;
    s_seen_rev = 0; /* force a refresh */
}

static int s_link_kind = -1;
int ui_link_transport(void) { return s_link_kind; }

void ui_set_link_transport(int kind)
{
    if (s_link_kind == kind) return;
    s_link_kind = kind;
    s_seen_rev = 0;
}

nav_mode_t ui_mode(void) { return s_mode; }
int ui_last_release_y(void) { return s_rel_y; }

/* The toast has one slot on every screen: centred on the legend row, replacing the legends while it shows. */
static void place_toast(void)
{
    lv_obj_update_layout(s_toast);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, UI_Y_TOAST - TOAST_H / 2);
}

/* The session WATCH was on when ASK or SCROLL took over; they return there instead of HOME. */
static uint32_t s_resume_watch_sid;
static uint32_t s_voice_rid; /* the request VOICE answers (ui_go_voice) */

static void enter_mode(nav_mode_t m)
{
    if (m != MODE_ASK && m != MODE_SCROLL) s_resume_watch_sid = 0;
    if (m == s_mode) return;
    if (s_mode == MODE_AGENT) ui_agent_close();
    if (s_mode == MODE_ASK) ui_ask_close();
    if (s_mode == MODE_SCROLL) ui_scroll_close();
    if (s_mode == MODE_STATUS) ui_status_close();
    if (s_mode == MODE_WATCH) ui_watch_close();
    if (s_mode == MODE_VOICE) ui_voice_close();
    if (s_mode == MODE_KEYS) ui_keys_close();
    s_mode = m;
    ui_home_set_card_shown(m == MODE_HOME);
    if (m == MODE_AGENT) ui_agent_open(ui_home_sel_sid(), s_now_ms);
    else if (m == MODE_ASK) ui_ask_open(ui_home_sel_sid(), s_now_ms);
    else if (m == MODE_SCROLL) ui_scroll_open(ui_home_sel_sid(), s_now_ms);
    else if (m == MODE_STATUS) {
        ui_home_refresh(s_now_ms); /* the ring shows sessions again, e.g. after AGENT -> Status */
        ui_status_open(s_now_ms);
    }
    else if (m == MODE_WATCH) ui_watch_open(ui_home_sel_sid(), s_now_ms);
    else if (m == MODE_VOICE) ui_voice_open(ui_home_sel_sid(), s_voice_rid, s_now_ms);
    else if (m == MODE_KEYS) ui_keys_open(ui_home_sel_sid(), s_now_ms);
    else ui_home_refresh(s_now_ms); /* the ring shows sessions again */
    if (m != MODE_HOME && m != MODE_KEYS) { /* deeper modes rise into place; KEYS records strokes at once */
        lv_obj_t *const roots[MODE_COUNT] = {NULL, s_agent, s_ask, s_scroll, s_status, s_watch, s_voice, s_keys};
        ui_motion_enter(roots[m]);
    }
    dbg_event("ui", "mode", "\"mode\":\"%s\"", nav_mode_name(m));
}

/* ASK or SCROLL opened over WATCH goes back to WATCH when it closes, if the watched session is still there. */
static void leave_mode(void)
{
    uint32_t sid = s_mode == MODE_ASK || s_mode == MODE_SCROLL ? s_resume_watch_sid : 0;
    const session_t *s = sid ? model_find(sid) : NULL;
    enter_mode(MODE_HOME);
    if (!s || s->state == SESS_ENDED) return;
    ui_home_select_sid(sid, "watch");
    enter_mode(MODE_WATCH);
}

static void open_ask(void)
{
    uint32_t resume = s_mode == MODE_WATCH ? ui_watch_sid() : 0;
    enter_mode(MODE_ASK);
    if (s_mode == MODE_ASK) s_resume_watch_sid = resume;
}

void ui_go_home(void) { leave_mode(); }
void ui_go_watch(void) { enter_mode(MODE_WATCH); }
void ui_go_voice(uint32_t rid)
{
    s_voice_rid = rid;
    enter_mode(MODE_VOICE);
}
void ui_go_keys(void) { enter_mode(MODE_KEYS); }
uint32_t ui_watched_sid(void) { return s_mode == MODE_WATCH ? ui_watch_sid() : 0; }

void ui_scroll_failed(void)
{
    if (s_mode == MODE_SCROLL) leave_mode();
}

void ui_scroll_pos(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total, uint32_t total, const char *preview)
{
    if (s_mode == MODE_SCROLL) ui_scroll_on_pos(sid, lines_above, at_bottom, has_total, total, preview);
}

/* An attention always lands on the deck, whatever was open. */
void ui_show_session(uint32_t sid)
{
    enter_mode(MODE_HOME);
    ui_home_select_sid(sid, "attention");
}

/* A permission or question opens ASK on its session (UI-V2 §2); done/error only update the card. */
void ui_show_attention(uint32_t sid)
{
    uint32_t resume = s_mode == MODE_WATCH ? ui_watch_sid() : s_mode == MODE_ASK || s_mode == MODE_SCROLL ? s_resume_watch_sid : 0;
    ui_show_session(sid);
    if (ui_home_sel_sid() == sid && ui_home_askable()) {
        enter_mode(MODE_ASK);
        s_resume_watch_sid = resume;
    }
}

uint32_t ui_pending_attention_sid(void)
{
    const attention_t *a = model_attention_first();
    return a && !a->answer_sent && (a->kind == ATT_APPROVAL || a->kind == ATT_QUESTION) ? a->sid : 0;
}

void ui_report_add(ui_report_t *r, const char *fmt, ...)
{
    if (r->n_visible >= 8) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->visible[r->n_visible++], sizeof r->visible[0], fmt, ap);
    va_end(ap);
}

void ui_report(ui_report_t *r)
{
    memset(r, 0, sizeof *r);
    r->power = "active";
    if (!lv_obj_has_flag(s_toast, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", lv_label_get_text(s_toast_label));
    if (s_mode == MODE_AGENT) ui_agent_report(r);
    else if (s_mode == MODE_ASK) ui_ask_report(r);
    else if (s_mode == MODE_SCROLL) ui_scroll_report(r);
    else if (s_mode == MODE_STATUS) ui_status_report(r);
    else if (s_mode == MODE_WATCH) ui_watch_report(r);
    else if (s_mode == MODE_VOICE) ui_voice_report(r);
    else if (s_mode == MODE_KEYS) ui_keys_report(r);
    else ui_home_report(r);
}

/* ───────── toast & glow ───────── */

static void anim_translate_y(void *o, int32_t v) { lv_obj_set_style_translate_y(o, v, 0); }

static void rise(lv_obj_t *o, int px)
{
    lv_anim_delete(o, anim_translate_y);
    if (!ui_animations_allowed()) {
        lv_obj_set_style_translate_y(o, 0, 0);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, anim_translate_y);
    lv_anim_set_values(&a, px, 0);
    lv_anim_set_duration(&a, UI_MOTION_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void ui_motion_enter(lv_obj_t *content) { rise(content, UI_MOTION_RISE); }

void ui_toast(const char *text, uint32_t rgb)
{
    lv_label_set_text(s_toast_label, text);
    lv_obj_set_style_bg_color(s_toast_dot, lv_color_hex(rgb), 0);
    place_toast();
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_toast_mask, LV_OBJ_FLAG_HIDDEN);
    rise(s_toast, UI_TOAST_RISE);
    s_toast_until_ms = s_now_ms + TOAST_MS;
    dbg_event("ui", "toast", "\"text\":%s", dbg_q(text));
}

void ui_glow(uint32_t rgb)
{
    ring_set_solid(s_glow_ring, lv_color_mix(lv_color_hex(rgb), COL_BG, 140)); /* dim: ~55 % */
    lv_obj_remove_flag(s_glow, LV_OBJ_FLAG_HIDDEN);
    dbg_event("ui", "glow", "\"rgb\":%u", (unsigned)rgb);
}

void ui_glow_hide(void) { lv_obj_add_flag(s_glow, LV_OBJ_FLAG_HIDDEN); }
bool ui_glow_active(void) { return !lv_obj_has_flag(s_glow, LV_OBJ_FLAG_HIDDEN); }

/* ───────── per-step ───────── */

void ui_tick(uint32_t now_ms)
{
    s_now_ms = now_ms;
    if (!lv_obj_has_flag(s_toast, LV_OBJ_FLAG_HIDDEN) && (int32_t)(now_ms - s_toast_until_ms) >= 0) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_toast_mask, LV_OBJ_FLAG_HIDDEN);
    }
    if (model_rev() != s_seen_rev) {
        s_seen_rev = model_rev();
        ui_home_refresh(now_ms);
        if (s_mode == MODE_AGENT && !ui_agent_refresh(now_ms)) enter_mode(MODE_HOME);
        if (s_mode == MODE_ASK && !ui_ask_refresh(now_ms)) leave_mode();
        if (s_mode == MODE_SCROLL && !ui_scroll_refresh(now_ms)) enter_mode(MODE_HOME); /* the session is gone */
        if (s_mode == MODE_STATUS) ui_status_refresh(now_ms);
        if (s_mode == MODE_VOICE && !ui_voice_refresh(now_ms)) enter_mode(MODE_HOME);
        if (s_mode == MODE_KEYS && !ui_keys_refresh(now_ms)) enter_mode(MODE_HOME);
        if (s_mode == MODE_WATCH && !ui_watch_refresh(now_ms)) {
            enter_mode(MODE_HOME);
            ui_toast("Session ended", RGB_IDLE);
        }
    }
    ui_home_tick(now_ms);
    if (s_mode == MODE_AGENT && ui_agent_tick(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_ASK && ui_ask_tick(now_ms)) leave_mode();
    if (s_mode == MODE_SCROLL && ui_scroll_tick(now_ms)) leave_mode(); /* idle: back to WATCH */
    if (s_mode == MODE_STATUS && ui_status_tick(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_WATCH) ui_watch_tick(now_ms);
    if (s_mode == MODE_VOICE && ui_voice_tick(now_ms)) { /* sent */
        /* an answer to a request goes back to ASK, which shows it pending until the daemon's clear */
        const attention_t *a = ui_voice_rid() ? model_attention_find(ui_voice_rid()) : NULL;
        uint32_t sid = a ? a->sid : 0;
        enter_mode(MODE_HOME);
        if (sid) {
            ui_home_select_sid(sid, "voice");
            enter_mode(MODE_ASK);
        }
    }
    if (s_mode == MODE_KEYS) ui_keys_tick(now_ms);
}

/* The interaction layer maps the gesture to an action; mode changes are handled here, everything
 * else goes to the screen that owns the mode. */
bool ui_on_gesture(const gesture_evt_t *g)
{
    if (g->kind == G_RELEASE) s_rel_y = g->y;
    nav_zone_t zone = nav_centre(g->x, g->y) ? ZONE_CENTRE : ZONE_RIM;
    nav_action_t a = nav_lookup(s_mode, g->kind, zone, ui_home_request_pending(), ui_home_askable());
    if (a != NAV_NONE && g->kind != G_PRESS && g->kind != G_RELEASE)
        dbg_event("ui", "nav", "\"mode\":\"%s\",\"gesture\":\"%s\",\"action\":\"%s\"", nav_mode_name(s_mode),
                  gesture_name(g->kind), nav_action_name(a));
    switch (a) {
    case NAV_OPEN_AGENT:
        ui_home_note_input();
        if (!ui_home_sel_sid()) {
            g_ui_actions->reject();
            return true;
        }
        enter_mode(MODE_AGENT);
        g_ui_actions->tick();
        return true;
    case NAV_OPEN_ASK: /* shows the options; answering takes a further tap or hold */
        ui_home_note_input();
        if (!ui_home_askable()) {
            g_ui_actions->reject();
            return true;
        }
        open_ask();
        g_ui_actions->tick();
        return true;
    case NAV_OPEN_STATUS:
        ui_home_note_input();
        enter_mode(MODE_STATUS);
        g_ui_actions->tick();
        return true;
    case NAV_BACK:
        if (g->kind == G_SWIPE_DOWN && !nav_long_swipe_down(g->y, s_rel_y)) return true; /* too short: ignored */
        if (s_mode == MODE_ASK && ui_ask_back()) { /* the reader closes first; the request stays on screen */
            g_ui_actions->tick();
            return true;
        }
        if (s_mode == MODE_SCROLL && g->kind == G_TAP && ui_scroll_caught()) return true; /* it stopped a glide */
        if (s_mode == MODE_SCROLL && g->kind == G_SWIPE_RIGHT && ui_scroll_dragged()) return true; /* a flick that scrolled */
        leave_mode();
        g_ui_actions->tick();
        return true;
    case NAV_HOME:
        enter_mode(MODE_HOME);
        return true;
    case NAV_SCROLL_DRAG:
        if (s_mode != MODE_WATCH) break; /* SCROLL's own drags go to the screen below */
        {
            /* A drag on WATCH scrolls the watched session's terminal (issue 5cbb): bring it to the
             * front quietly, open SCROLL over WATCH, then this first drag step moves it. */
            uint32_t sid = ui_watch_sid();
            const session_t *s = model_find(sid);
            ui_home_note_input();
            if (!s || s->no_scroll) {
                ui_toast("Not supported", RGB_AMBER);
                g_ui_actions->reject();
                return true;
            }
            g_ui_actions->scroll_open(sid);
            enter_mode(MODE_SCROLL);
            if (s_mode != MODE_SCROLL) return true;
            s_resume_watch_sid = sid;
            return ui_scroll_act(a, g);
        }
    default: break;
    }
    if (s_mode == MODE_AGENT) {
        ui_home_note_input();
        return ui_agent_act(a, g);
    }
    if (s_mode == MODE_ASK) {
        ui_home_note_input();
        return ui_ask_act(a, g);
    }
    if (s_mode == MODE_SCROLL) {
        ui_home_note_input();
        return ui_scroll_act(a, g);
    }
    if (s_mode == MODE_STATUS) {
        ui_home_note_input();
        return ui_status_act(a, g);
    }
    if (s_mode == MODE_VOICE) {
        ui_home_note_input();
        return ui_voice_act(a, g);
    }
    if (s_mode == MODE_KEYS) {
        ui_home_note_input();
        return ui_keys_act(a, g);
    }
    if (s_mode == MODE_WATCH) {
        ui_home_note_input();
        return ui_watch_act(a, g);
    }
    return ui_home_act(a, g);
}
