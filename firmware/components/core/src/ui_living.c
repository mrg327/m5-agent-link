/* Living marks on screen: see ui_living.h. The decisions are pure (persona.c says what may move,
 * temper.c how); this file keeps, per bound mark, which session it shows and since when, and
 * hands each step's pose to the change-only setters. The finish's radial burst is the one object
 * of its own: eight dots in the state colour, each invalidated as its own small box. */
#include "ui_living.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define JOY_DOTS 8
#define JOY_R0 70  /* the sparks leave from just outside the hero glyph ... */
#define JOY_R1 104 /* ... and fade out past the time dots */
#define JOY_DOT 4  /* px radius at the start; 1 at the end */
#define JOY_HALF (JOY_R1 + JOY_DOT + 2)
#define AT_REST_MS 60000u /* "since" for a mark that starts at rest: every gesture is over */

typedef struct {
    lv_obj_t *sigil, *hero, *joy;
    int16_t r, hx, hy;
    bool lean_x;
    uint32_t sid, since;
    char name[40];
    temper_state_t st;
    temper_t tp;
    unsigned allow;
    int16_t joy_pm; /* -1: no burst */
} mark_t;

static mark_t s_marks[UI_LIVING_SLOTS];
static persona_ctx_t s_ctx;
static uint32_t s_now;

/* ───────── the burst ───────── */

static void joy_dot(const mark_t *m, int k, int *x, int *y, int *r)
{
    float t = (float)m->joy_pm / 1000.0f, u = 1.0f - t;
    float e = 1.0f - u * u * u; /* out3 */
    float rr = (float)JOY_R0 + (float)(JOY_R1 - JOY_R0) * e;
    float a = (float)(k * 45 + m->tp.spark_deg) * 0.017453292f;
    lv_area_t c;
    lv_obj_get_coords(m->joy, &c);
    *x = (c.x1 + c.x2) / 2 + (int)lroundf(rr * sinf(a));
    *y = (c.y1 + c.y2) / 2 - (int)lroundf(rr * cosf(a));
    *r = JOY_DOT - (int)lroundf((float)(JOY_DOT - 1) * t);
}

static void joy_invalidate(const mark_t *m)
{
    if (m->joy_pm < 0) return;
    for (int k = 0; k < JOY_DOTS; k++) {
        int x, y, r;
        joy_dot(m, k, &x, &y, &r);
        lv_area_t a = {x - r - 1, y - r - 1, x + r + 1, y + r + 1};
        lv_obj_invalidate_area(m->joy, &a);
    }
}

static void joy_draw(lv_event_t *e)
{
    const mark_t *m = lv_event_get_user_data(e);
    if (m->joy_pm < 0) return;
    lv_layer_t *l = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = lv_color_hex(RGB_DONE); /* colour is state: a finish is green */
    d.bg_opa = (lv_opa_t)(LV_OPA_COVER - (LV_OPA_COVER - LV_OPA_30) * m->joy_pm / 1000);
    for (int k = 0; k < JOY_DOTS; k++) {
        int x, y, r;
        joy_dot(m, k, &x, &y, &r);
        lv_area_t a = {x - r, y - r, x + r, y + r};
        lv_draw_rect(l, &d, &a);
    }
}

static void joy_set(mark_t *m, int pm)
{
    if (!m->joy || pm == m->joy_pm) return;
    joy_invalidate(m);
    m->joy_pm = (int16_t)pm;
    joy_invalidate(m);
}

/* ───────── binding ───────── */

void ui_living_bind(ui_living_slot_t slot, lv_obj_t *sigil, int r, lv_obj_t *hero, int hero_cx,
                    int hero_cy, bool lean_x)
{
    if (slot >= UI_LIVING_SLOTS) return;
    mark_t *m = &s_marks[slot];
    memset(m, 0, sizeof *m);
    m->sigil = sigil;
    m->r = (int16_t)r;
    m->hero = hero;
    m->hx = (int16_t)hero_cx;
    m->hy = (int16_t)hero_cy;
    m->lean_x = lean_x;
    m->joy_pm = -1;
    if (!hero) return;
    m->joy = lv_obj_create(lv_obj_get_parent(hero));
    lv_obj_remove_style_all(m->joy);
    lv_obj_remove_flag(m->joy, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(m->joy, 2 * JOY_HALF + 1, 2 * JOY_HALF + 1);
    lv_obj_set_pos(m->joy, hero_cx - JOY_HALF, hero_cy - JOY_HALF);
    lv_obj_add_event_cb(m->joy, joy_draw, LV_EVENT_DRAW_MAIN, m);
}

void ui_living_show(ui_living_slot_t slot, const char *name, uint32_t sid, temper_state_t st)
{
    if (slot >= UI_LIVING_SLOTS) return;
    mark_t *m = &s_marks[slot];
    if (!name) name = "";
    if (!name[0] && !sid) st = TEMPER_STILL;
    if (sid != m->sid || strncmp(name, m->name, sizeof m->name - 1) != 0) {
        m->sid = sid;
        snprintf(m->name, sizeof m->name, "%s", name);
        m->tp = temper_of(name, sid);
        m->st = st;
        m->since = s_now - AT_REST_MS; /* a new session starts at rest */
        return;
    }
    if (st == m->st) return;
    m->st = st;
    m->since = s_now;
}

temper_state_t ui_living_state(const session_t *s, bool asks, bool snoozed, bool still)
{
    if (still) return TEMPER_STILL;
    if (asks || s->state == SESS_INPUT) return TEMPER_WANTS;
    if (snoozed) return TEMPER_ASLEEP;
    switch (s->state) {
    case SESS_START:
    case SESS_WORK: return TEMPER_WORK;
    case SESS_IDLE: return TEMPER_IDLE;
    case SESS_DONE: return TEMPER_DONE;
    case SESS_ERROR: return TEMPER_ERROR;
    default: return TEMPER_STILL;
    }
}

/* ───────── each step ───────── */

static void rest(mark_t *m)
{
    ui_sigil_set_pose(m->sigil, &temper_neutral);
    if (m->hero) ui_glyph_set_pos(m->hero, m->hx, m->hy);
    joy_set(m, -1);
}

static void advance(mark_t *m, int slot, uint32_t now)
{
    persona_ctx_t c = s_ctx;
    c.alerting = m->st == TEMPER_WANTS; /* the asking mark's lean is the request's own */
    unsigned allow = persona_allow(PF_TEMPER, &c);
    if (allow != m->allow) {
        m->allow = allow;
        dbg_event("fun", "marks", "\"slot\":%d,\"state\":%d,\"allow\":%u", slot, (int)m->st, allow);
    }
    uint32_t since = now - m->since;
    temper_pose_t p = temper_pose(&m->tp, m->st, since, now, allow, m->r);
    if (m->lean_x && m->st == TEMPER_WANTS) {
        p.dx = p.dy;
        p.dy = 0;
    }
    ui_sigil_set_pose(m->sigil, &p);
    if (m->hero) {
        temper_pose_t h = temper_hero(&m->tp, m->st, since, allow);
        ui_glyph_set_pos(m->hero, m->hx + h.dx, m->hy + h.dy);
    }
    joy_set(m, temper_joy(m->st, since, allow));
}

void ui_living_tick(const persona_ctx_t *ctx, uint32_t now_ms)
{
    s_ctx = *ctx;
    s_now = now_ms;
    for (int i = 0; i < UI_LIVING_SLOTS; i++) {
        mark_t *m = &s_marks[i];
        if (!m->sigil) continue;
        /* a mark out of sight rests, so it is never found mid-gesture */
        if (ui_mode() != MODE_HOME || lv_obj_has_flag(m->sigil, LV_OBJ_FLAG_HIDDEN) ||
            m->st == TEMPER_STILL) {
            rest(m);
            continue;
        }
        advance(m, i, now_ms);
    }
}

unsigned ui_living_allow(persona_feat_t f) { return persona_allow(f, &s_ctx); }

const persona_ctx_t *ui_living_ctx(void) { return &s_ctx; }
