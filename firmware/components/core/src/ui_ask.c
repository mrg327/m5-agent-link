/* The ASK screen (UI-V2 §4.3, §7): the request's title and text in the top half, its answer options
 * on the wheel in the bottom half, and the answer-pending line once an answer is out. The item list
 * and the hold rules come from nav.c (interaction model); this file only names, colours and places
 * the items and runs the one the user commits. A hold is bound to the request id it started on and
 * cancels when that request changes, clears or is answered. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "ui_wheel.h"

#define HEADER_Y 50
#define TITLE_Y 78
#define TEXT_Y 108
#define TEXT_LINES 3
#define DIVIDER_Y 190
#define HINT_Y 364
#define ASK_WHEEL ((wheel_layout_t){272, 40, 0}) /* three rows in the bottom half */

static lv_obj_t *s_root, *s_header, *s_title, *s_text, *s_divider, *s_wheel, *s_answer, *s_hint;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static uint32_t s_sid, s_rid, s_shown_ms, s_now;
static ask_item_t s_items[ASK_MAX_ITEMS];
static int s_n;
static bool s_holding, s_hold_fired, s_ramp_started;
static uint32_t s_hold_t0, s_hold_rid;
static int s_hold_idx;
static char s_header_txt[64], s_title_txt[48], s_text_txt[168], s_answer_txt[64], s_hint_txt[40];
static uint32_t s_answer_rgb;

/* The request this screen is about: the selected session's unanswered permission or question. */
static const attention_t *req(void)
{
    const attention_t *a = model_attention_for_sid(s_sid);
    return a && (a->kind == ATT_APPROVAL || a->kind == ATT_QUESTION) ? a : NULL;
}

static const char *label_of(const ask_item_t *it, const attention_t *a)
{
    switch (it->id) {
    case ASK_OPT: return a->opts[it->opt];
    case ASK_APPROVE: return "Approve";
    case ASK_DENY: return "Deny";
    default: return "Open on PC";
    }
}

/* Colour says what the item does: orange grants, vermillion denies, yellow answers a question,
 * sky blue goes to the PC. Never the only cue: hold items also carry the HOLD tag. */
static uint32_t rgb_of(const ask_item_t *it)
{
    if (it->grants) return RGB_PERMISSION;
    if (it->hold_ms) return RGB_ERROR;
    return it->id == ASK_OPEN_PC ? RGB_ACCENT : RGB_ASK;
}

static void ring_items(const attention_t *a)
{
    lv_obj_t *ring = ui_home_ring();
    ring_seg_t segs[WHEEL_MAX_ITEMS];
    int n = wheel_count(s_wheel), sel = wheel_sel(s_wheel);
    if (a->answer_sent) { /* one settled, dim capsule in the answer's colour: nothing left to pick */
        segs[0] = (ring_seg_t){0x40000000u, s_answer_rgb, RING_DONE, false, true};
        n = 1;
    } else {
        for (int i = 0; i < n; i++)
            segs[i] = (ring_seg_t){0x40000000u | (uint32_t)i, wheel_item(s_wheel, i)->rgb, RING_DONE, i == sel, false};
    }
    ring_set(ring, segs, n, s_now);
    ring_set_still(ring, true);
    if (g_ui_actions->sectors) g_ui_actions->sectors(n);
}

static void hold_cancel(void)
{
    if (!s_holding) return;
    s_holding = false;
    if (s_ramp_started) g_ui_actions->hold_stop();
    s_ramp_started = false;
    ring_set_progress(ui_home_ring(), 0, 0xffffff);
}

static void set_txt(lv_obj_t *l, char *cache, size_t cap, const char *t)
{
    if (strcmp(cache, t) == 0) return;
    snprintf(cache, cap, "%s", t);
    lv_label_set_text(l, t);
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Returns false when there is nothing left to ask (request cleared or session gone). */
bool ui_ask_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = model_find(s_sid);
    const attention_t *a = req();
    if (!s || s->state == SESS_ENDED || !a) return false;
    if (a->rid != s_rid) { /* a different request: re-arm, start at the default, drop any hold */
        hold_cancel();
        s_rid = a->rid;
        s_shown_ms = now;
        wheel_set(s_wheel, NULL, 0, 0);
    }
    char buf[80];
    const char *base = theme_basename(s->repo);
    char host[24];
    theme_host_prefix(host, sizeof host, s);
    snprintf(buf, sizeof buf, "%s%s%s%s", host, theme_agent_name(s->agent), theme_sep(), base[0] ? base : "(no folder)");
    set_txt(s_header, s_header_txt, sizeof s_header_txt, buf);
    bool perm = a->kind == ATT_APPROVAL;
    set_txt(s_title, s_title_txt, sizeof s_title_txt,
            a->title[0] ? a->title : s->tool[0] ? s->tool : perm ? "Permission request" : "Question");
    set_txt(s_text, s_text_txt, sizeof s_text_txt, a->text);

    if (a->answer_sent) {
        uint32_t rgb = RGB_DONE;
        if (a->answer == ATT_ANSWER_DENY) {
            snprintf(buf, sizeof buf, "Denied");
            rgb = RGB_ERROR;
        } else if (a->answer == ATT_ANSWER_REPLY) {
            snprintf(buf, sizeof buf, "Sent \"%s\"%swaiting for PC", a->opts[a->answer_opt % 4], theme_sep());
            rgb = RGB_ASK;
        } else {
            snprintf(buf, sizeof buf, "Approved%swaiting for PC", theme_sep());
        }
        set_txt(s_answer, s_answer_txt, sizeof s_answer_txt, buf);
        s_answer_rgb = rgb;
        theme_set_text_color(s_answer, lv_color_hex(rgb));
        show(s_answer, true);
        wheel_set_shown(s_wheel, false);
        set_txt(s_hint, s_hint_txt, sizeof s_hint_txt, "");
        hold_cancel();
    } else {
        s_n = nav_ask_items(a, s_items, ASK_MAX_ITEMS);
        wheel_item_t w[ASK_MAX_ITEMS];
        for (int i = 0; i < s_n; i++) w[i] = (wheel_item_t){label_of(&s_items[i], a), rgb_of(&s_items[i]), s_items[i].hold_ms > 0, i};
        int sel = wheel_count(s_wheel) == s_n ? wheel_sel(s_wheel) : 0;
        wheel_set(s_wheel, w, s_n, sel);
        wheel_set_shown(s_wheel, true);
        show(s_answer, false);
        set_txt(s_hint, s_hint_txt, sizeof s_hint_txt, perm && (a->acts & (1u << ACT_DENY)) ? "long swipe down to deny" : "");
    }
    theme_set_text_color(s_word[0], lv_color_hex(RGB_ACCENT));
    ring_items(a);
    return true;
}

void ui_ask_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_rid = 0;
    s_now = now;
    s_holding = s_hold_fired = s_ramp_started = false;
    wheel_set(s_wheel, NULL, 0, 0);
    ui_ask_refresh(now);
    show(s_root, true);
    dbg_event("ui", "ask", "\"sid\":%u,\"rid\":%u,\"items\":%d", (unsigned)sid, (unsigned)s_rid, s_n);
}

void ui_ask_close(void)
{
    hold_cancel();
    show(s_root, false);
    ring_set_still(ui_home_ring(), !ui_animations_allowed());
}

static void select_item(int i, const char *via, void (*haptic)(void))
{
    int n = wheel_count(s_wheel);
    if (n == 0) return;
    i = ((i % n) + n) % n;
    if (i == wheel_sel(s_wheel)) return;
    wheel_select(s_wheel, i);
    hold_cancel(); /* the hold belongs to the item it started on */
    const attention_t *a = req();
    if (a) ring_items(a);
    dbg_event("ui", "item", "\"idx\":%d,\"via\":\"%s\"", i, via);
    if (haptic) haptic();
}

/* Sends the answer for item `idx` of request `a`. Approve-class and deny items only arrive here from
 * a completed hold (or an injected long press that covered it); never from a tap. */
static void commit(const attention_t *a, int idx, uint32_t hold_ms)
{
    const ask_item_t *it = &s_items[idx];
    dbg_event("ui", "commit", "\"sid\":%u,\"rid\":%u,\"item\":%d,\"opt\":%d,\"hold_ms\":%u", (unsigned)s_sid,
              (unsigned)a->rid, (int)it->id, it->opt, (unsigned)hold_ms);
    switch (it->id) {
    case ASK_OPT: g_ui_actions->reply(s_sid, a->rid, (uint8_t)it->opt, hold_ms); break;
    case ASK_APPROVE: g_ui_actions->approve(s_sid, a->rid, hold_ms); break;
    case ASK_DENY: g_ui_actions->deny(s_sid, a->rid); break;
    case ASK_OPEN_PC:
        g_ui_actions->focus(s_sid);
        ui_go_home();
        break;
    }
}

bool ui_ask_act(nav_action_t act, const gesture_evt_t *g)
{
    const attention_t *a = req();
    int sel = wheel_sel(s_wheel);
    const ask_item_t *it = a && !a->answer_sent && sel < s_n ? &s_items[sel] : NULL;
    switch (act) {
    case NAV_HOLD_PRESS:
        s_hold_fired = false;
        /* only a hold item arms a hold, only for the request on screen, and only once it is armed */
        if (it && it->hold_ms && nav_hold_may_start(s_rid, s_shown_ms, a->rid, s_now)) {
            s_holding = true;
            s_ramp_started = false;
            s_hold_t0 = s_now;
            s_hold_rid = a->rid;
            s_hold_idx = sel;
        }
        return true;
    case NAV_HOLD_RELEASE: hold_cancel(); return true;
    case NAV_COMMIT:
        if (!it) {
            g_ui_actions->reject();
            return true;
        }
        if (it->hold_ms) { /* a tap never commits an approve-class or deny item */
            ui_toast("Hold to confirm", rgb_of(it));
            g_ui_actions->reject();
            return true;
        }
        commit(a, sel, 0);
        return true;
    case NAV_HOLD_LONG: /* injected input whose duration already covers the hold */
        if (it && it->hold_ms && g->dur_ms >= it->hold_ms && !s_hold_fired &&
            nav_hold_may_start(s_rid, s_shown_ms, a->rid, s_now)) {
            hold_cancel();
            s_hold_fired = true;
            commit(a, sel, g->dur_ms);
        }
        return true;
    case NAV_FOCUS: /* button A: Open on PC, whatever is selected */
        g_ui_actions->focus(s_sid);
        ui_go_home();
        return true;
    case NAV_DENY: {
        if (!a || a->answer_sent || !(a->acts & (1u << ACT_DENY))) return false;
        if (!nav_long_swipe_down(g->y, ui_last_release_y())) return true; /* too short: ignored, but consumed */
        hold_cancel();
        g_ui_actions->deny(s_sid, a->rid);
        return true;
    }
    case NAV_PICK_RIM: {
        int i = ring_hit(ui_home_ring(), g->x, g->y);
        if (i >= 0 && it) select_item(i, "tap", g_ui_actions->tick);
        return true;
    }
    case NAV_SCRUB:
        if (g->kind != G_SCRUB_END && it) select_item(g->x, "scrub", g_ui_actions->detent);
        return true;
    case NAV_NEXT: select_item(sel + 1, "btn", g_ui_actions->tick); return true;
    case NAV_PREV: select_item(sel - 1, "btn", g_ui_actions->tick); return true;
    default: return false;
    }
}

/* Returns true when the screen wants to close: the request was cleared or its session went away.
 * ASK has no idle timeout; it waits for the answer or the daemon's clear. */
bool ui_ask_tick(uint32_t now)
{
    s_now = now;
    const attention_t *a = req();
    if (!a || !model_find(s_sid)) return true;
    show(s_hint, !ui_toast_active()); /* the toast takes the hint's line (UI_TOAST_LOW_Y) */
    if (s_holding && !s_hold_fired) {
        uint32_t el = now - s_hold_t0;
        const ask_item_t *it = s_hold_idx < s_n ? &s_items[s_hold_idx] : NULL;
        if (a->rid != s_hold_rid || a->answer_sent || !it || !it->hold_ms || s_hold_idx != wheel_sel(s_wheel)) {
            hold_cancel();
            return false;
        }
        uint32_t rgb = rgb_of(it);
        if (el >= NAV_HOLD_SHOW_MS) {
            if (!s_ramp_started) {
                s_ramp_started = true;
                if (it->grants) g_ui_actions->hold_start();
                else g_ui_actions->deny_hold_start();
            }
            ring_set_progress(ui_home_ring(), (int)(el * 1000 / it->hold_ms), rgb);
        }
        if (el >= it->hold_ms) {
            s_hold_fired = true;
            s_holding = false;
            s_ramp_started = false;
            ring_set_progress(ui_home_ring(), 0, rgb);
            commit(a, s_hold_idx, el);
        }
    }
    return false;
}

void ui_ask_report(ui_report_t *r)
{
    const attention_t *a = req();
    snprintf(r->screen, sizeof r->screen, "ask");
    r->has_sel_sid = model_find(s_sid) != NULL;
    r->sel_sid = s_sid;
    if (a) {
        r->has_rid = true;
        r->rid = a->rid;
    }
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    if (s_title_txt[0]) ui_report_add(r, "%s", s_title_txt);
    if (s_text_txt[0]) ui_report_add(r, "%s", s_text_txt);
    if (a && a->answer_sent) {
        ui_report_add(r, "%s", s_answer_txt);
        return;
    }
    int n = wheel_count(s_wheel), sel = wheel_sel(s_wheel);
    for (int k = 0; k < n && k < 4; k++) {
        const wheel_item_t *it = wheel_item(s_wheel, (sel + k) % n);
        ui_report_add(r, "%s%s%s", k == 0 ? "> " : "", it->label, it->hold ? " HOLD" : "");
    }
    ui_report_add(r, "%d options", n);
}

static lv_obj_t *line(lv_obj_t *parent, int px, lv_color_t col, int y, int w, int lines)
{
    lv_obj_t *l = theme_label(parent, px, col);
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(theme_font(px)));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, UI_CX - w / 2, y);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ui_ask_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_header = line(s_root, 16, COL_MUTED, HEADER_Y, 260, 1);
    s_title = line(s_root, 24, COL_TEXT, TITLE_Y, 340, 1);
    s_text = line(s_root, 20, COL_MUTED, TEXT_Y, 360, TEXT_LINES);
    s_divider = lv_obj_create(s_root); /* a thin capsule between the question and its answers */
    lv_obj_remove_style_all(s_divider);
    lv_obj_set_size(s_divider, 180, 3);
    lv_obj_set_style_radius(s_divider, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_divider, LV_OPA_COVER, 0);
    theme_set_bg_color(s_divider, COL_BORDER);
    lv_obj_set_pos(s_divider, UI_CX - 90, DIVIDER_Y);
    s_wheel = wheel_create_at(s_root, ASK_WHEEL);
    s_answer = line(s_root, 24, COL_TEXT, ASK_WHEEL.cy - lv_font_get_line_height(theme_font(24)) / 2, 360, 1);
    lv_obj_add_flag(s_answer, LV_OBJ_FLAG_HIDDEN);
    s_hint = line(s_root, 16, COL_MUTED, HINT_Y, 300, 1);
    static const char *const words[2] = {"OPEN ON PC", "NEXT"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
