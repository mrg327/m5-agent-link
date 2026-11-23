/* The AGENT screen in Sigil (docs/design/GLYPHS.md §5): what can be done with the selected session.
 *
 * The menu is an orbit (rule 5): one disc for each action, arched over the session's sigil from the
 * left to the right, open at the bottom where the two key hints close the circle. The selected disc
 * is the large one in the session's colour (Stop in red, Run again in blue). Under the sigil is the
 * project's name: the one place a mark is spelled out (rule 2). Below it the gauges' readout
 * (ui_gauges.c) and, between the key hints, the message that waits to be typed into the session,
 * behind the queue glyph (the user's own words: rule 1).
 *
 * Reply turns the screen into the reply list: the reply glyph beside the sigil, the agent's closing
 * question (what it asks: rule 1), the quick replies as rows of text (the user's own words, and the
 * session's own answers), the selected one in the middle between two arcs, and Dictate and Keyboard
 * as discs between the key hints. Page dots say where the selection is.
 *
 * The item list comes from nav_agent_items() (interaction model); this file names, draws and runs
 * the items. Every glyph is named in words in the report (rule 9), as the rows were. Gestures are
 * the list's: the dial, a vertical drag and button B step the selection; a tap on a disc or a row
 * runs it, a tap elsewhere and button A run the selection; Stop and Run again run only by a hold on
 * their own disc, which fills its ring and the bezel; a finger held still anywhere else goes back.
 * Redraw discipline (UI-V2 §8a): only change-only setters; nothing is drawn on a tick but a hold's
 * arc. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "detail.h"
#include "graph.h"
#include "nav.h"
#include "ui_gauges.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"

/* Geometry: centres. The orbit's discs sit on r 150 about the screen centre, between -111 and +111
 * degrees; the key hints are at (101, 365) and (365, 365), so below y 345 a row is only as wide as
 * the gap between them. */
/* ends 24 degrees short of each key hint: clear of a selected disc and its hold ring */
#define ARCH_SPAN 222
#define ARCH_PITCH 50 /* a few discs sit together at the top */
#define SIGIL_Y 212   /* the subject: inside the orbit, which leaves r 105 free */
#define SIGIL_R 56
#define NAME_Y 298 /* the project's name, under its mark */
#define NAME_W 200
#define READOUT_Y 330 /* the gauges' readout: model, mode, pull request */
#define READOUT_W 208
#define READOUT_H 28
/* ui_gauges.c: where the readout places itself in the parent it is given */
#define GAUGES_OWN_Y 108
#define QUEUED_Y 368 /* the queued message: between the key hints */
#define QUEUED_W 200
/* the reply list */
#define HEAD_Y 96   /* the reply glyph and the sigil, side by side ... */
#define HEAD_Y_Q 44 /* ... higher when the agent's question is under them */
#define HEAD_DX 30
#define HEAD_SIGIL_R 22
#define QUESTION_Y 72 /* two lines of FONT_BODY_STRONG */
#define QUESTION_W 270
#define ROW_Y 236 /* the selected row; its neighbours a pitch above and below */
#define ROW_PITCH 54
#define ROW_W 300
/* the two arcs that hold the selected row: on a circle about the row's centre */
#define FOCUS_R 184
#define FOCUS_W 5
#define FOCUS_DEG 18
#define PIPS_Y 328
#define TALK_Y 374 /* Dictate and Keyboard: discs between the key hints */
#define TALK_R 26
#define TALK_DX 36
#define TALK_Y_ALONE 236 /* with no quick reply at all they are the screen */
#define TALK_R_ALONE UI_DISC_R_SECONDARY
#define TALK_DX_ALONE 66

/* a tap on a hold item: this much of its ring shows for a moment, the part still to fill */
#define NUDGE_PM 200
#define NUDGE_MS 700

#define MAX_ITEMS 10
#define LABEL_CAP 64
#define QUEUE_MIN_MINOR 17
#define REPLY_ID 1000
#define SESSION_REPLY 100
#define KEYS_ID 998    /* "Keyboard" after "Dictate": opens KEYS (issue 1e57) */
#define DICTATE_ID 999 /* "Dictate" at the end of the Reply list: opens VOICE (issue 0ea9) */

typedef struct {
    const char *glyph; /* the disc's glyph; NULL for a row of text */
    uint32_t rgb;      /* the colour of a hold item and its arc; else the session's */
    bool hold;         /* destructive: commits only by a hold on its own disc */
    /* agent_action_t, or REPLY_ID + the cmd type argument, DICTATE_ID, KEYS_ID */
    int id;
} item_t;

static lv_obj_t *s_root, *s_menu, *s_reply;
/* the menu */
static lv_obj_t *s_orbit, *s_sigil, *s_name, *s_readout_box, *s_queued;
static lv_obj_t *s_head_glyph, *s_head_sigil, *s_question, *s_row[3], *s_focus[2], *s_pips,
    *s_talk[2];             /* the reply list */
static lv_obj_t *s_hint[2]; /* [0] = A, [1] = B */

static item_t s_items[MAX_ITEMS];
/* items; the selection; how many are rows of text; the row in the middle */
static int s_n, s_sel, s_rows, s_row_at;
/* What the screen says in words, for the report (rule 9), and the caches of its labels. The item
 * names and the texts are in PSRAM: the screen's only large statics. */
MODEL_EXT_BSS static char s_labels[MAX_ITEMS][LABEL_CAP];
MODEL_EXT_BSS static char s_row_txt[3][LABEL_CAP];
MODEL_EXT_BSS static char s_question_txt[128], s_queued_txt[112], s_queued_lbl[120];
static char s_header_txt[64], s_name_txt[48];
static uint32_t s_sid, s_opened_ms, s_last_input, s_now;
static bool s_holding, s_hold_fired, s_ramp_started;
static uint32_t s_hold_t0, s_hold_ms, s_nudge_until;
/* "Reply" turns the screen into the quick-reply list (issue b150); its items carry REPLY_ID + the
 * cmd type argument: the session's own answers to its closing question first (100 + i), then the
 * daemon's fixed replies (i). The question itself shows above the list. */
static bool s_replies;
/* "Reply now" (a working session) opens the same list in queue mode: the fixed replies and Dictate
 * only (no Keyboard, no answers of the session's own). The cmd carries q (proto 0.17): the daemon
 * types it at once, mid-turn, and holds it only while the agent waits on an approval or a question
 * (PROTOCOL §6d). */
static bool s_queue;
static int s_scrub_last, s_drag_acc;

/* Presentation of the interaction model's action ids: the words (the report), the glyphs (the
 * glass). */
static const char *const kWords[AG_COUNT] = {
    [AG_FOCUS] = "Open on PC",        [AG_DISMISS] = "Dismiss", [AG_SNOOZE] = "Snooze 30 min",
    [AG_NOTIFY] = "Notify when done", [AG_WATCH] = "Activity",  [AG_REPLY] = "Reply",
    [AG_INTERRUPT] = "Stop",          [AG_RERUN] = "Run again", [AG_QUEUE] = "Reply now",
    [AG_UNQUEUE] = "Cancel queued",   [AG_READ] = "Read",       [AG_GRAPH] = "Graph",
};

static const char *const kGlyph[AG_COUNT] = {
    [AG_FOCUS] = ICON_COMPUTER,    [AG_DISMISS] = ICON_X,      [AG_SNOOZE] = ICON_IDLE,
    [AG_NOTIFY] = ICON_NOTIFY,     [AG_WATCH] = ICON_ACTIVITY, [AG_REPLY] = ICON_REPLY,
    [AG_INTERRUPT] = ICON_STOP,    [AG_RERUN] = ICON_RERUN,    [AG_QUEUE] = ICON_REPLY,
    [AG_UNQUEUE] = ICON_BACKSPACE, /* rubs out the message that waits */
    [AG_READ] = ICON_READ,         [AG_GRAPH] = ICON_GRAPH,
};

static const session_t *session(void) { return model_find(s_sid); }

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Snooze and Notify are toggles: the word and the glyph say what a commit will do now. */
static bool snoozed(const session_t *s) { return model_session_snoozed(s, theme_now_unix()); }

static void add(const char *label, const char *glyph, uint32_t rgb, bool hold, int id)
{
    if (s_n >= MAX_ITEMS) return;
    snprintf(s_labels[s_n], LABEL_CAP, "%s", label);
    s_items[s_n++] = (item_t){glyph, rgb, hold, id};
}

/* The items of the screen as it stands: the session's actions, or the reply list. */
static void build_items(const session_t *s)
{
    s_n = s_rows = 0;
    if (s_replies) {
        for (int i = 0; !s_queue && i < s->n_rp && s_n < MAX_ITEMS - 2; i++)
            add(s->rp[i], NULL, RGB_ASK, false, REPLY_ID + SESSION_REPLY + i);
        for (int i = 0; i < model_reply_count() && s_n < MAX_ITEMS - 2; i++)
            add(model_reply(i), NULL, RGB_ASK, false, REPLY_ID + i);
        s_rows = s_n;
        if (model_voice()) add("Dictate", ICON_MIC, RGB_ASK, false, DICTATE_ID);
        if (!s_queue && model_keys()) add("Keyboard", ICON_KEYBOARD, RGB_ASK, false, KEYS_ID);
        return;
    }
    agent_item_t items[MAX_ITEMS];
    int n;
    if (theme_is_job(s)) n = nav_job_items(s->state, items, MAX_ITEMS);
    else {
        bool queue_ok = model_peer_minor() >= QUEUE_MIN_MINOR && model_can_type() &&
                        (model_reply_count() > 0 || model_voice());
        n = nav_agent_items(s->state, model_can_type(),
                            model_reply_count() > 0 || s->n_rp > 0 || model_voice() || model_keys(),
                            queue_ok, s->queued[0] != 0,
                            detail_msg_fetchable(s) && !detail_msg_whole(s),
                            model_peer_minor() >= GRAPH_MINOR, items, MAX_ITEMS);
    }
    for (int i = 0; i < n; i++) {
        agent_action_t id = items[i].id;
        bool undo = (id == AG_SNOOZE && snoozed(s)) || (id == AG_NOTIFY && s->ntf);
        add(id == AG_SNOOZE && undo   ? "Unsnooze"
            : id == AG_NOTIFY && undo ? "Don't notify"
                                      : kWords[id],
            id == AG_SNOOZE && undo   ? ICON_UNSNOOZE
            : id == AG_NOTIFY && undo ? ICON_QUIET
                                      : kGlyph[id],
            id == AG_INTERRUPT ? RGB_DANGER
            : id == AG_RERUN   ? RGB_WORKING
                               : 0,
            items[i].hold, (int)id);
    }
}

static int find_item(int id)
{
    for (int i = 0; i < s_n; i++)
        if (s_items[i].id == id) return i;
    return -1;
}

static void hold_cancel(void)
{
    if (s_nudge_until) { /* the ring a refused tap showed */
        s_nudge_until = 0;
        ui_orbit_set_hold(s_orbit, 0);
    }
    if (!s_holding) return;
    s_holding = false;
    if (s_ramp_started) g_ui_actions->hold_stop();
    s_ramp_started = false;
    ring_set_progress(ui_home_ring(), 0, 0xffffff);
    ui_orbit_set_hold(s_orbit, 0);
}

/* ───────── the menu: an orbit over the sigil ───────── */

static void draw_menu(const session_t *s)
{
    ui_orbit_item_t o[MAX_ITEMS];
    for (int i = 0; i < s_n; i++) o[i] = (ui_orbit_item_t){s_items[i].glyph, s_items[i].rgb, false};
    ui_orbit_set_accent(s_orbit, theme_session_rgb(s));
    ui_orbit_set_items(s_orbit, o, s_n);
    ui_orbit_select(s_orbit, s_sel);

    const char *base = theme_basename(s->repo);
    char name[sizeof s_name_txt];
    ui_sigil_set_session(s_sigil, base, s->sid);
    /* the name, with the globe of a remote session before it; no folder is the dashed folder
     * alone */
    snprintf(name, sizeof name, "%s%s", s->host && base[0] ? ICON_G_WEB " " : "", base);
    theme_set_text(s_name, s_name_txt, sizeof s_name_txt, name);

    char qd[sizeof s_queued_txt] = "", lbl[sizeof s_queued_lbl] = "";
    if (s->queued[0] && !theme_is_job(s)) {
        snprintf(qd, sizeof qd, "Queued: %s", s->queued);
        snprintf(lbl, sizeof lbl, ICON_QUEUED " %s", s->queued);
    }
    snprintf(s_queued_txt, sizeof s_queued_txt, "%s", qd);
    theme_set_text(s_queued, s_queued_lbl, sizeof s_queued_lbl, lbl);
    show(s_queued, lbl[0] != 0);
}

/* ───────── the reply list: rows of text, and two discs ───────── */

static int talk_item(int k) { return find_item(k == 0 ? DICTATE_ID : KEYS_ID); }

/* The row drawn `k` rows from the middle one (-1, 0, 1), or -1 when there is none: the list wraps,
 * and two rows are the selected one and the other below it. */
static int row_item(int k)
{
    if (s_rows == 0 || (k < 0 && s_rows < 3) || (k > 0 && s_rows < 2)) return -1;
    return ((s_row_at + k) % s_rows + s_rows) % s_rows;
}

static void draw_replies(const session_t *s)
{
    if (s_sel < s_rows) s_row_at = s_sel; /* a disc selected: the rows stay where they were */
    if (s_row_at >= s_rows) s_row_at = 0;
    bool on_row = s_sel < s_rows;
    for (int k = -1; k <= 1; k++) {
        int i = row_item(k);
        lv_obj_t *l = s_row[k + 1];
        show(l, i >= 0);
        theme_set_text(l, s_row_txt[k + 1], LABEL_CAP, i >= 0 ? s_labels[i] : "");
    }
    /* the selected reply is as large as it can be and still show whole */
    int mid = row_item(0);
    lv_point_t sz = {0, 0};
    if (mid >= 0)
        lv_text_get_size(&sz, s_labels[mid], FONT_TITLE, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_font_t *f = sz.x <= ROW_W ? FONT_TITLE : FONT_HEAD;
    if (lv_obj_get_style_text_font(s_row[1], 0) != f) {
        int lh = lv_font_get_line_height(f);
        lv_obj_set_style_text_font(s_row[1], f, 0);
        lv_obj_set_height(s_row[1], lh);
        lv_obj_set_y(s_row[1], ROW_Y - lh / 2);
    }
    theme_set_text_color(s_row[1], on_row ? COL_TEXT : COL_TEXT2);
    for (int i = 0; i < 2; i++) show(s_focus[i], on_row);
    ui_pips_set(s_pips, s_n, s_sel);

    /* Dictate and Keyboard: small between the key hints, or the screen itself when there is no
     * row */
    bool alone = s_rows == 0;
    int both = talk_item(0) >= 0 && talk_item(1) >= 0, dx = alone ? TALK_DX_ALONE : TALK_DX;
    for (int k = 0; k < 2; k++) {
        int i = talk_item(k);
        show(s_talk[k], i >= 0);
        if (i < 0) continue;
        ui_disc_set_r(s_talk[k], alone ? TALK_R_ALONE : TALK_R);
        ui_disc_set_pos(s_talk[k], UI_CX + (both ? (k == 0 ? -dx : dx) : 0),
                        alone ? TALK_Y_ALONE : TALK_Y);
        ui_disc_set_rgb(s_talk[k], i == s_sel ? RGB_ASK : 0);
        ui_disc_set_state(s_talk[k], i == s_sel ? UI_DISC_SELECTED : UI_DISC_NORMAL);
    }

    /* who is being answered, and what it asked (not when queueing: the agent is at work, it asked
     * nothing) */
    const char *q = s_queue ? "" : s->q;
    theme_set_text(s_question, s_question_txt, sizeof s_question_txt, q);
    show(s_question, q[0] != 0);
    int hy = q[0] ? HEAD_Y_Q : HEAD_Y;
    ui_glyph_set_pos(s_head_glyph, UI_CX - HEAD_DX, hy);
    ui_sigil_set_pos(s_head_sigil, UI_CX + HEAD_DX, hy);
    ui_sigil_set_session(s_head_sigil, theme_basename(s->repo), s->sid);
}

/* The item under a screen point, or -1. */
static int hit(int x, int y)
{
    if (!s_replies) return ui_orbit_hit(s_orbit, x, y);
    for (int k = 0; k < 2; k++)
        if (ui_disc_hit(s_talk[k], x, y)) return talk_item(k);
    int dx = x - UI_CX;
    if (dx < -ROW_W / 2 - 20 || dx > ROW_W / 2 + 20) return -1;
    for (int k = -1; k <= 1; k++) {
        int dy = y - (ROW_Y + k * ROW_PITCH);
        if (dy >= -ROW_PITCH / 2 && dy < ROW_PITCH - ROW_PITCH / 2) return row_item(k);
    }
    return -1;
}

/* The orbit's discs reach past the centre zone (r 150), and so do the two discs of the reply list:
 * ui.c lets a touch on one through as a centre touch. */
bool ui_agent_hit(int x, int y)
{ return !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN) && hit(x, y) >= 0; }

/* ───────── both ───────── */

static void draw(const session_t *s)
{
    show(s_menu, !s_replies);
    show(s_reply, s_replies);
    if (s_replies) draw_replies(s);
    else draw_menu(s);
    /* GAUGES.md: model, mode and pull request under the name */
    ui_gauges_agent_update(s, !s_replies);
    /* rule 6: A runs the selection (a reply is sent, anything else is picked), B is the next one */
    ui_key_hint_set(s_hint[0], s_replies && s_sel < s_rows ? ICON_SENT : ICON_CHECK);
    show(s_hint[0], s_n > 0);
    show(s_hint[1], s_n > 1); /* none to step to with one */
}

bool ui_agent_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = session();
    if (!s || s->state == SESS_ENDED) return false;
    const char *base = theme_basename(s->repo);
    char host[24];
    theme_host_prefix(host, sizeof host, s);
    /* in words: the project (host first when remote); Reply says what the list is for */
    snprintf(s_header_txt, sizeof s_header_txt, "%s%s", s_queue || s_replies ? "Reply to " : host,
             base[0] ? base : "(no folder)");
    int was = s_n;
    build_items(s);
    if (s_n != was || s_sel >= s_n) s_sel = 0; /* a list that changed starts at its first item */
    draw(s);
    return true;
}

/* A new list on the screen: the menu, or the replies. */
static void relist(void)
{
    hold_cancel();
    s_n = 0;
    s_sel = s_row_at = 0;
    ui_agent_refresh(s_now);
}

void ui_agent_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_opened_ms = s_last_input = s_now = now;
    s_holding = s_hold_fired = s_ramp_started = false;
    s_nudge_until = 0;
    s_replies = s_queue = false;
    s_header_txt[0] = 0;
    ui_home_rim_mode(true); /* a rim drag dials through the items */
    /* the sessions stay on the ring, settled: the comets cost redraws */
    ring_set_still(ui_home_ring(), true);
    relist();
    const session_t *s = session();
    /* looking at it is seeing it; not for a job, whose ack is Dismiss: the card would go from under
     * the finger */
    if (s && s->state == SESS_DONE && !s->seen && !theme_is_job(s)) g_ui_actions->ack(sid);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    dbg_event("ui", "agent", "\"sid\":%u,\"items\":%d", (unsigned)sid, s_n);
}

/* Straight into the Reply list (from the end of the last message): Back goes to the deck. */
void ui_agent_show_replies(void)
{
    s_replies = true;
    s_queue = false;
    relist();
    dbg_event("ui", "replies", "\"sid\":%u,\"n\":%d", (unsigned)s_sid, model_reply_count());
}

void ui_agent_close(void)
{
    hold_cancel();
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    ring_set_still(ui_home_ring(), !ui_animations_allowed());
}

static void select_item(int i, const char *via, void (*haptic)(void))
{
    if (s_n == 0) return;
    i = ((i % s_n) + s_n) % s_n;
    if (i == s_sel) return;
    hold_cancel(); /* the hold belongs to the item it started on */
    s_sel = i;
    const session_t *s = session();
    if (s) draw(s);
    dbg_event("ui", "item", "\"idx\":%d,\"id\":%d,\"via\":\"%s\"", i, s_items[i].id, via);
    if (haptic) haptic();
}

static void commit(const item_t *it)
{
    const session_t *s = session();
    if (!s) return;
    dbg_event("ui", "commit", "\"sid\":%u,\"id\":%d", (unsigned)s->sid, it->id);
    if (it->id == DICTATE_ID) {
        g_ui_actions->tick();
        ui_go_voice(0, s_queue);
        return;
    }
    if (it->id == KEYS_ID) {
        g_ui_actions->tick();
        ui_go_keys();
        return;
    }
    if (it->id >= REPLY_ID) {
        /* a quick reply: the daemon types it (it never takes text from here) */
        g_ui_actions->type_reply(s->sid, it->id - REPLY_ID, s_queue);
        ui_go_home();
        return;
    }
    switch ((agent_action_t)it->id) {
    case AG_FOCUS:
        g_ui_actions->focus(s->sid);
        if (s->state == SESS_DONE) g_ui_actions->ack(s->sid);
        break;
    case AG_DISMISS:
        g_ui_actions->ack(s->sid);
        g_ui_actions->tick();
        break;
    case AG_SNOOZE: g_ui_actions->snooze(s->sid, snoozed(s) ? 0 : 30); break;
    case AG_NOTIFY: g_ui_actions->notify_done(s->sid, !s->ntf); break;
    case AG_REPLY: /* the same screen, now listing the quick replies */
        s_replies = true;
        relist();
        g_ui_actions->tick();
        dbg_event("ui", "replies", "\"sid\":%u,\"n\":%d", (unsigned)s->sid, model_reply_count());
        return;
    case AG_QUEUE: /* the reply list again, to queue one */
        s_replies = s_queue = true;
        relist();
        g_ui_actions->tick();
        dbg_event("ui", "queue_replies", "\"sid\":%u,\"n\":%d", (unsigned)s->sid, s_n);
        return;
    case AG_UNQUEUE: g_ui_actions->unqueue(s->sid); break;
    /* only ever after the hold */
    case AG_INTERRUPT: g_ui_actions->interrupt(s->sid, s_hold_ms); break;
    /* only ever after the hold */
    case AG_RERUN: g_ui_actions->rerun(s->sid, s_hold_ms); break;
    case AG_READ: /* what the agent said (LAST-MESSAGE.md) */
        g_ui_actions->tick();
        ui_go_read();
        return;
    case AG_GRAPH: /* the agent graph (GRAPH.md §4) */
        g_ui_actions->tick();
        ui_go_graph();
        return;
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
    const item_t *it = s_sel < s_n ? &s_items[s_sel] : NULL;
    switch (a) {
    case NAV_HOLD_PRESS:
        s_hold_fired = false;
        s_drag_acc = 0;
        /* A hold acts on the disc under the finger: only a destructive item arms one, only when the
         * press is on its own (selected) disc, and only once the menu has been on screen a
         * moment. */
        if (it && it->hold && hit(g->x, g->y) == s_sel && s_now - s_opened_ms >= NAV_CARD_ARM_MS) {
            s_holding = true;
            s_ramp_started = false;
            s_hold_t0 = s_now;
        }
        return true;
    case NAV_HOLD_RELEASE: hold_cancel(); return true;
    case NAV_HOLD_LONG: /* a finger held still anywhere off the discs and the rows goes back */
        if (hit(g->x, g->y) < 0) ui_go_back();
        return true;
    case NAV_SCROLL_DRAG: {
        hold_cancel(); /* a moving finger is not holding anything */
        int d = nav_list_drag(&s_drag_acc, g->dy);
        if (d) select_item(s_sel + d, "drag", g_ui_actions->detent);
        return true;
    }
    case NAV_COMMIT:
        if (!it) return false;
        /* too soon after opening */
        if (g->kind == G_TAP && s_now - s_opened_ms < NAV_CARD_ARM_MS) return true;
        if (g->kind == G_TAP) { /* the disc you touch is the one you get (UI-V3 §3.3) */
            int i = hit(g->x, g->y);
            if (i >= 0 && i != s_sel) {
                select_item(i, "tap", g_ui_actions->tick);
                it = &s_items[s_sel];
            }
        }
        if (it->hold) {
            /* a tap never commits a destructive item: a sign says to hold, and the ring to fill
             * shows */
            ui_toast(it->id == AG_RERUN ? "Hold to run again" : "Hold to stop", it->rgb);
            ui_orbit_set_hold(s_orbit, NUDGE_PM);
            s_nudge_until = s_now + NUDGE_MS;
            g_ui_actions->reject();
            return true;
        }
        commit(it);
        return true;
    case NAV_SCRUB: {
        /* a finger that slides along the bezel is not holding the disc it started on */
        hold_cancel();
        int d = nav_list_scrub(&s_scrub_last, g->kind, g->x);
        if (d) select_item(s_sel + d, "scrub", g_ui_actions->detent);
        return true;
    }
    case NAV_NEXT: select_item(s_sel + 1, "btn", g_ui_actions->tick); return true;
    case NAV_PREV: select_item(s_sel - 1, "btn", g_ui_actions->tick); return true;
    default: return false;
    }
}

/* Returns true when the screen wants to close (timeout, or its session went away). */
bool ui_agent_tick(uint32_t now)
{
    s_now = now;
    if (!session()) return true;
    if (s_nudge_until && !s_ramp_started && (int32_t)(now - s_nudge_until) >= 0) {
        s_nudge_until = 0;
        ui_orbit_set_hold(s_orbit, 0);
    }
    if (s_holding && !s_hold_fired) {
        const item_t *it = s_sel < s_n ? &s_items[s_sel] : NULL;
        uint32_t el = now - s_hold_t0;
        if (!it || !it->hold) {
            hold_cancel();
        } else {
            if (el >= NAV_HOLD_SHOW_MS) {
                if (!s_ramp_started) {
                    s_ramp_started = true;
                    s_nudge_until = 0; /* the ring is the hold's now */
                    g_ui_actions->hold_start();
                }
                /* the disc's own ring, and the bezel: the finger covers the disc */
                ring_set_progress(ui_home_ring(), (int)(el * 1000 / NAV_HOLD_MS), it->rgb);
                ui_orbit_set_hold(s_orbit, (int)(el * 1000 / NAV_HOLD_MS));
            }
            if (el >= NAV_HOLD_MS) {
                s_hold_ms = el;
                s_hold_fired = true;
                s_holding = false;
                s_ramp_started = false;
                ring_set_progress(ui_home_ring(), 0, it->rgb);
                ui_orbit_set_hold(s_orbit, 0);
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
    if (!s_replies && s_queued_txt[0]) ui_report_add(r, "%s", s_queued_txt);
    if (s_replies && s_question_txt[0]) ui_report_add(r, "%s", s_question_txt);
    for (int k = 0; k < s_n && k < 5; k++)
        ui_report_add(r, "%s%s", k == 0 ? "> " : "", s_labels[(s_sel + k) % s_n]);
    ui_report_add(r, "%d items", s_n);
}

/* A full-size, see-through parent: a view of the screen, or the place of a part that draws
 * itself. */
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* A block of centred text `lines` rows high about (UI_CX, top + ...): content (rule 1). */
static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, int top, int w, int lines)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(f));
    lv_obj_set_pos(l, UI_CX - w / 2, top);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *row(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, int cy, int w)
{ return text(parent, f, c, cy - lv_font_get_line_height(f) / 2, w, 1); }

lv_obj_t *ui_agent_init(lv_obj_t *screen)
{
    s_root = box(screen, 0, 0, UI_SIZE, UI_SIZE);

    /* the menu: the orbit, its subject, and what belongs to the session under it */
    s_menu = box(s_root, 0, 0, UI_SIZE, UI_SIZE);
    s_orbit = ui_orbit_create(s_menu);
    ui_orbit_set_arch(s_orbit, ARCH_SPAN, ARCH_PITCH);
    s_sigil = ui_sigil_create(s_menu, UI_CX, SIGIL_Y, SIGIL_R);
    s_name = row(s_menu, FONT_HEAD, COL_TEXT, NAME_Y, NAME_W);
    s_queued = row(s_menu, FONT_BODY, COL_TEXT, QUEUED_Y, QUEUED_W);
    lv_obj_add_flag(s_queued, LV_OBJ_FLAG_HIDDEN);

    /* the gauges' readout places itself (at GAUGES_OWN_Y of its parent, centred): it gets a window
     * READOUT_W wide under the name, and a line of text that is wider is cut to it with dots */
    s_readout_box =
        box(s_root, UI_CX - READOUT_W / 2, READOUT_Y - READOUT_H / 2, READOUT_W, READOUT_H);
    lv_obj_t *at =
        box(s_readout_box, READOUT_W / 2 - UI_CX, READOUT_H / 2 - GAUGES_OWN_Y, UI_SIZE, UI_SIZE);
    ui_gauges_agent_create(at);
    lv_obj_t *part = lv_obj_get_child_count(at) == 1 ? lv_obj_get_child(at, 0) : NULL;
    if (part && lv_obj_check_type(part, &lv_label_class) &&
        lv_obj_get_style_width(part, 0) > READOUT_W) {
        lv_obj_set_width(part, READOUT_W);
        lv_obj_set_x(part, UI_CX - READOUT_W / 2);
    }

    /* the reply list */
    s_reply = box(s_root, 0, 0, UI_SIZE, UI_SIZE);
    s_head_glyph = ui_glyph_create(s_reply, FONT_ICON_M, UI_CX - HEAD_DX, HEAD_Y);
    ui_glyph_set(s_head_glyph, ICON_REPLY, lv_color_hex(RGB_ASK));
    s_head_sigil = ui_sigil_create(s_reply, UI_CX + HEAD_DX, HEAD_Y, HEAD_SIGIL_R);
    /* the agent's closing question */
    s_question = text(s_reply, FONT_BODY_STRONG, COL_TEXT, QUESTION_Y, QUESTION_W, 2);
    lv_obj_add_flag(s_question, LV_OBJ_FLAG_HIDDEN);
    for (int k = 0; k < 3; k++)
        s_row[k] = row(s_reply, k == 1 ? FONT_HEAD : FONT_BODY, k == 1 ? COL_TEXT : COL_TEXT3,
                       ROW_Y + (k - 1) * ROW_PITCH, ROW_W);
    for (int i = 0; i < 2; i++) {
        /* the selected row sits between two arcs, at 9 and 3 o'clock of its circle */
        s_focus[i] = ui_arc_create(s_reply, UI_CX, ROW_Y, FOCUS_R, FOCUS_W);
        ui_arc_set_span(s_focus[i], (i == 0 ? 270 : 90) - FOCUS_DEG / 2, FOCUS_DEG);
        ui_arc_set(s_focus[i], 1000, lv_color_hex(RGB_ASK), lv_color_black());
    }
    s_pips = ui_pips_create(s_reply, UI_CX, PIPS_Y);
    for (int k = 0; k < 2; k++) {
        s_talk[k] = ui_disc_create(s_reply, UI_CX, TALK_Y, TALK_R);
        ui_disc_set_glyph(s_talk[k], k == 0 ? ICON_MIC : ICON_KEYBOARD);
        lv_obj_add_flag(s_talk[k], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_reply, LV_OBJ_FLAG_HIDDEN);

    s_hint[0] = ui_key_hint_create(s_root, UI_KEY_A);
    s_hint[1] = ui_key_hint_create(s_root, UI_KEY_B);
    ui_key_hint_set(s_hint[1], ICON_NEXT);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
