/* The Ring screen: one arc segment per session around the bezel, the selected session in the
 * centre. Gestures and buttons are mapped in ui_home_gesture(); see docs/design/UI-RING.md. */
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
#define AMBIENT_MS 30000
#define MERGE_AT 9
#define NAG_MS 15000
#define NAG_MAX 3

/* Edge hints sit just inside the ring next to the physical buttons (angles clockwise from 12 o'clock):
 * KEYA lower-left at 7:30 (225 deg), KEYB lower-right at 4:30 (135 deg). A small dot points at each button. */
#define HINT_A_DEG 225
#define HINT_B_DEG 135
#define HINT_R 152
#define HINT_DOT_R 177

typedef struct {
    uint32_t sid;
    uint8_t n_merged; /* > 0: this slot stands for that many Done sessions */
} slot_t;

static lv_obj_t *s_root, *s_ring, *s_centre;
static lv_obj_t *s_agent, *s_repo, *s_state, *s_elapsed, *s_pill, *s_pill_lbl, *s_hint, *s_link;
static lv_obj_t *s_hint_a, *s_hint_b, *s_dot[2];

static slot_t s_slots[RING_MAX_SEGMENTS];
static int s_n, s_sel;
static uint32_t s_sel_sid;
static bool s_has_sel;
static bool s_scrubbing, s_freeze_expand;
static uint32_t s_now, s_last_input, s_last_elapsed;
static bool s_ambient;

static bool s_press_centre, s_holding, s_hold_fired, s_ramp_started;
static uint32_t s_hold_t0, s_hold_rid;

static uint32_t s_nag_rid, s_nag_next;
static int s_nag_n;

static char s_agent_txt[24], s_repo_txt[40], s_state_txt[40], s_elapsed_txt[16], s_pill_txt[32], s_link_txt[48];

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

static void place(lv_obj_t *l, int y, int w)
{
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, UI_CX - w / 2, y);
}

static const char *state_icon(const session_t *s)
{
    switch (s->state) {
    case SESS_INPUT: return s->sub == 0 ? LV_SYMBOL_STOP : "?";
    case SESS_ERROR: return LV_SYMBOL_WARNING;
    case SESS_DONE: return LV_SYMBOL_OK;
    case SESS_IDLE: return LV_SYMBOL_PAUSE;
    default: return LV_SYMBOL_REFRESH;
    }
}

static void link_text(char *dst, size_t cap, uint32_t *rgb)
{
    int k = ui_link_transport();
    const char *sym = k == HAL_LINK_USB ? LV_SYMBOL_USB : k == HAL_LINK_BLE ? LV_SYMBOL_BLUETOOTH : LV_SYMBOL_WIFI;
    const char *name = k == HAL_LINK_USB ? "USB" : k == HAL_LINK_BLE ? "BLE" : "PC";
    if (k < 0) {
        snprintf(dst, cap, "%s Offline", LV_SYMBOL_WARNING);
        *rgb = RGB_AMBER;
    } else if (!ui_link_connected()) {
        snprintf(dst, cap, "%s %s...", sym, name);
        *rgb = RGB_AMBER;
    } else {
        const char *host = model_host();
        if (host[0]) snprintf(dst, cap, "%s %s %s %.12s", sym, name, theme_sep(), host);
        else snprintf(dst, cap, "%s %s", sym, name);
        *rgb = RGB_SUCCESS;
    }
}

static void update_elapsed(const session_t *s)
{
    uint32_t now = theme_now_unix(), from = s->state == SESS_WORK || s->state == SESS_START ? s->t0 : s->tst;
    if (!from) from = s->t0;
    char buf[16] = "";
    if (from && now >= from) theme_elapsed_long(buf, sizeof buf, now - from);
    set_txt(s_elapsed, s_elapsed_txt, sizeof s_elapsed_txt, buf);
}

static void update_centre(void)
{
    char buf[64];
    uint32_t link_rgb;
    link_text(buf, sizeof buf, &link_rgb);
    set_txt(s_link, s_link_txt, sizeof s_link_txt, buf);
    lv_obj_set_style_text_color(s_link, lv_color_hex(link_rgb), 0);

    const session_t *s = sel_session();
    if (!s || s_n == 0) {
        set_txt(s_agent, s_agent_txt, sizeof s_agent_txt, "");
        set_txt(s_repo, s_repo_txt, sizeof s_repo_txt, "No sessions");
        lv_obj_set_style_text_color(s_repo, COL_TEXT, 0);
        set_txt(s_state, s_state_txt, sizeof s_state_txt, "Waiting for an agent");
        lv_obj_set_style_text_color(s_state, COL_MUTED, 0);
        set_txt(s_elapsed, s_elapsed_txt, sizeof s_elapsed_txt, "");
        lv_obj_add_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_hint, "");
        lv_obj_add_flag(s_hint_a, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint_b, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dot[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dot[1], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    uint32_t rgb = theme_session_rgb(s);
    const slot_t *sl = &s_slots[s_sel];
    if (sl->n_merged) {
        snprintf(buf, sizeof buf, "%d sessions", sl->n_merged);
        set_txt(s_agent, s_agent_txt, sizeof s_agent_txt, buf);
        set_txt(s_repo, s_repo_txt, sizeof s_repo_txt, "Done");
    } else {
        set_txt(s_agent, s_agent_txt, sizeof s_agent_txt, theme_agent_name(s->agent));
        const char *base = theme_basename(s->repo);
        set_txt(s_repo, s_repo_txt, sizeof s_repo_txt, base[0] ? base : "(no folder)");
    }
    lv_obj_set_style_text_color(s_repo, COL_TEXT, 0);
    snprintf(buf, sizeof buf, "%s  %s", state_icon(s), theme_state_word(s));
    set_txt(s_state, s_state_txt, sizeof s_state_txt, buf);
    lv_obj_set_style_text_color(s_state, lv_color_hex(rgb), 0);
    update_elapsed(s);

    bool perm = approvable(s) != NULL;
    const char *pill = perm ? "HOLD " LV_SYMBOL_OK " APPROVE" : s->state == SESS_DONE ? "FOCUS + ACK" : "FOCUS";
    set_txt(s_pill_lbl, s_pill_txt, sizeof s_pill_txt, pill);
    lv_obj_set_style_text_color(s_pill_lbl, lv_color_hex(rgb), 0);
    lv_obj_set_style_border_color(s_pill, lv_color_hex(rgb), 0);
    lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_pill, LV_ALIGN_TOP_MID, 0, 262);
    lv_label_set_text(s_hint, perm ? "swipe down to deny" : "");
    lv_obj_set_style_text_color(s_hint_a, lv_color_hex(rgb), 0);
    lv_obj_remove_flag(s_hint_a, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_dot[0], LV_OBJ_FLAG_HIDDEN);
    if (s_n > 1) {
        lv_obj_remove_flag(s_hint_b, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_dot[1], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_hint_b, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dot[1], LV_OBJ_FLAG_HIDDEN);
    }
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
        if (s_press_centre && approvable(s)) {
            s_holding = true;
            s_ramp_started = false;
            s_hold_t0 = s_now;
            s_hold_rid = model_attention_for_sid(s->sid)->rid;
        }
        return true;
    case G_RELEASE:
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
            if (g->dur_ms >= UI_HOLD_MS) {
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
                ring_set_progress(s_ring, (int)(el * 1000 / UI_HOLD_MS), 0xffffff);
            }
            if (el >= UI_HOLD_MS) {
                s_hold_fired = true;
                s_holding = false;
                s_ramp_started = false;
                ring_set_progress(s_ring, 0, 0xffffff);
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
        lv_obj_set_style_opa(s_centre, amb ? LV_OPA_30 : LV_OPA_COVER, 0);
    }

    if (now - s_last_elapsed >= 500) {
        s_last_elapsed = now;
        const session_t *s = sel_session();
        if (s && s_n > 0 && !s_slots[s_sel].n_merged) update_elapsed(s);
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
    place(s_agent, 94, 200);
    s_repo = theme_label(s_centre, 32, COL_TEXT);
    place(s_repo, 116, 250);
    s_state = theme_label(s_centre, 24, COL_MUTED);
    place(s_state, 162, 250);
    s_elapsed = theme_label(s_centre, 32, COL_MUTED);
    place(s_elapsed, 196, 250);

    s_pill = lv_obj_create(s_centre);
    lv_obj_remove_style_all(s_pill);
    lv_obj_set_size(s_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(s_pill, 2, 0);
    lv_obj_set_style_radius(s_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(s_pill, 14, 0);
    lv_obj_set_style_pad_ver(s_pill, 6, 0);
    lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_pill_lbl = theme_label(s_pill, 20, COL_TEXT);
    lv_label_set_long_mode(s_pill_lbl, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(s_pill_lbl, LV_SIZE_CONTENT);

    s_hint = theme_label(s_centre, 16, COL_MUTED);
    place(s_hint, 306, 230);
    s_link = theme_label(s_centre, 16, COL_MUTED);
    place(s_link, 392, 170);

    s_hint_a = theme_label(s_centre, 16, COL_TEXT);
    lv_label_set_text(s_hint_a, "FOCUS");
    s_hint_b = theme_label(s_centre, 16, COL_MUTED);
    lv_label_set_text(s_hint_b, "NEXT");
    static const int deg[2] = {HINT_A_DEG, HINT_B_DEG};
    lv_obj_t *h[2] = {s_hint_a, s_hint_b};
    for (int i = 0; i < 2; i++) {
        float rad = (float)deg[i] * 3.14159265f / 180.0f;
        lv_obj_set_width(h[i], 60);
        lv_obj_set_style_text_align(h[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(h[i], UI_CX + (int)((float)HINT_R * sinf(rad)) - 30,
                       UI_CY - (int)((float)HINT_R * cosf(rad)) - 9);
        lv_obj_t *d = s_dot[i] = lv_obj_create(s_centre);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 6, 6);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, i == 0 ? COL_TEXT : COL_MUTED, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(d, UI_CX + (int)((float)HINT_DOT_R * sinf(rad)) - 3,
                       UI_CY - (int)((float)HINT_DOT_R * cosf(rad)) - 3);
    }
    lv_obj_t *all[] = {s_agent, s_repo, s_state, s_elapsed, s_pill_lbl, s_hint, s_link};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) lv_label_set_text(all[i], "");
    ui_home_animations_changed();
    return s_root;
}
