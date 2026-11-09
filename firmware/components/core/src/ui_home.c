/* The HOME deck (docs/design/UI-V2.md §4.1): one session card in the centre, a thin pinned index ring
 * at the rim. This file owns the slots (pinned order, merged Done groups), the selection, the actions
 * the interaction layer (nav.c) hands it, the nag and the ambient timer; the card itself is drawn by
 * ui_home_card.c. Nothing on the deck is destructive: a request shows its text here and is answered
 * on the ASK screen. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_home_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define AMBIENT_MS 30000
#define MERGE_AT 9
#define NAG_MS 15000
#define NAG_MAX 3

static lv_obj_t *s_root, *s_ring, *s_centre;

static home_slot_t s_slots[RING_MAX_SEGMENTS];
static int s_n, s_sel;
static uint32_t s_sel_sid;
static bool s_has_sel;
static bool s_scrubbing, s_freeze_expand;
static uint32_t s_now, s_last_input, s_last_elapsed;
static bool s_ambient;

static uint32_t s_nag_rid, s_nag_next;
static int s_nag_n;

static const session_t *sel_session(void)
{
    return s_has_sel ? model_find(s_sel_sid) : NULL;
}

/* An unanswered permission or question for the session: what ASK would show. */
static const attention_t *askable(const session_t *s)
{
    if (!s || s->state != SESS_INPUT) return NULL;
    const attention_t *a = model_attention_for_sid(s->sid);
    return a && (a->kind == ATT_APPROVAL || a->kind == ATT_QUESTION) && !a->answer_sent ? a : NULL;
}

/* The subset that can be approved, and so denied: the only state in which the deny swipe exists (on ASK). */
static const attention_t *approvable(const session_t *s)
{
    const attention_t *a = askable(s);
    return a && a->kind == ATT_APPROVAL && (a->acts & (1u << ACT_APPROVE)) ? a : NULL;
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
            s_slots[m++] = (home_slot_t){l[i]->sid, (uint8_t)ndone};
            continue;
        }
        s_slots[m++] = (home_slot_t){l[i]->sid, 0};
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
        segs[i].dim = (s->state == SESS_DONE && (s->seen || s_slots[i].n_merged)) || model_session_snoozed(s, theme_now_unix());
    }
    ring_set(s_ring, segs, s_n, s_now);
}

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
    ui_home_card_update();
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
    ui_home_card_update();
    dbg_event("ui", "select", "\"sid\":%u,\"idx\":%d,\"n\":%d,\"via\":\"%s\"", (unsigned)sid, s_sel, s_n, via);
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

/* Accessors for the mode plumbing in ui.c, the other screens, and the card. */
bool ui_home_request_pending(void) { return approvable(sel_session()) != NULL; }
bool ui_home_askable(void) { return askable(sel_session()) != NULL; }
uint32_t ui_home_sel_sid(void) { return sel_session() ? s_sel_sid : 0; }
lv_obj_t *ui_home_ring(void) { return s_ring; }
void ui_home_note_input(void) { noted_input(); }
const session_t *ui_home_sel_session(void) { return sel_session(); }
const home_slot_t *ui_home_sel_slot(void) { return s_n > 0 ? &s_slots[s_sel] : NULL; }
int ui_home_slot_count(void) { return s_n; }
const attention_t *ui_home_askable_of(const session_t *s) { return askable(s); }
void ui_home_set_card_shown(bool shown)
{
    if (shown) {
        lv_obj_remove_flag(s_centre, LV_OBJ_FLAG_HIDDEN);
        if (g_ui_actions->sectors) g_ui_actions->sectors(s_n); /* the rim counts sessions again */
    } else {
        lv_obj_add_flag(s_centre, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Acts on what the interaction layer decided (nav.c). No hold exists on the deck: approving means
 * opening ASK (NAV_OPEN_ASK, handled in ui.c) and holding there; deny lives there too (UI-V2 §3), so
 * the deck answers nothing. */
bool ui_home_act(nav_action_t a, const gesture_evt_t *g)
{
    if (g->kind != G_LIFT && g->kind != G_SHAKE) noted_input();
    switch (a) {
    case NAV_FOCUS:
        act_focus_selected();
        return true;
    case NAV_PICK_RIM: {
        int i = ring_hit(s_ring, g->x, g->y);
        if (i >= 0) select_idx(i, "tap", g_ui_actions->tick);
        return true;
    }
    case NAV_SCRUB:
        if (g->kind == G_SCRUB_START) {
            s_scrubbing = true;
            s_freeze_expand = false;
        }
        if (g->kind == G_SCRUB_END) {
            s_scrubbing = false;
            s_freeze_expand = false;
            rebuild();
            ui_home_card_update();
            return true;
        }
        if (select_idx(g->x, "scrub", g_ui_actions->detent) && s_n > 0) {
            int i = g->y * s_n / 360;
            select_idx(i < s_n ? i : s_n - 1, "scrub", NULL);
        }
        return true;
    case NAV_NEXT_ATTENTION:
        next_attention();
        return true;
    case NAV_NEXT:
        step_selection(1, "btn");
        return true;
    case NAV_PREV:
        step_selection(-1, "btn");
        return true;
    default:
        return false; /* reserved or not for this screen */
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

    bool amb = now - s_last_input > AMBIENT_MS && !needs_you();
    if (amb != s_ambient) {
        s_ambient = amb;
        ring_set_ambient(s_ring, amb);
        ui_home_card_set_ambient(amb);
    }

    if (now - s_last_elapsed >= 500) {
        s_last_elapsed = now;
        ui_home_card_tick();
    }
}

void ui_home_refresh(uint32_t now)
{
    s_now = now;
    rebuild();
    ui_home_card_update();
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
    ui_home_card_report(r);
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
    ui_home_card_create(s_centre);
    ui_home_animations_changed();
    return s_root;
}
