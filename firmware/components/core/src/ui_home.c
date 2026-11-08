/* The HOME deck (docs/design/UI-V2.md §4.1): one session card in the centre, a thin pinned index ring
 * at the rim. Gestures and buttons are mapped in ui_home_gesture(). Until Phase 2 (the per-agent
 * wheel) a tap on the centre is still the default action. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "hal.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define CENTRE_R 150 /* a touch inside this radius is a centre touch; outside it is the rim */
#define HOLD_SHOW_MS 250 /* the arc and the haptic ramp only start after this, so a tap never flashes them */
#define CARD_ARM_MS 300  /* a hold only starts once the request has been on screen this long */
#define DENY_MIN_PX 120  /* deny is a deliberate swipe: this long, starting above the centre line */
#define AMBIENT_MS 30000
#define MERGE_AT 9
#define NAG_MS 15000
#define NAG_MAX 3

/* Card rows, top to bottom. Glanceable first: the state word is the largest coloured element and the
 * time in state is demoted below the summary. */
#define AGENT_Y 80    /* 16 px muted: agent · branch */
#define REPO_Y 102    /* 32 px white on one line, or 24 px on two; centred in a row REPO_H tall */
#define REPO_H 64
#define REPO_W 280
#define REPO_PX 32
#define REPO_SMALL_PX 24
#define STATE_Y 168   /* 32 px state colour: glyph + word */
#define SUMMARY_Y 210 /* 20 px muted, two lines: the agent's last summary */
#define SUMMARY_W 300
#define REQ_TITLE_Y 208 /* approval: 20 px title, then 16 px text on two lines, in place of summary + time */
#define REQ_TEXT_Y 234
#define ELAPSED_Y 262 /* 20 px muted: time in this state */
#define HINT_Y 364    /* 16 px: "long swipe down to deny", between the badges and the link row */
#define LINK_Y 392    /* 16 px: link · host · battery, or Offline · stale */

/* Pebble-style key badges beside the physical buttons (angles clockwise from 12 o'clock):
 * KEYA lower-left at 7:30 (225 deg), KEYB lower-right at 4:30 (135 deg). Legends, not touch targets. */
#define BADGE_A_DEG 225
#define BADGE_B_DEG 135
#define BADGE_R 158

typedef struct {
    uint32_t sid;
    uint8_t n_merged; /* > 0: this slot stands for that many Done sessions */
} slot_t;

static lv_obj_t *s_root, *s_ring, *s_centre;
static lv_obj_t *s_agent, *s_repo, *s_state, *s_summary, *s_elapsed, *s_pill, *s_pill_lbl, *s_hint, *s_link;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2]; /* [0] = A, [1] = B */
static lv_obj_t *s_req_title, *s_req_text; /* approval card: what is being approved */

static slot_t s_slots[RING_MAX_SEGMENTS];
static int s_n, s_sel;
static uint32_t s_sel_sid;
static bool s_has_sel;
static bool s_scrubbing, s_freeze_expand;
static uint32_t s_now, s_last_input, s_last_elapsed;
static bool s_ambient;

static bool s_press_centre, s_holding, s_hold_fired, s_ramp_started;
static uint32_t s_hold_t0, s_hold_rid;
static uint32_t s_card_rid, s_card_t0; /* request on screen, and since when */
static int s_rel_y;                    /* where the last touch lifted, for the deny swipe length */

static uint32_t s_nag_rid, s_nag_next;
static int s_nag_n;

static bool s_offline_dim;

static char s_agent_txt[64], s_repo_txt[40], s_state_txt[40], s_summary_txt[88], s_elapsed_txt[16], s_pill_txt[32],
    s_link_txt[64];
static char s_req_title_txt[48], s_req_text_txt[168];

static const session_t *sel_session(void)
{
    return s_has_sel ? model_find(s_sel_sid) : NULL;
}

static const attention_t *approvable(const session_t *s)
{
    if (!s || s->state != SESS_INPUT) return NULL;
    const attention_t *a = model_attention_for_sid(s->sid);
    return a && a->kind == ATT_APPROVAL && (a->acts & (1u << ACT_APPROVE)) && !a->answer_sent ? a : NULL;
}

static ring_kind_t kind_of(const session_t *s)
{
    switch (s->state) {
    case SESS_INPUT: return s->sub == 0 ? RING_PERM : RING_ASK;
    case SESS_ERROR: return RING_ERROR;
    case SESS_DONE: return RING_DONE;
    case SESS_IDLE: return RING_IDLE;
    default: return RING_WORK;
    }
}

static void noted_input(void)
{
    s_last_input = s_now;
    s_nag_n = NAG_MAX;
}

static bool before(const session_t *a, const session_t *b)
{
    return a->t0 != b->t0 ? a->t0 < b->t0 : a->sid < b->sid;
}

/* ───────── slots ───────── */

static void rebuild(void)
{
    const session_t *l[MODEL_MAX_SESSIONS];
    uint8_t idx[MODEL_MAX_SESSIONS];
    int n = 0;
    size_t c = model_sorted(idx);
    for (size_t i = 0; i < c && n < RING_MAX_SEGMENTS; i++) {
        const session_t *s = model_at(idx[i]);
        if (s && s->state != SESS_ENDED) l[n++] = s;
    }
    if (!s_has_sel || !model_find(s_sel_sid)) {
        s_has_sel = n > 0;
        if (n) {
            s_sel_sid = l[0]->sid; /* the most urgent one */
            dbg_event("ui", "select", "\"sid\":%u,\"idx\":-1,\"n\":%d,\"via\":\"auto\"", (unsigned)s_sel_sid, n);
        }
    }
    /* Pinned order: a session keeps its place however its state changes. */
    for (int i = 1; i < n; i++) {
        const session_t *k = l[i];
        int j = i - 1;
        while (j >= 0 && before(k, l[j])) {
            l[j + 1] = l[j];
            j--;
        }
        l[j + 1] = k;
    }
    int ndone = 0;
    for (int i = 0; i < n; i++) ndone += l[i]->state == SESS_DONE;
    const session_t *sel = sel_session();
    bool merge = n >= MERGE_AT && ndone >= 2 && !(sel && sel->state == SESS_DONE) && !s_freeze_expand;

    int old_n = s_n, m = 0;
    bool placed = false;
    for (int i = 0; i < n; i++) {
        if (merge && l[i]->state == SESS_DONE) {
            if (placed) continue;
            placed = true;
            s_slots[m++] = (slot_t){l[i]->sid, (uint8_t)ndone};
            continue;
        }
        s_slots[m++] = (slot_t){l[i]->sid, 0};
    }
    s_n = m;
    s_sel = 0;
    for (int i = 0; i < s_n; i++)
        if (s_slots[i].sid == s_sel_sid) s_sel = i;
    if (s_n != old_n && g_ui_actions->sectors) g_ui_actions->sectors(s_n);

    ring_seg_t segs[RING_MAX_SEGMENTS];
    for (int i = 0; i < s_n; i++) {
        const session_t *s = model_find(s_slots[i].sid);
        segs[i].id = s_slots[i].sid | (s_slots[i].n_merged ? 0x80000000u : 0);
        segs[i].rgb = theme_session_rgb(s);
        segs[i].kind = (uint8_t)kind_of(s);
        segs[i].selected = i == s_sel && s_n > 0;
        segs[i].dim = s->state == SESS_DONE && (s->seen || s_slots[i].n_merged);
    }
    ring_set(s_ring, segs, s_n, s_now);
}

static void update_centre(void);

/* Returns true when the number of slots changed (a merged Done group expanded). */
static bool select_idx(int i, const char *via, void (*haptic)(void))
{
    if (i < 0 || i >= s_n) return false;
    uint32_t sid = s_slots[i].sid;
    bool changed = !s_has_sel || sid != s_sel_sid;
    int old_n = s_n;
    s_sel_sid = sid;
    s_has_sel = true;
    if (s_slots[i].n_merged && s_scrubbing) s_freeze_expand = true;
    rebuild();
    update_centre();
    if (changed) {
        dbg_event("ui", "select", "\"sid\":%u,\"idx\":%d,\"n\":%d,\"via\":\"%s\"", (unsigned)sid, s_sel, s_n, via);
        if (haptic) haptic();
    }
    return s_n != old_n;
}

void ui_home_select_sid(uint32_t sid, const char *via)
{
    if (!model_find(sid)) return;
    noted_input();
    for (int i = 0; i < s_n; i++)
        if (s_slots[i].sid == sid) {
            select_idx(i, via, NULL);
            return;
        }
    if (s_has_sel && s_sel_sid == sid) return;
    s_sel_sid = sid; /* hidden inside a merged Done group: selecting it expands the group */
    s_has_sel = true;
    rebuild();
    update_centre();
    dbg_event("ui", "select", "\"sid\":%u,\"idx\":%d,\"n\":%d,\"via\":\"%s\"", (unsigned)sid, s_sel, s_n, via);
}

/* ───────── centre ───────── */

static void set_txt(lv_obj_t *l, char *cache, size_t cap, const char *t)
{
    if (strcmp(cache, t) == 0) return;
    snprintf(cache, cap, "%s", t);
    lv_label_set_text(l, t);
}

/* Fixed height of `lines` rows: LONG_MODE_DOTS only truncates when the height is fixed, otherwise a
 * long text wraps onto the next row and collides with it. */
static void place_lines(lv_obj_t *l, int y, int w, int lines)
{
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, UI_CX - w / 2, y);
}

static void place(lv_obj_t *l, int y, int w) { place_lines(l, y, w, 1); }

/* The folder name is one 32 px line when it fits, otherwise two 24 px lines, centred in the same row. */
static void set_repo(const char *t)
{
    if (strcmp(s_repo_txt, t) == 0) return;
    const lv_font_t *big = theme_font(REPO_PX), *f = big;
    int lines = 1;
    lv_point_t sz;
    lv_text_get_size(&sz, t, big, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x > REPO_W) {
        f = theme_font(REPO_SMALL_PX);
        lines = 2;
    }
    lv_obj_set_style_text_font(s_repo, f, 0);
    int h = lines * lv_font_get_line_height(f);
    place_lines(s_repo, REPO_Y + (REPO_H - h) / 2, REPO_W, lines);
    set_txt(s_repo, s_repo_txt, sizeof s_repo_txt, t);
}

static bool offline(void) { return !ui_link_connected(); }

/* Link row: transport · host · battery while connected; otherwise how stale the snapshot is. */
static void link_text(char *dst, size_t cap, uint32_t *rgb)
{
    int k = ui_link_transport();
    const char *sym = k == HAL_LINK_USB ? LV_SYMBOL_USB : k == HAL_LINK_BLE ? LV_SYMBOL_BLUETOOTH : LV_SYMBOL_WIFI;
    const char *name = k == HAL_LINK_USB ? "USB" : k == HAL_LINK_BLE ? "BLE" : "PC";
    uint32_t down = ui_link_down_ms();
    if (!ui_link_connected() && down && model_count() > 0) {
        char age[16];
        theme_elapsed(age, sizeof age, down / 1000);
        snprintf(dst, cap, "%s Offline %s stale %s", LV_SYMBOL_WARNING, theme_sep(), age);
        *rgb = RGB_AMBER;
    } else if (k < 0) {
        snprintf(dst, cap, "%s Offline", LV_SYMBOL_WARNING);
        *rgb = RGB_AMBER;
    } else if (!ui_link_connected()) {
        snprintf(dst, cap, "%s %s...", sym, name);
        *rgb = RGB_AMBER;
    } else {
        const char *host = model_host();
        hal_battery_t b = hal_battery_read();
        char batt[16] = "";
        if (b.percent <= 100) snprintf(batt, sizeof batt, "%s%s%u%%", theme_sep(), b.charging ? LV_SYMBOL_CHARGE " " : "", b.percent);
        if (host[0]) snprintf(dst, cap, "%s %s%s%.12s%s", sym, name, theme_sep(), host, batt);
        else snprintf(dst, cap, "%s %s%s", sym, name, batt);
        *rgb = RGB_SUCCESS;
    }
}

/* Time in the current state (UI-V2 §10). While offline the clock is frozen at the moment the link
 * dropped, so a stale card does not keep counting as if it were live. */
static void update_elapsed(const session_t *s)
{
    uint32_t now = theme_now_unix() - ui_link_down_ms() / 1000, from = s->tst ? s->tst : s->t0;
    char buf[16] = "";
    if (from && now >= from) theme_elapsed_long(buf, sizeof buf, now - from);
    set_txt(s_elapsed, s_elapsed_txt, sizeof s_elapsed_txt, buf);
}

/* One opacity for the card: ambient wins, then offline, else full. */
static void apply_dim(void)
{
    lv_obj_set_style_opa(s_centre, s_ambient ? LV_OPA_30 : s_offline_dim ? LV_OPA_50 : LV_OPA_COVER, 0);
}

static void set_badges(bool show_a, bool show_b, uint32_t rgb_a)
{
    bool off = offline();
    lv_obj_set_style_text_color(s_word[0], off ? COL_MUTED : lv_color_hex(rgb_a), 0);
    lv_obj_set_style_text_color(s_word[1], COL_MUTED, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_text_color(s_key[i], off ? COL_MUTED : COL_TEXT, 0);
        if (i == 0 ? show_a : show_b) lv_obj_remove_flag(s_badge[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_badge[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_centre(void)
{
    char buf[64];
    uint32_t link_rgb;
    link_text(buf, sizeof buf, &link_rgb);
    set_txt(s_link, s_link_txt, sizeof s_link_txt, buf);
    lv_obj_set_style_text_color(s_link, lv_color_hex(link_rgb), 0);

    s_offline_dim = offline() && model_count() > 0;
    apply_dim();

    const session_t *s = sel_session();
    if (!s || s_n == 0) {
        set_txt(s_agent, s_agent_txt, sizeof s_agent_txt, "");
        set_repo("No sessions");
        lv_obj_set_style_text_color(s_repo, COL_TEXT, 0);
        set_txt(s_state, s_state_txt, sizeof s_state_txt, "");
        set_txt(s_summary, s_summary_txt, sizeof s_summary_txt, "Waiting for an agent");
        set_txt(s_elapsed, s_elapsed_txt, sizeof s_elapsed_txt, "");
        lv_obj_add_flag(s_req_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_req_text, LV_OBJ_FLAG_HIDDEN);
        s_card_rid = 0;
        lv_obj_add_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_hint, "");
        set_badges(false, false, 0);
        return;
    }
    uint32_t rgb = theme_session_rgb(s);
    const slot_t *sl = &s_slots[s_sel];
    if (sl->n_merged) {
        snprintf(buf, sizeof buf, "%d sessions", sl->n_merged);
        set_txt(s_agent, s_agent_txt, sizeof s_agent_txt, buf);
        set_repo("Done");
    } else {
        /* agent · branch: two sessions on the same folder stay distinguishable */
        if (s->branch[0]) snprintf(buf, sizeof buf, "%s%s%s", theme_agent_name(s->agent), theme_sep(), s->branch);
        else snprintf(buf, sizeof buf, "%s", theme_agent_name(s->agent));
        set_txt(s_agent, s_agent_txt, sizeof s_agent_txt, buf);
        const char *base = theme_basename(s->repo);
        set_repo(base[0] ? base : "(no folder)");
    }
    lv_obj_set_style_text_color(s_repo, COL_TEXT, 0);
    snprintf(buf, sizeof buf, "%s  %s", theme_state_glyph(s), theme_state_word(s));
    set_txt(s_state, s_state_txt, sizeof s_state_txt, buf);
    lv_obj_set_style_text_color(s_state, lv_color_hex(rgb), 0);
    update_elapsed(s);

    const attention_t *req = approvable(s);
    bool perm = req != NULL;
    if (perm) {
        /* Show what is being approved in place of the summary and the time. Fall back to the tool
         * name when the daemon sent no title (privacy: meta). */
        if (req->rid != s_card_rid) {
            s_card_rid = req->rid;
            s_card_t0 = s_now;
        }
        set_txt(s_req_title, s_req_title_txt, sizeof s_req_title_txt,
                req->title[0] ? req->title : s->tool[0] ? s->tool : "Permission request");
        set_txt(s_req_text, s_req_text_txt, sizeof s_req_text_txt, req->text);
        set_txt(s_summary, s_summary_txt, sizeof s_summary_txt, "");
        lv_obj_add_flag(s_elapsed, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_req_title, LV_OBJ_FLAG_HIDDEN);
        if (req->text[0]) lv_obj_remove_flag(s_req_text, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_req_text, LV_OBJ_FLAG_HIDDEN);
    } else {
        s_card_rid = 0;
        set_txt(s_summary, s_summary_txt, sizeof s_summary_txt, sl->n_merged ? "" : s->summary);
        lv_obj_remove_flag(s_elapsed, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_req_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_req_text, LV_OBJ_FLAG_HIDDEN);
    }
    const char *pill = perm ? "HOLD " LV_SYMBOL_OK " APPROVE" : s->state == SESS_DONE ? "FOCUS + ACK" : "FOCUS";
    set_txt(s_pill_lbl, s_pill_txt, sizeof s_pill_txt, pill);
    lv_color_t pill_col = offline() ? COL_MUTED : lv_color_hex(rgb);
    lv_obj_set_style_text_color(s_pill_lbl, pill_col, 0);
    lv_obj_set_style_bg_color(s_pill, pill_col, 0);
    lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_pill, LV_ALIGN_TOP_MID, 0, UI_ACTION_Y);
    lv_label_set_text(s_hint, perm ? "long swipe down to deny" : "");
    set_badges(true, s_n > 1, rgb);
}

/* ───────── actions ───────── */

static void act_focus_selected(void)
{
    const session_t *s = sel_session();
    if (!s) {
        g_ui_actions->reject();
        return;
    }
    g_ui_actions->focus(s->sid);
    if (s->state == SESS_DONE) g_ui_actions->ack(s->sid); /* focusing a finished session also acknowledges it */
}

static void act_ack_selected(void)
{
    const session_t *s = sel_session();
    if (s) g_ui_actions->ack(s->sid);
}

static void next_attention(void)
{
    const session_t *sel = sel_session();
    for (int cls = 0; cls < 3; cls++) {
        for (int k = 1; k < s_n; k++) {
            int i = (s_sel + k) % s_n;
            const session_t *s = model_find(s_slots[i].sid);
            if (!s || s_slots[i].n_merged) continue;
            bool hit = cls == 0 ? s->state == SESS_INPUT : cls == 1 ? s->state == SESS_ERROR : (s->state == SESS_DONE && !s->seen);
            if (hit && s != sel) {
                select_idx(i, "attention", g_ui_actions->tick);
                return;
            }
        }
    }
    ui_toast("Nothing else needs you", RGB_ACCENT);
    g_ui_actions->reject();
}

static void step_selection(int dir, const char *via)
{
    if (s_n < 2) {
        g_ui_actions->reject();
        return;
    }
    select_idx((s_sel + dir + s_n) % s_n, via, g_ui_actions->tick);
}

static void hold_cancel(void)
{
    if (!s_holding) return;
    s_holding = false;
    if (s_ramp_started) g_ui_actions->hold_stop();
    s_ramp_started = false;
    ring_set_progress(s_ring, 0, 0xffffff);
}

static bool centre_hit(int x, int y)
{
    int dx = x - UI_CX, dy = y - UI_CY;
    return dx * dx + dy * dy < CENTRE_R * CENTRE_R;
}

bool ui_home_gesture(const gesture_evt_t *g)
{
    if (g->kind != G_LIFT && g->kind != G_SHAKE) noted_input();
    const session_t *s = sel_session();
    switch (g->kind) {
    case G_PRESS:
        s_press_centre = centre_hit(g->x, g->y);
        s_hold_fired = false;
        if (s_press_centre && approvable(s) && s_card_rid == model_attention_for_sid(s->sid)->rid &&
            s_now - s_card_t0 >= CARD_ARM_MS) {
            s_holding = true;
            s_ramp_started = false;
            s_hold_t0 = s_now;
            s_hold_rid = model_attention_for_sid(s->sid)->rid;
        }
        return true;
    case G_RELEASE:
        s_rel_y = g->y;
        hold_cancel();
        return true;
    case G_DOUBLE_TAP:
    case G_TAP:
        if (centre_hit(g->x, g->y)) act_focus_selected();
        else {
            int i = ring_hit(s_ring, g->x, g->y);
            if (i >= 0) select_idx(i, "tap", g_ui_actions->tick);
        }
        return true;
    case G_LONG_PRESS: {
        if (!s_press_centre || s_hold_fired || !s) return false;
        const attention_t *a = approvable(s);
        if (a) { /* an injected long press that already covers the hold time approves directly */
            if (g->dur_ms >= UI_HOLD_MS && s_card_rid == a->rid && s_now - s_card_t0 >= CARD_ARM_MS) {
                hold_cancel();
                s_hold_fired = true;
                g_ui_actions->approve(s->sid, a->rid, g->dur_ms);
            }
            return true;
        }
        if (s->state == SESS_DONE || s->state == SESS_ERROR) act_ack_selected();
        else act_focus_selected(); /* Working, Needs input, Idle: long press = Focus */
        return true;
    }
    case G_SWIPE_DOWN: {
        const attention_t *a = approvable(s);
        if (!s_press_centre || !a || !(a->acts & (1u << ACT_DENY))) return false;
        /* g->y is where the swipe started; G_RELEASE (sent just before) recorded where it ended. */
        if (g->y >= UI_CY || s_rel_y - g->y < DENY_MIN_PX) return true; /* too short: ignore, but consume */
        hold_cancel();
        g_ui_actions->deny(s->sid, a->rid);
        return true;
    }
    case G_SCRUB_START:
        s_scrubbing = true;
        s_freeze_expand = false;
        /* fall through */
    case G_SCRUB:
        if (select_idx(g->x, "scrub", g_ui_actions->detent) && s_n > 0) {
            int i = g->y * s_n / 360;
            select_idx(i < s_n ? i : s_n - 1, "scrub", NULL);
        }
        return true;
    case G_SCRUB_END:
        s_scrubbing = false;
        s_freeze_expand = false;
        rebuild();
        update_centre();
        return true;
    case G_BTN_A:
        act_focus_selected();
        return true;
    case G_BTN_A_DOUBLE:
        next_attention();
        return true;
    case G_BTN_B:
        step_selection(1, "btn");
        return true;
    case G_BTN_B_LONG:
        step_selection(-1, "btn");
        return true;
    default:
        return false; /* A long, chord, swipes, lift, shake: no action */
    }
}

/* ───────── per-step ───────── */

static bool needs_you(void)
{
    model_counts_t c = model_counts();
    return c.waiting > 0 || c.error > 0;
}

void ui_home_tick(uint32_t now)
{
    s_now = now;
    if (!s_last_input) s_last_input = now;
    if (ui_animations_allowed()) ring_tick(s_ring, now);

    if (s_holding && !s_hold_fired) {
        const session_t *s = sel_session();
        const attention_t *a = approvable(s);
        if (!a || a->rid != s_hold_rid) {
            hold_cancel();
        } else {
            uint32_t el = now - s_hold_t0;
            if (el >= HOLD_SHOW_MS) {
                if (!s_ramp_started) {
                    s_ramp_started = true;
                    g_ui_actions->hold_start();
                }
                ring_set_progress(s_ring, (int)(el * 1000 / UI_HOLD_MS), theme_session_rgb(s)); /* the item's colour */
            }
            if (el >= UI_HOLD_MS) {
                s_hold_fired = true;
                s_holding = false;
                s_ramp_started = false;
                ring_set_progress(s_ring, 0, theme_session_rgb(s));
                g_ui_actions->approve(s->sid, a->rid, el);
            }
        }
    }

    /* Unanswered permission requests nag a few times, until the user touches anything. */
    const attention_t *first = model_attention_first();
    if (first && first->kind == ATT_APPROVAL && !first->answer_sent) {
        if (first->rid != s_nag_rid) {
            s_nag_rid = first->rid;
            s_nag_n = 0;
            s_nag_next = now + NAG_MS;
        } else if (s_nag_n < NAG_MAX && (int32_t)(now - s_nag_next) >= 0) {
            s_nag_n++;
            s_nag_next = now + NAG_MS;
            g_ui_actions->nag();
        }
    }

    bool amb = now - s_last_input > AMBIENT_MS && !needs_you() && !s_holding;
    if (amb != s_ambient) {
        s_ambient = amb;
        ring_set_ambient(s_ring, amb);
        apply_dim();
    }

    if (now - s_last_elapsed >= 500) {
        s_last_elapsed = now;
        const session_t *s = sel_session();
        if (s && s_n > 0 && !s_slots[s_sel].n_merged) update_elapsed(s);
        if (ui_link_down_ms()) { /* the stale age ticks while offline */
            char buf[64];
            uint32_t link_rgb;
            link_text(buf, sizeof buf, &link_rgb);
            set_txt(s_link, s_link_txt, sizeof s_link_txt, buf);
        }
    }
}

void ui_home_refresh(uint32_t now)
{
    s_now = now;
    rebuild();
    update_centre();
}

void ui_home_animations_changed(void)
{
    if (s_ring) ring_set_still(s_ring, !ui_animations_allowed());
}

void ui_home_report(ui_report_t *r)
{
    const session_t *s = sel_session();
    const attention_t *a = s ? model_attention_for_sid(s->sid) : NULL;
    snprintf(r->screen, sizeof r->screen, "%s", a && !a->answer_sent ? "attention" : "ring");
    r->has_sel_sid = s != NULL;
    r->sel_sid = s_sel_sid;
    if (a) {
        r->has_rid = true;
        r->rid = a->rid;
    }
    if (s_repo_txt[0]) ui_report_add(r, "%s", s_repo_txt);
    if (s_state_txt[0] && s) ui_report_add(r, "%s", theme_state_word(s));
    if (s_summary_txt[0]) ui_report_add(r, "%s", s_summary_txt);
    if (!lv_obj_has_flag(s_req_title, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_req_title_txt);
    if (!lv_obj_has_flag(s_req_text, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_req_text_txt);
    if (s && !lv_obj_has_flag(s_pill, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_pill_txt);
    if (s_link_txt[0]) ui_report_add(r, "%s", s_link_txt);
    ui_report_add(r, "%d segments", s_n);
}

/* ───────── construction ───────── */

lv_obj_t *ui_home_init(void)
{
    s_root = ui_new_screen();
    s_ring = ring_create(s_root);
    s_centre = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_centre);
    lv_obj_set_size(s_centre, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_centre, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_agent = theme_label(s_centre, 16, COL_MUTED);
    place(s_agent, AGENT_Y, 280);
    s_repo = theme_label(s_centre, REPO_PX, COL_TEXT);
    place(s_repo, REPO_Y + (REPO_H - lv_font_get_line_height(theme_font(REPO_PX))) / 2, REPO_W);
    s_state = theme_label(s_centre, 32, COL_MUTED);
    place(s_state, STATE_Y, 300);
    s_summary = theme_label(s_centre, 20, COL_MUTED);
    place_lines(s_summary, SUMMARY_Y, SUMMARY_W, 2);
    s_elapsed = theme_label(s_centre, 20, COL_MUTED);
    place(s_elapsed, ELAPSED_Y, 250);
    s_req_title = theme_label(s_centre, 20, COL_TEXT);
    place(s_req_title, REQ_TITLE_Y, 300);
    s_req_text = theme_label(s_centre, 16, COL_MUTED);
    place_lines(s_req_text, REQ_TEXT_Y, 300, 2);
    lv_obj_add_flag(s_req_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_req_text, LV_OBJ_FLAG_HIDDEN);

    s_pill = lv_obj_create(s_centre);
    lv_obj_remove_style_all(s_pill);
    lv_obj_set_size(s_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_pill, UI_PILL_TINT_OPA, 0); /* a filled tint, no border */
    lv_obj_set_style_radius(s_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(s_pill, 18, 0);
    lv_obj_set_style_pad_ver(s_pill, 8, 0);
    lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_pill_lbl = theme_label(s_pill, 20, COL_TEXT);
    lv_label_set_long_mode(s_pill_lbl, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(s_pill_lbl, LV_SIZE_CONTENT);

    s_hint = theme_label(s_centre, 16, COL_MUTED);
    place(s_hint, HINT_Y, 300);
    s_link = theme_label(s_centre, 16, COL_MUTED);
    place(s_link, LINK_Y, 240);

    /* Key badges: a key cap with the button's letter, and the word for what it does. A reads
     * "[A] FOCUS" with the cap on the button side; B reads "NEXT [B]". */
    static const int deg[2] = {BADGE_A_DEG, BADGE_B_DEG};
    static const char *const words[2] = {"FOCUS", "NEXT"};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = s_badge[i] = lv_obj_create(s_centre);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 6, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *cap = lv_obj_create(b);
        lv_obj_remove_style_all(cap);
        lv_obj_set_size(cap, UI_KEYCAP_PX, UI_KEYCAP_PX);
        lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(cap, COL_SURFACE2, 0);
        lv_obj_set_style_border_width(cap, 1, 0);
        lv_obj_set_style_border_color(cap, COL_BORDER, 0);
        lv_obj_remove_flag(cap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_key[i] = theme_label(cap, 16, COL_TEXT);
        lv_label_set_text(s_key[i], i == 0 ? "A" : "B");
        lv_obj_center(s_key[i]);
        s_word[i] = theme_label(b, 16, i == 0 ? COL_TEXT : COL_MUTED);
        lv_label_set_text(s_word[i], words[i]);
        if (i == 1) lv_obj_move_to_index(cap, 1); /* word first, cap on the button side */
        float rad = (float)deg[i] * 3.14159265f / 180.0f;
        lv_obj_update_layout(b);
        int w = lv_obj_get_width(b), h = lv_obj_get_height(b);
        lv_obj_set_pos(b, UI_CX + (int)((float)BADGE_R * sinf(rad)) - w / 2,
                       UI_CY - (int)((float)BADGE_R * cosf(rad)) - h / 2);
    }
    lv_obj_t *all[] = {s_agent, s_repo, s_state, s_summary, s_elapsed, s_req_title, s_req_text, s_pill_lbl, s_hint, s_link};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) lv_label_set_text(all[i], "");
    ui_home_animations_changed();
    return s_root;
}
