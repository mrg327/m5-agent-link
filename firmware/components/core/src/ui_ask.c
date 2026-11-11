/* The ASK screen (UI-V3 §4.3, UI-V2 §7): caption, title and the request text above, the picker below,
 * and the answered state once an answer is out. The item list and the hold rules come from nav.c; this
 * file names, colours and places the items and runs the one the user commits. A hold is bound to the
 * request id it started on and cancels when that request changes, clears or is answered. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "ui_wheel.h"

#define CAPTION_Y 62 /* UI-V3 §4.3: context caption, title, platter, then the picker */
#define TITLE_Y 84
#define PLATTER_Y 118
#define PLATTER_W 320
#define PLATTER_PAD_X 14
#define PLATTER_PAD_Y 10
#define TEXT_LINES 3
#define ICON_Y 154 /* answered state: 44 px icon, word, caption (top edges) */
#define WORD_Y 215
#define CAP_Y 266
#define ASK_WHEEL WHEEL_LAYOUT_BUTTON(282) /* the picker: rows at 234 / 282 / 330 */

static lv_obj_t *s_root, *s_header, *s_title, *s_plat, *s_text, *s_wheel, *s_icon, *s_word, *s_cap;
static lv_obj_t *s_badge[2], *s_key[2], *s_wlbl[2];
static uint32_t s_sid, s_rid, s_shown_ms, s_now;
static ask_item_t s_items[ASK_MAX_ITEMS];
static int s_n;
static bool s_holding, s_hold_fired, s_ramp_started;
static uint32_t s_hold_t0, s_hold_rid;
static int s_hold_idx;
static char s_header_txt[64], s_title_txt[48], s_text_txt[168], s_word_txt[16], s_opt_txt[48], s_cap_txt[72];
static const char *s_icon_txt;
static int s_layout_key;
static const char *s_icon_shown;
/* Read before you approve (UI-V3 §4.3a). A text that does not fit the platter shows MORE_LINES lines and
 * "Read all"; a tap opens the reader, which pages through the whole text. Until its last page has been on
 * screen, nothing that grants permission can be held. A text the daemon itself cut (it ends in an ellipsis)
 * can never be shown in full here, so it can be read and denied on the watch but only approved on the PC. */
#define MORE_LINES 2
#define READ_Y 118
#define READ_W 320
#define READ_LINES 7
#define ASK_BLOCK_CUT_TEXT 1 /* 0: a daemon-cut text may be approved once the part the watch has was read */
#define GATE_TELL_MS 400     /* a finger resting this long on a locked Approve gets told why nothing happens */
static lv_obj_t *s_more, *s_read, *s_read_lbl, *s_page;
static bool s_long, s_cut, s_reading, s_read_ok, s_gate_press;
static int s_page_i, s_pages;
static uint32_t s_read_ms, s_gate_t0;
static char s_page_txt[32];
#define ANSWERED_MIN_MS 900 /* the answered state stays this long, so a quick clear cannot make it flash */
static uint32_t s_answered_ms; /* when the answered state first showed; 0 = not showing */
static bool s_keeping;         /* the request is gone but the answered state is still held on screen */

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
    case ASK_DICTATE: return "Dictate";
    default: return "Open on PC";
    }
}

/* Colour says what the item does: orange grants, vermillion denies, yellow answers a question,
 * the neutral button goes to the PC. Never the only cue: hold items show the hold ring. */
static uint32_t rgb_of(const ask_item_t *it)
{
    if (it->grants) return RGB_PERMISSION;
    if (it->hold_ms) return RGB_ERROR;
    return it->id == ASK_OPEN_PC ? RGB_IDLE : RGB_ASK;
}

/* The wheel's fill: the same colours, except that "Open on PC" is a neutral button. */
static uint32_t fill_of(const ask_item_t *it) { return it->id == ASK_OPEN_PC ? 0 : rgb_of(it); }

static void hold_cancel(void)
{
    if (!s_holding) return;
    s_holding = false;
    if (s_ramp_started) g_ui_actions->hold_stop();
    s_ramp_started = false;
    ring_set_progress(ui_home_ring(), 0, 0xffffff);
    wheel_set_progress(s_wheel, 0);
}

static void show(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return; /* LVGL invalidates even on a no-op */
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* True while item `it` may not be held: it grants permission and the text has not been, or cannot be, read. */
static bool grant_locked(const ask_item_t *it)
{
    return it->grants && ((s_long && !s_read_ok) || (ASK_BLOCK_CUT_TEXT && s_cut));
}

static void reader_page(void)
{
    int lh = lv_font_get_line_height(FONT_BODY);
    lv_obj_set_y(s_read_lbl, -s_page_i * READ_LINES * lh);
    snprintf(s_page_txt, sizeof s_page_txt, "%d of %d", s_page_i + 1, s_pages);
    lv_label_set_text(s_page, s_pages > 1 ? s_page_txt : "");
    bool last = s_page_i == s_pages - 1;
    theme_key_badge_set_word(s_badge[1], s_wlbl[1], last ? "Done" : "More", UI_BADGE_B_DEG, UI_BADGE_R);
    s_read_ms = s_now;
    if (last) s_read_ok = true; /* the whole text has now been on screen */
}

static void reader_open(void)
{
    int lh = lv_font_get_line_height(FONT_BODY);
    lv_point_t sz;
    lv_text_get_size(&sz, s_text_txt, FONT_BODY, 0, 0, READ_W, LV_TEXT_FLAG_NONE);
    int lines = sz.y / lh;
    s_pages = lines <= READ_LINES ? 1 : (lines + READ_LINES - 1) / READ_LINES;
    s_page_i = 0;
    s_reading = true;
    hold_cancel();
    lv_label_set_text_static(s_read_lbl, s_text_txt);
    show(s_plat, false);
    wheel_set_shown(s_wheel, false);
    show(s_read, true);
    show(s_page, true);
    show(s_badge[1], true);
    reader_page();
    dbg_event("ui", "read", "\"rid\":%u,\"pages\":%d", (unsigned)s_rid, s_pages);
}

static void reader_close(void)
{
    if (!s_reading) return;
    s_reading = false;
    show(s_read, false);
    show(s_page, false);
    theme_key_badge_set_word(s_badge[1], s_wlbl[1], "Next", UI_BADGE_B_DEG, UI_BADGE_R);
}

/* The request text sits in a platter for a permission and bare, secondary-coloured, for a question. */
static void layout_text(bool perm, const char *text)
{
    const lv_font_t *f = FONT_BODY;
    int lh = lv_font_get_line_height(f);
    int pw = perm ? PLATTER_W : theme_chord(PLATTER_Y, 12);
    int px = perm ? PLATTER_PAD_X : 0, py = perm ? PLATTER_PAD_Y : 0;
    if (pw > 360) pw = 360;
    lv_point_t sz;
    lv_text_get_size(&sz, text, f, 0, 0, pw - 2 * px, LV_TEXT_FLAG_NONE);
    int lines = sz.y / lh;
    size_t tl = strlen(text);
    s_long = lines > TEXT_LINES;
    /* the daemon clips with an ellipsis; a text that fills the model's buffer was clipped on arrival */
    s_cut = (tl >= 3 && !memcmp(text + tl - 3, "\xE2\x80\xA6", 3)) || tl >= sizeof(((attention_t *)0)->text) - 1;
    lines = lines < 1 ? 1 : s_long ? MORE_LINES : lines;
    int more_h = s_long ? lv_font_get_line_height(FONT_CAPTION) + 4 : 0;
    if (strcmp(s_text_txt, text)) { /* a different text has not been read */
        s_read_ok = false;
        reader_close();
    }
    /* every style or size write invalidates, and refresh runs on each session delta: change-only */
    int key = (perm << 20) | (s_long << 19) | (pw << 8) | lines;
    if (key != s_layout_key) {
        s_layout_key = key;
        lv_obj_set_style_bg_opa(s_plat, perm ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_size(s_plat, pw, lines * lh + more_h + 2 * py);
        lv_obj_set_pos(s_plat, UI_CX - pw / 2, PLATTER_Y);
        lv_obj_set_size(s_text, pw - 2 * px, lines * lh);
        lv_obj_set_pos(s_text, px, py);
        theme_set_text_color(s_text, perm ? COL_TEXT : COL_TEXT2);
        lv_obj_set_size(s_more, pw - 2 * px, lv_font_get_line_height(FONT_CAPTION));
        lv_obj_set_pos(s_more, px, py + lines * lh + 4);
        theme_set_text_color(s_more, lv_color_hex(perm ? RGB_PERMISSION : RGB_ASK));
        show(s_more, s_long);
    }
    theme_set_text(s_text, s_text_txt, sizeof s_text_txt, text);
}

/* Returns false when there is nothing left to ask (request cleared or session gone). */
bool ui_ask_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = model_find(s_sid);
    const attention_t *a = req();
    if (!s || s->state == SESS_ENDED) return false;
    if (!a) {
        /* cleared: hold the answered state on screen for a moment (a new request or input still takes over) */
        if (s_answered_ms && now - s_answered_ms < ANSWERED_MIN_MS) {
            s_keeping = true;
            return true;
        }
        return false;
    }
    s_keeping = false;
    if (a->rid != s_rid) { /* a different request: re-arm, start at the default, drop any hold */
        hold_cancel();
        reader_close();
        s_read_ok = false;
        s_text_txt[0] = 0;
        s_rid = a->rid;
        s_shown_ms = now;
        wheel_set(s_wheel, NULL, 0, 0);
    }
    char buf[80];
    const char *base = theme_basename(s->repo);
    char host[24];
    theme_host_prefix(host, sizeof host, s);
    snprintf(buf, sizeof buf, "%s%s%s%s", host, theme_agent_name(s->agent), theme_sep(), base[0] ? base : "(no folder)");
    theme_set_text(s_header, s_header_txt, sizeof s_header_txt, buf);
    bool perm = a->kind == ATT_APPROVAL;
    theme_set_text(s_title, s_title_txt, sizeof s_title_txt,
            a->title[0] ? a->title : s->tool[0] ? s->tool : perm ? "Permission request" : "Question");
    layout_text(perm, a->text);

    if (!a->answer_sent) s_answered_ms = 0;
    else if (!s_answered_ms) s_answered_ms = now ? now : 1;
    if (a->answer_sent) {
        uint32_t rgb = RGB_DONE;
        s_icon_txt = ICON_DONE;
        const char *word = "Approved";
        s_opt_txt[0] = 0;
        if (a->answer == ATT_ANSWER_DENY) {
            word = "Denied";
            s_icon_txt = ICON_DENIED;
            rgb = RGB_ERROR;
        } else if (a->answer == ATT_ANSWER_REPLY || a->answer == ATT_ANSWER_DICTATE) {
            word = "Sent";
            s_icon_txt = ICON_SENT;
            rgb = RGB_ASK;
            if (a->answer == ATT_ANSWER_REPLY) snprintf(s_opt_txt, sizeof s_opt_txt, "\"%s\"", a->opts[a->answer_opt % 4]);
        }
        snprintf(buf, sizeof buf, "%s%sWaiting for your computer", s_opt_txt, s_opt_txt[0] ? "\n" : "");
        theme_set_text(s_cap, s_cap_txt, sizeof s_cap_txt, buf);
        int cap_y = s_opt_txt[0] ? CAP_Y - 10 : CAP_Y;
        if (lv_obj_get_y(s_cap) != cap_y) lv_obj_set_y(s_cap, cap_y);
        theme_set_text(s_word, s_word_txt, sizeof s_word_txt, word);
        if (s_icon_txt != s_icon_shown) {
            s_icon_shown = s_icon_txt;
            lv_label_set_text(s_icon, s_icon_txt);
        }
        theme_set_text_color(s_icon, lv_color_hex(rgb));
        reader_close();
        show(s_plat, false); /* the request text makes room for the answer */
        show(s_icon, true);
        show(s_word, true);
        show(s_cap, true);
        wheel_set_shown(s_wheel, false);
        hold_cancel();
        s_n = 0;
    } else {
        s_n = nav_ask_items(a, model_voice(), s_items, ASK_MAX_ITEMS);
        wheel_item_t w[ASK_MAX_ITEMS];
        for (int i = 0; i < s_n; i++) w[i] = (wheel_item_t){label_of(&s_items[i], a), fill_of(&s_items[i]), s_items[i].hold_ms > 0, i, NULL};
        int sel = wheel_count(s_wheel) == s_n ? wheel_sel(s_wheel) : 0;
        wheel_set(s_wheel, w, s_n, sel);
        wheel_set_shown(s_wheel, !s_reading);
        show(s_plat, !s_reading);
        show(s_icon, false);
        show(s_word, false);
        show(s_cap, false);
    }
    show(s_badge[1], s_n > 1 || s_reading); /* B steps through the options: nothing to step through with one */
    return true;
}

void ui_ask_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_rid = 0;
    s_now = now;
    s_holding = s_hold_fired = s_ramp_started = false;
    s_answered_ms = 0;
    s_keeping = false;
    s_read_ok = s_gate_press = false;
    s_text_txt[0] = 0;
    reader_close();
    wheel_set(s_wheel, NULL, 0, 0);
    ui_ask_refresh(now);
    ring_set_still(ui_home_ring(), true); /* the sessions stay on the ring, settled: no comet behind a decision */
    show(s_root, true);
    dbg_event("ui", "ask", "\"sid\":%u,\"rid\":%u,\"items\":%d", (unsigned)sid, (unsigned)s_rid, s_n);
}

void ui_ask_close(void)
{
    hold_cancel();
    reader_close();
    show(s_root, false);
    ring_set_still(ui_home_ring(), !ui_animations_allowed());
}

/* A hold acts on the row under the finger: it arms only when the press is on the selected row, or on no row
 * at all (a finger resting beside the button). A press on a neighbour is the start of a tap on that row. */
static bool press_on_selected(const gesture_evt_t *g)
{
    int hit = wheel_hit(s_wheel, g->x, g->y);
    return hit < 0 || hit == wheel_sel(s_wheel);
}

static void select_item(int i, const char *via, void (*haptic)(void))
{
    int n = wheel_count(s_wheel);
    if (n == 0) return;
    i = ((i % n) + n) % n;
    if (i == wheel_sel(s_wheel)) return;
    wheel_select(s_wheel, i);
    hold_cancel(); /* the hold belongs to the item it started on */
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
    case ASK_DICTATE: ui_go_voice(a->rid); break;
    case ASK_OPEN_PC:
        g_ui_actions->focus(s_sid);
        ui_go_home();
        break;
    }
}

/* Back closes the reader first; the request and what has been read stay as they are. */
bool ui_ask_back(void)
{
    if (!s_reading) return false;
    reader_close();
    ui_ask_refresh(s_now);
    return true;
}

/* A locked grant item answers a tap by showing why: the reader if the text is unread, a toast if the watch
 * can never show all of it. */
static void grant_tell(const ask_item_t *it)
{
    if (s_long && !s_read_ok) {
        reader_open();
        g_ui_actions->tick();
    } else {
        ui_toast("Too long to approve here", rgb_of(it));
        g_ui_actions->reject();
    }
}

static bool on_platter(const gesture_evt_t *g)
{
    int x = lv_obj_get_x(s_plat), y = lv_obj_get_y(s_plat);
    return g->x >= x && g->x < x + lv_obj_get_width(s_plat) && g->y >= y && g->y < y + lv_obj_get_height(s_plat);
}

/* Input while the reader is up. Nothing here can answer the request: a swipe down turns a page back and is
 * never the deny swipe, and holds do nothing. Button A is still Open on PC (handled by the caller). */
static bool read_act(nav_action_t act, const gesture_evt_t *g)
{
    switch (act) {
    case NAV_COMMIT:
        if (g->kind == G_TAP && s_now - s_read_ms < NAV_CARD_ARM_MS) return true; /* the tap that opened it, doubled */
        /* fallthrough */
    case NAV_NEXT:
    case NAV_PAGE_NEWER:
        if (s_page_i < s_pages - 1) {
            s_page_i++;
            reader_page();
        } else {
            ui_ask_back();
        }
        g_ui_actions->tick();
        return true;
    case NAV_DENY:
    case NAV_PREV:
        if (s_page_i > 0) {
            s_page_i--;
            reader_page();
            g_ui_actions->tick();
        }
        return true;
    case NAV_HOLD_PRESS:
    case NAV_HOLD_RELEASE:
    case NAV_HOLD_LONG:
    case NAV_SCRUB: return true;
    default: return false;
    }
}

bool ui_ask_act(nav_action_t act, const gesture_evt_t *g)
{
    if (s_reading && act != NAV_FOCUS) return read_act(act, g);
    if (act == NAV_PAGE_NEWER) return true; /* a swipe up only means something in the reader */
    const attention_t *a = req();
    int sel = wheel_sel(s_wheel);
    const ask_item_t *it = a && !a->answer_sent && sel < s_n ? &s_items[sel] : NULL;
    switch (act) {
    case NAV_HOLD_PRESS:
        s_hold_fired = false;
        if (it && it->hold_ms && grant_locked(it) && press_on_selected(g)) { /* locked: never arms */
            s_gate_press = true;
            s_gate_t0 = s_now;
            return true;
        }
        /* only a hold item arms a hold, only for the request on screen, and only once it is armed */
        if (it && it->hold_ms && press_on_selected(g) && nav_hold_may_start(s_rid, s_shown_ms, a->rid, s_now)) {
            s_holding = true;
            s_ramp_started = false;
            s_hold_t0 = s_now;
            s_hold_rid = a->rid;
            s_hold_idx = sel;
        }
        return true;
    case NAV_HOLD_RELEASE:
        s_gate_press = false;
        hold_cancel();
        return true;
    case NAV_COMMIT: {
        /* the card must have been on screen a moment before a tap means anything: the tap that opened it, or
         * a second one right behind it, would otherwise land on a neighbour row */
        if (g->kind == G_TAP && s_now - s_shown_ms < NAV_CARD_ARM_MS) return true;
        /* A tap acts on the row it lands on (UI-V3 §3.3), but never approves or denies: a neighbour that
         * needs a hold is only selected, and the toast says which hold it needs. */
        if (it && s_long && on_platter(g)) { /* the text itself opens the reader */
            reader_open();
            g_ui_actions->tick();
            return true;
        }
        int hit = it ? wheel_hit(s_wheel, g->x, g->y) : -1;
        if (hit >= 0 && hit < s_n && hit != sel) {
            const ask_item_t *h = &s_items[hit];
            select_item(hit, "tap", g_ui_actions->tick);
            if (h->hold_ms && grant_locked(h)) grant_tell(h);
            else if (h->hold_ms) ui_toast(h->grants ? "Hold to approve" : "Hold to deny", rgb_of(h));
            else commit(a, hit, 0);
            return true;
        }
        if (!it) {
            g_ui_actions->reject();
            return true;
        }
        if (it->hold_ms && grant_locked(it)) {
            grant_tell(it);
            return true;
        }
        if (it->hold_ms) { /* a tap never commits an approve-class or deny item */
            ui_toast(it->grants ? "Hold to approve" : "Hold to deny", rgb_of(it));
            g_ui_actions->reject();
            return true;
        }
        commit(a, sel, 0);
        return true;
    }
    case NAV_HOLD_LONG: /* injected input whose duration already covers the hold */
        if (it && it->hold_ms && !grant_locked(it) && g->dur_ms >= it->hold_ms && !s_hold_fired && press_on_selected(g) &&
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
    if (!model_find(s_sid)) return true;
    if (!a) return !(s_answered_ms && now - s_answered_ms < ANSWERED_MIN_MS);
    if (s_gate_press && now - s_gate_t0 >= GATE_TELL_MS) { /* a hold on a locked Approve: say why, once */
        s_gate_press = false;
        int gs = wheel_sel(s_wheel);
        if (!a->answer_sent && gs < s_n && s_items[gs].hold_ms && grant_locked(&s_items[gs])) {
            ui_toast(s_long && !s_read_ok ? "Read it first" : "Too long to approve here", rgb_of(&s_items[gs]));
            g_ui_actions->reject();
        }
    }
    if (s_holding && !s_hold_fired) {
        uint32_t el = now - s_hold_t0;
        const ask_item_t *it = s_hold_idx < s_n ? &s_items[s_hold_idx] : NULL;
        if (a->rid != s_hold_rid || a->answer_sent || !it || !it->hold_ms || s_hold_idx != wheel_sel(s_wheel) ||
            grant_locked(it)) {
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
            wheel_set_progress(s_wheel, (int)(el * 1000 / it->hold_ms));
        }
        if (el >= it->hold_ms) {
            s_hold_fired = true;
            s_holding = false;
            s_ramp_started = false;
            ring_set_progress(ui_home_ring(), 0, rgb);
            wheel_set_progress(s_wheel, 0);
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
    if ((a && a->answer_sent) || s_keeping) {
        ui_report_add(r, "%s", s_word_txt);
        if (s_opt_txt[0]) ui_report_add(r, "%s", s_opt_txt);
        ui_report_add(r, "Waiting for your computer");
        return;
    }
    if (s_reading) {
        ui_report_add(r, "Reading %d of %d", s_page_i + 1, s_pages);
        return;
    }
    if (s_long) ui_report_add(r, "Read all");
    int n = wheel_count(s_wheel), sel = wheel_sel(s_wheel);
    for (int k = 0; k < n && k < 4; k++) {
        const wheel_item_t *it = wheel_item(s_wheel, (sel + k) % n);
        ui_report_add(r, "%s%s%s", k == 0 ? "> " : "", it->label, it->hold ? " HOLD" : "");
    }
    ui_report_add(r, "%d options", n);
}

static lv_obj_t *line(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, int y, int w, int lines)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(f));
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
    s_header = line(s_root, FONT_CAPTION, COL_TEXT2, CAPTION_Y, theme_chord(CAPTION_Y + 10, 20), 1);
    s_title = line(s_root, FONT_HEAD, COL_TEXT, TITLE_Y, theme_chord(TITLE_Y + 15, 20), 1);
    s_plat = lv_obj_create(s_root); /* the platter for a permission's text; transparent for a question's */
    lv_obj_remove_style_all(s_plat);
    lv_obj_remove_flag(s_plat, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(s_plat, UI_PLATTER_R, 0);
    theme_set_bg_color(s_plat, COL_FILL1);
    s_text = lv_label_create(s_plat);
    lv_obj_set_style_text_font(s_text, FONT_BODY, 0);
    lv_obj_set_style_text_color(s_text, COL_TEXT, 0);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_MODE_DOTS);
    s_more = lv_label_create(s_plat);
    lv_obj_set_style_text_font(s_more, FONT_CAPTION, 0);
    lv_obj_set_style_text_align(s_more, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(s_more, "Read all");
    show(s_more, false);
    s_read = lv_obj_create(s_root); /* the reader: a window READ_LINES tall over the whole text */
    lv_obj_remove_style_all(s_read);
    lv_obj_remove_flag(s_read, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_read, READ_W, READ_LINES * lv_font_get_line_height(FONT_BODY));
    lv_obj_set_pos(s_read, UI_CX - READ_W / 2, READ_Y);
    s_read_lbl = lv_label_create(s_read);
    lv_obj_set_style_text_font(s_read_lbl, FONT_BODY, 0);
    lv_obj_set_style_text_color(s_read_lbl, COL_TEXT, 0);
    lv_obj_set_style_text_align(s_read_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_read_lbl, READ_W);
    lv_label_set_text_static(s_read_lbl, "");
    s_page = line(s_root, FONT_CAPTION, COL_TEXT2, READ_Y + READ_LINES * lv_font_get_line_height(FONT_BODY) + 6, 160, 1);
    show(s_read, false);
    show(s_page, false);
    s_wheel = wheel_create_at(s_root, ASK_WHEEL);
    s_icon = line(s_root, FONT_TITLE, COL_TEXT, ICON_Y, 80, 1);
    lv_obj_set_style_text_font(s_icon, FONT_ICON_L, 0);
    lv_obj_set_height(s_icon, 44);
    s_word = line(s_root, FONT_TITLE, COL_TEXT, WORD_Y, 340, 1);
    s_cap = line(s_root, FONT_CAPTION, COL_TEXT2, CAP_Y, 300, 2);
    show(s_icon, false);
    show(s_word, false);
    show(s_cap, false);
    static const char *const words[2] = {"Open on PC", "Next"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_wlbl[i]);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
