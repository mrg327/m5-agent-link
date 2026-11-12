/* The AGENT screen (UI-V3 §4.2): the selected session's actions on the wheel, with gesture shortcuts
 * layered on it. The item list comes from nav_agent_items() (interaction model); this file only
 * names, colours and places the items and runs the one the user commits. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "nav.h"
#include "ui_gauges.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "ui_wheel.h"

#define LIST_Y 232 /* UI-V3 §4.2: the list's centre (73 px selected row); 250 under a question */
#define LIST_Y_Q 250
#define QUESTION_Y 62 /* two lines of FONT_BODY_STRONG span y 64-112 */

static lv_obj_t *s_root, *s_header, *s_wheel, *s_wheel_main, *s_wheel_q, *s_question;
static char s_question_txt[128];
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static uint32_t s_sid, s_opened_ms, s_last_input, s_now;
static bool s_holding, s_hold_fired, s_ramp_started;
static uint32_t s_hold_t0, s_hold_ms;
/* "Reply" turns the wheel into the quick-reply list (issue b150); its items carry REPLY_ID + the
 * cmd type argument: the session's own answers to its closing question first (100 + i), then the
 * daemon's fixed replies (i). The question itself shows above the list. */
static bool s_replies;
#define REPLY_ID 1000
#define SESSION_REPLY 100
#define KEYS_ID 998 /* "Keyboard" after "Dictate": opens KEYS (issue 1e57) */
#define DICTATE_ID 999 /* "Dictate" at the end of the Reply list: opens VOICE (issue 0ea9) */
static char s_header_txt[64];

/* Presentation of the interaction model's action ids. */
static const char *const s_labels[AG_COUNT] = {
    [AG_FOCUS] = "Open on PC", [AG_DISMISS] = "Dismiss",
    [AG_SNOOZE] = "Snooze 30 min", [AG_NOTIFY] = "Notify when done", [AG_WATCH] = "Activity",
    [AG_REPLY] = "Reply",          [AG_INTERRUPT] = "Stop",       [AG_RERUN] = "Run again",
};

static const char *const s_icons[AG_COUNT] = {
    [AG_FOCUS] = ICON_OPEN,   [AG_DISMISS] = ICON_DENIED, [AG_SNOOZE] = ICON_IDLE,  [AG_NOTIFY] = ICON_BELL,
    [AG_WATCH] = ICON_ACTIVITY, [AG_REPLY] = ICON_REPLY,  [AG_INTERRUPT] = ICON_STOP, [AG_RERUN] = ICON_WORKING,
};

static const session_t *session(void) { return model_find(s_sid); }

/* Snooze and Notify are toggles: the label says what a commit will do now. */
static bool snoozed(const session_t *s) { return model_session_snoozed(s, theme_now_unix()); }

static const char *label_of(agent_action_t id, const session_t *s)
{
    if (id == AG_SNOOZE && snoozed(s)) return "Unsnooze";
    if (id == AG_NOTIFY && s->ntf) return "Don't notify";
    return s_labels[id];
}

static void hold_cancel(void)
{
    if (!s_holding) return;
    s_holding = false;
    if (s_ramp_started) g_ui_actions->hold_stop();
    s_ramp_started = false;
    ring_set_progress(ui_home_ring(), 0, 0xffffff);
    wheel_set_progress(s_wheel, 0);
}

bool ui_agent_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = session();
    if (!s || s->state == SESS_ENDED) return false;
    char buf[64];
    const char *base = theme_basename(s->repo);
    char host[24];
    theme_host_prefix(host, sizeof host, s);
    /* the caption is the project (host first when remote); Reply says what the list is for */
    snprintf(buf, sizeof buf, "%s%s", s_replies ? "Reply to " : host, base[0] ? base : "(no folder)");
    if (strcmp(buf, s_header_txt)) {
        snprintf(s_header_txt, sizeof s_header_txt, "%s", buf);
        lv_label_set_text(s_header, buf);
    }
    uint32_t rgb = theme_session_rgb(s);
    wheel_item_t w[WHEEL_MAX_ITEMS];
    int n = 0;
    if (s_replies) {
        for (int i = 0; i < s->n_rp && n < WHEEL_MAX_ITEMS; i++)
            w[n++] = (wheel_item_t){s->rp[i], RGB_ASK, false, REPLY_ID + SESSION_REPLY + i, ICON_REPLY};
        for (int i = 0; i < model_reply_count() && n < WHEEL_MAX_ITEMS; i++)
            w[n++] = (wheel_item_t){model_reply(i), RGB_ASK, false, REPLY_ID + i, ICON_REPLY};
        if (model_voice() && n < WHEEL_MAX_ITEMS) w[n++] = (wheel_item_t){"Dictate", RGB_ASK, false, DICTATE_ID, ICON_MIC};
        if (model_keys() && n < WHEEL_MAX_ITEMS) w[n++] = (wheel_item_t){"Keyboard", RGB_ASK, false, KEYS_ID, ICON_KEYBOARD};
    } else {
        agent_item_t items[WHEEL_MAX_ITEMS];
        if (theme_is_job(s)) n = nav_job_items(s->state, items, WHEEL_MAX_ITEMS);
        else n = nav_agent_items(s->state, model_can_type(), model_reply_count() > 0 || s->n_rp > 0 || model_voice() || model_keys(), items, WHEEL_MAX_ITEMS);
        for (int i = 0; i < n; i++)
            w[i] = (wheel_item_t){label_of(items[i].id, s), items[i].id == AG_INTERRUPT ? RGB_DANGER : items[i].id == AG_RERUN ? RGB_WORKING : rgb, items[i].hold,
                                  (int)items[i].id, s_icons[items[i].id]};
    }
    const char *q = s_replies ? s->q : "";
    lv_obj_t *want = q[0] ? s_wheel_q : s_wheel_main; /* a question pushes the list down */
    if (want != s_wheel) {
        int keep = wheel_sel(s_wheel);
        wheel_set_shown(s_wheel, false);
        s_wheel = want;
        wheel_set_shown(s_wheel, true);
        wheel_set(s_wheel, w, n, keep);
    } else {
        wheel_set(s_wheel, w, n, wheel_count(s_wheel) == n ? wheel_sel(s_wheel) : 0);
    }
    if (strcmp(q, s_question_txt)) {
        snprintf(s_question_txt, sizeof s_question_txt, "%s", q);
        lv_label_set_text(s_question, q);
    }
    if (n > 1) lv_obj_remove_flag(s_badge[1], LV_OBJ_FLAG_HIDDEN); /* B steps through the items: none to step to with one */
    else lv_obj_add_flag(s_badge[1], LV_OBJ_FLAG_HIDDEN);
    if (q[0]) {
        lv_obj_remove_flag(s_question, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_header, LV_OBJ_FLAG_HIDDEN); /* the question owns the top of the screen */
    } else {
        lv_obj_add_flag(s_question, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_header, LV_OBJ_FLAG_HIDDEN);
    }
    ui_gauges_agent_update(s, !s_replies); /* GAUGES.md: model, mode and pull request under the caption */
    return true;
}

void ui_agent_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_opened_ms = s_last_input = s_now = now;
    s_holding = s_hold_fired = s_ramp_started = false;
    s_replies = false;
    s_question_txt[0] = 0;
    s_header_txt[0] = 0;
    ui_home_rim_mode(true); /* a rim drag dials through the items */
    ring_set_still(ui_home_ring(), true); /* the sessions stay on the ring, settled: the comets cost redraws */
    wheel_set(s_wheel, NULL, 0, 0);
    ui_agent_refresh(now);
    const session_t *s = session();
    /* looking at it is seeing it; not for a job, whose ack is Dismiss: the card would go from under the finger */
    if (s && s->state == SESS_DONE && !s->seen && !theme_is_job(s)) g_ui_actions->ack(sid);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    dbg_event("ui", "agent", "\"sid\":%u,\"items\":%d", (unsigned)sid, wheel_count(s_wheel));
}

void ui_agent_close(void)
{
    hold_cancel();
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    ring_set_still(ui_home_ring(), !ui_animations_allowed());
}

/* A hold acts on the row under the finger: it arms only when the press is on the selected row. A finger held
 * still anywhere off the rows goes back. */
static bool press_on_selected(const gesture_evt_t *g) { return wheel_hit(s_wheel, g->x, g->y) == wheel_sel(s_wheel); }

static int s_scrub_last, s_drag_acc;

static void select_item(int i, const char *via, void (*haptic)(void))
{
    int n = wheel_count(s_wheel);
    if (n == 0) return;
    i = ((i % n) + n) % n;
    if (i == wheel_sel(s_wheel)) return;
    wheel_select(s_wheel, i);
    hold_cancel(); /* the hold belongs to the item it started on */
    dbg_event("ui", "item", "\"idx\":%d,\"id\":%d,\"via\":\"%s\"", i, wheel_item(s_wheel, i)->id, via);
    if (haptic) haptic();
}

static void commit(const wheel_item_t *it)
{
    const session_t *s = session();
    if (!s) return;
    dbg_event("ui", "commit", "\"sid\":%u,\"id\":%d", (unsigned)s->sid, it->id);
    if (it->id == DICTATE_ID) {
        g_ui_actions->tick();
        ui_go_voice(0);
        return;
    }
    if (it->id == KEYS_ID) {
        g_ui_actions->tick();
        ui_go_keys();
        return;
    }
    if (it->id >= REPLY_ID) { /* a quick reply: the daemon types it (it never takes text from here) */
        g_ui_actions->type_reply(s->sid, it->id - REPLY_ID);
        ui_go_home();
        return;
    }
    switch ((agent_action_t)it->id) {
    case AG_FOCUS:
        g_ui_actions->focus(s->sid);
        if (s->state == SESS_DONE) g_ui_actions->ack(s->sid);
        break;
    case AG_DISMISS: g_ui_actions->ack(s->sid); g_ui_actions->tick(); break;
    case AG_SNOOZE: g_ui_actions->snooze(s->sid, snoozed(s) ? 0 : 30); break;
    case AG_NOTIFY: g_ui_actions->notify_done(s->sid, !s->ntf); break;
    case AG_REPLY: /* the same wheel, now listing the quick replies */
        s_replies = true;
        wheel_set(s_wheel, NULL, 0, 0);
        ui_agent_refresh(s_now);
        g_ui_actions->tick();
        dbg_event("ui", "replies", "\"sid\":%u,\"n\":%d", (unsigned)s->sid, model_reply_count());
        return;
    case AG_INTERRUPT: g_ui_actions->interrupt(s->sid, s_hold_ms); break; /* only ever after the hold */
    case AG_RERUN: g_ui_actions->rerun(s->sid, s_hold_ms); break;         /* only ever after the hold */
    case AG_WATCH: /* the monitor for this session (UI-V2 §4.7) */
        g_ui_actions->tick();
        ui_go_watch();
        return;
    default: break;
    }
    ui_go_home();
}

bool ui_agent_act(nav_action_t a, const gesture_evt_t *g)
{
    if (g->kind != G_LIFT && g->kind != G_SHAKE) s_last_input = s_now;
    const wheel_item_t *it = wheel_item(s_wheel, wheel_sel(s_wheel));
    switch (a) {
    case NAV_HOLD_PRESS:
        s_hold_fired = false;
        s_drag_acc = 0;
        /* only a destructive item arms a hold, and only once the wheel has been on screen a moment */
        if (it && it->hold && press_on_selected(g) && s_now - s_opened_ms >= NAV_CARD_ARM_MS) {
            s_holding = true;
            s_ramp_started = false;
            s_hold_t0 = s_now;
        }
        return true;
    case NAV_HOLD_RELEASE: hold_cancel(); return true;
    case NAV_HOLD_LONG:
        if (wheel_hit(s_wheel, g->x, g->y) < 0) ui_go_back();
        return true;
    case NAV_SCROLL_DRAG: {
        hold_cancel(); /* a moving finger is not holding anything */
        int d = nav_list_drag(&s_drag_acc, g->dy);
        if (d) select_item(wheel_sel(s_wheel) + d, "drag", g_ui_actions->detent);
        return true;
    }
    case NAV_COMMIT:
        if (!it) return false;
        if (g->kind == G_TAP && s_now - s_opened_ms < NAV_CARD_ARM_MS) return true; /* too soon after opening */
        if (g->kind == G_TAP) { /* the row you touch is the row you get (UI-V3 §3.3) */
            int i = wheel_hit(s_wheel, g->x, g->y);
            if (i >= 0 && i != wheel_sel(s_wheel)) {
                select_item(i, "tap", g_ui_actions->tick);
                it = wheel_item(s_wheel, wheel_sel(s_wheel));
            }
        }
        if (it->hold) { /* a tap never commits a destructive item */
            ui_toast(it->id == AG_RERUN ? "Hold to run again" : "Hold to stop", it->rgb);
            g_ui_actions->reject();
            return true;
        }
        commit(it);
        return true;
    case NAV_SCRUB: {
        int d = nav_list_scrub(&s_scrub_last, g->kind, g->x);
        if (d) select_item(wheel_sel(s_wheel) + d, "scrub", g_ui_actions->detent);
        return true;
    }
    case NAV_NEXT: select_item(wheel_sel(s_wheel) + 1, "btn", g_ui_actions->tick); return true;
    case NAV_PREV: select_item(wheel_sel(s_wheel) - 1, "btn", g_ui_actions->tick); return true;
    default: return false;
    }
}

/* Returns true when the screen wants to close (timeout, or its session went away). */
bool ui_agent_tick(uint32_t now)
{
    s_now = now;
    if (!session()) return true;
    if (s_holding && !s_hold_fired) {
        const wheel_item_t *it = wheel_item(s_wheel, wheel_sel(s_wheel));
        uint32_t el = now - s_hold_t0;
        if (!it || !it->hold) {
            hold_cancel();
        } else {
            if (el >= NAV_HOLD_SHOW_MS) {
                if (!s_ramp_started) {
                    s_ramp_started = true;
                    g_ui_actions->hold_start();
                }
                ring_set_progress(ui_home_ring(), (int)(el * 1000 / NAV_HOLD_MS), it->rgb);
                wheel_set_progress(s_wheel, (int)(el * 1000 / NAV_HOLD_MS));
            }
            if (el >= NAV_HOLD_MS) {
                s_hold_ms = el;
                s_hold_fired = true;
                s_holding = false;
                s_ramp_started = false;
                ring_set_progress(ui_home_ring(), 0, it->rgb);
                wheel_set_progress(s_wheel, 0);
                commit(it);
                return false;
            }
        }
        return false;
    }
    return now - s_last_input > NAV_AGENT_TIMEOUT_MS;
}

void ui_agent_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "agent");
    r->has_sel_sid = session() != NULL;
    r->sel_sid = s_sid;
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    ui_gauges_agent_report(r);
    if (s_replies && s_question_txt[0]) ui_report_add(r, "%s", s_question_txt);
    int n = wheel_count(s_wheel), sel = wheel_sel(s_wheel);
    for (int k = 0; k < n && k < 5; k++) {
        const wheel_item_t *it = wheel_item(s_wheel, (sel + k) % n);
        ui_report_add(r, "%s%s", k == 0 ? "> " : "", it->label);
    }
    ui_report_add(r, "%d items", n);
}

lv_obj_t *ui_agent_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    int lh = lv_font_get_line_height(FONT_CAPTION), cw = theme_chord(UI_Y_CAPTION, 24);
    s_header = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_header, FONT_CAPTION, 0);
    lv_obj_set_style_text_color(s_header, COL_TEXT2, 0);
    lv_label_set_long_mode(s_header, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s_header, cw, lh);
    lv_obj_set_style_text_align(s_header, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_header, UI_CX - cw / 2, UI_Y_CAPTION - lh / 2);
    ui_gauges_agent_create(s_root);
    s_wheel_main = s_wheel = wheel_create_at(s_root, WHEEL_LAYOUT_LIST_TALL(LIST_Y));
    s_wheel_q = wheel_create_at(s_root, WHEEL_LAYOUT_LIST_TALL(LIST_Y_Q));
    wheel_set_shown(s_wheel_q, false);
    int qw = theme_chord(70, 0), qh = 2 * lv_font_get_line_height(FONT_BODY_STRONG);
    s_question = lv_label_create(s_root); /* the agent's closing question, above the Reply list */
    lv_obj_set_style_text_font(s_question, FONT_BODY_STRONG, 0);
    lv_obj_set_style_text_color(s_question, COL_TEXT, 0);
    lv_obj_set_size(s_question, qw, qh);
    lv_label_set_long_mode(s_question, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s_question, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_question, UI_CX - qw / 2, QUESTION_Y);
    lv_obj_add_flag(s_question, LV_OBJ_FLAG_HIDDEN);
    static const char *const words[2] = {"Select", "Next"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
