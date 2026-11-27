/* The decision toys in Sigil (EXPERIMENTS.md "Toys", issue 20261126-74ee; the brief is the UX
 * research's idea 17): a coin, a die and an oracle, for a choice that should take five seconds.
 * The logic and the timelines are decide.c; this draws them and turns input into throws.
 *
 * - Coin: a white disc r 90 that turns about its vertical axis (a 48-triangle fan as wide as
 *   |cos| of the turn) and lands on a black tick or cross (Bold, 96 px). Before the first throw the
 *   disc is blank. A detent on each half-turn, a 60 ms pulse and a two-note cue on landing.
 * - Die: a ring r 156 in as many pieces as the die has sides, the number 56 px in the middle. The
 *   ring fills to the number, so the number reads twice. The bezel picks 4, 6, 8, 12 or 20 sides
 *   (a detent a step). It rattles through nine numbers, a detent on each, and lands with a 50 ms
 *   pulse and its cue.
 * - Oracle: hold a finger in the middle, or key A, or hold the watch face up and still, for 2 s
 *   while a grey ring fills; one of six glyphs (128 px, all white: a green tick would read as
 *   "done") rises 20 px into place, with a 70 ms pulse and a bell. The six are to be tried on
 *   people (the issue): tick yes, cross no, hourglass later, arrows ask again, moon sleep on it,
 *   speech bubble unclear.
 *
 * A or a tap throws the coin or the die; a flick of the watch does too (toy_motion.h). B steps
 * coin, die, oracle (three pips say which). A request takes the screen at once (ui_toy.h): the
 * throw freezes where it was, the screen comes back dim, and a tap or A carries on. While a request
 * waits nothing is thrown (the hand sign), so nobody rolls a die on an approval. Reduced motion
 * shows each end frame at once. Words only in the report (rule 9). */
#include "ui_decide.h"

#include <math.h>
#include <stdio.h>

#include "debug.h"
#include "decide.h"
#include "hal.h"
#include "motion.h"
#include "nav.h"
#include "persona.h"
#include "sound.h"
#include "toy_motion.h"
#include "ui_icons.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define COIN_R 90
#define COIN_FAN 48      /* the disc's outline points; each triangle spans two, so no seam shows */
#define COIN_FACE_MIN 55 /* % of |cos| below which the coin is too edge-on to show its face */
#define DIE_R 156        /* clear of the key hints (r 167 to 207 at 10:30 and 1:30) */
#define DIE_W 10
#define HOLD_R 100
#define HOLD_W 6
#define TAP_Y 233
#define PIPS_Y UI_Y_LEGEND
#define CUE_VOLUME 60 /* % of the stored level at most (feedback.h caps it there) */
#define PI_F 3.14159265f

/* The oracle's answers: a glyph each, and its word for the report. */
static const struct {
    const char *icon, *word;
} s_answers[DECIDE_ANSWERS] = {
    {ICON_CHECK, "yes"},        {ICON_X, "no"},
    {ICON_WAIT, "later"},       {ICON_WORKING, "ask again"},
    {ICON_IDLE, "sleep on it"}, {ICON_QUESTION, "unclear"},
};
static const char *const s_kind[DECIDE_KINDS] = {"coin", "die", "oracle"};

/* The cues, in the sound bank's format (sound_bank.h): rendered into the toy block on opening. */
#define S(w, n, sl, v, ms, d) {SND_##w, n, sl, v, ms, d}
static const snd_step_t s_cue_coin[] = {S(SQ25, 86, 0, 9, 40, 0), S(SQ25, 93, 0, 9, 160, 1)};
static const snd_step_t s_cue_die[] = {S(TRI, 79, 0, 12, 35, 0), S(TRI, 84, 0, 12, 140, 1)};
static const snd_step_t s_cue_oracle[] = {S(BELL, 84, 0, 8, 520, 1)};
#undef S
enum { CUE_COIN, CUE_DIE, CUE_ORACLE, CUES };
static size_t s_cue_at[CUES], s_cue_n[CUES];

static const uint16_t s_land_coin[] = {60};
static const uint16_t s_land_die[] = {50};
static const uint16_t s_land_oracle[] = {70};

typedef enum { HOLD_NONE, HOLD_FINGER, HOLD_KEY, HOLD_STILL } hold_src_t;

static decide_t s_d;
static toy_motion_t s_motion;
static hold_src_t s_hold_src;
static bool s_open, s_rendered;
static int s_scrub_last;
static uint32_t s_now;
static lv_obj_t *s_coin, *s_ring, *s_num, *s_hold, *s_tap, *s_answer, *s_pips, *s_hint[2];
/* what is drawn, so a step redraws only what moved */
static int s_coin_deg = -1, s_coin_face = -1, s_die_shown = -1, s_die_sides, s_rise = -1;
static int s_hold_pm = -1, s_shown_kind = -1, s_answer_shown = -1;
static bool s_coin_blank = true, s_dim, s_drawn, s_was_holding;

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

/* The microsecond of an edge: what a person cannot repeat. Not once a script seeded the source:
 * the simulator's step times follow the host's, so its runs would differ. */
static bool s_seeded;
static uint32_t entropy(void) { return s_seeded ? 0 : (uint32_t)hal_time_us(); }

void ui_decide_seed(uint32_t seed)
{
    toy_rand_seed(&s_d.rng, seed);
    s_seeded = true;
}

/* What the fun word allows the toys now (persona.h): the user started it, so buzz and sound are
 * up to the gate; reduced motion takes the one-shots. */
static bool motion_ok(void) { return (ui_toy_allow() & PA_ONESHOT) != 0; }

static lv_color_t ink(void) { return s_dim ? COL_TEXT3 : COL_TEXT; }

/* ── the coin: a disc turned about its vertical axis ── */
static void coin_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    float c = cosf((float)s_coin_deg * PI_F / 180.0f);
    float w = (float)COIN_R * fabsf(c);
    /* turned away it darkens a little: the light catches it square on */
    uint8_t lum = (uint8_t)(s_dim ? 0x6c : 0xa0 + (int)(0x5f * fabsf(c)));
    lv_color_t col = lv_color_make(lum, lum, lum);
    if (w < 2.0f) { /* edge on: a bar */
        lv_draw_rect_dsc_t r;
        lv_draw_rect_dsc_init(&r);
        r.bg_color = col;
        r.bg_opa = LV_OPA_COVER;
        r.radius = 2;
        lv_area_t bar = {cx - 2, cy - COIN_R, cx + 2, cy + COIN_R};
        lv_draw_rect(l, &r, &bar);
        return;
    }
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.color = col;
    t.opa = LV_OPA_COVER;
    int px[COIN_FAN], py[COIN_FAN];
    for (int k = 0; k < COIN_FAN; k++) {
        float th = 2.0f * PI_F * (float)k / COIN_FAN;
        px[k] = cx + (int)lroundf(w * sinf(th));
        py[k] = cy - (int)lroundf((float)COIN_R * cosf(th));
    }
    for (int k = 0; k < COIN_FAN; k++) {
        int k2 = (k + 2) % COIN_FAN; /* overlapping: each seam lies inside the next triangle */
        t.p[0].x = cx;
        t.p[0].y = cy;
        t.p[1].x = px[k];
        t.p[1].y = py[k];
        t.p[2].x = px[k2];
        t.p[2].y = py[k2];
        lv_draw_triangle(l, &t);
    }
    /* the fan's edges all meet in the middle, where their soft edges would leave a dot: a stadium
     * inside the outline covers it */
    lv_draw_rect_dsc_t core;
    lv_draw_rect_dsc_init(&core);
    core.bg_color = col;
    core.bg_opa = LV_OPA_COVER;
    core.radius = LV_RADIUS_CIRCLE;
    int hw = (int)(w * 0.7f), hh = COIN_R * 7 / 10;
    lv_area_t in = {cx - hw, cy - hh, cx + hw, cy + hh};
    lv_draw_rect(l, &core, &in);
    if (s_coin_blank || fabsf(c) * 100.0f < COIN_FACE_MIN) return;
    ui_glyph_draw(l, FONT_ICON_XXL, s_coin_face == DECIDE_FACE_TICK ? ICON_CHECK : ICON_X, cx, cy,
                  COL_ON_ACCENT, LV_OPA_COVER);
}

/* ── drawing: only what changed ── */
static void render_coin(uint32_t now, bool all)
{
    int deg = decide_coin_deg(&s_d, now);
    int face = decide_coin_face_at(&s_d, deg);
    bool blank = !s_d.shown[DECIDE_COIN] && s_d.phase != DECIDE_THROWING;
    if (all || deg != s_coin_deg || face != s_coin_face || blank != s_coin_blank) {
        s_coin_deg = deg;
        s_coin_face = face;
        s_coin_blank = blank;
        lv_obj_invalidate(s_coin);
    }
}

static void render_die(uint32_t now, bool all)
{
    int v = decide_die_shown(&s_d, now), sides = decide_sides(&s_d);
    if (!all && v == s_die_shown && sides == s_die_sides) return;
    if (sides != s_die_sides) { ui_arc_set_steps(s_ring, sides); }
    s_die_shown = v;
    s_die_sides = sides;
    ui_arc_set(s_ring, v * 1000 / sides, ink(), COL_FILL2);
    if (v) { lv_label_set_text_fmt(s_num, "%d", v); }
    lv_obj_set_style_text_color(s_num, ink(), 0);
    show(s_num, v > 0);
}

static void render_oracle(uint32_t now, bool all)
{
    int pm = decide_hold_permille(&s_d, now);
    int rise = decide_rise_px(&s_d, now);
    int ans = s_d.shown[DECIDE_ORACLE] ? s_d.answer : -1;
    bool holding = s_d.phase == DECIDE_HOLDING;
    if (all || pm != s_hold_pm) {
        s_hold_pm = pm;
        ui_arc_set(s_hold, pm, COL_TEXT2, holding ? COL_FILL2 : COL_FILL1);
    }
    if (!all && rise == s_rise && ans == s_answer_shown && holding == s_was_holding) return;
    s_was_holding = holding;
    s_rise = rise;
    s_answer_shown = ans;
    show(s_tap, ans < 0);
    show(s_answer, ans >= 0);
    if (ans >= 0) {
        /* held again: the old answer steps back while the ring fills */
        ui_glyph_set(s_answer, s_answers[ans].icon, holding ? COL_TEXT3 : ink());
        ui_glyph_set_pos(s_answer, UI_CX, UI_CX + rise);
    } else {
        ui_glyph_set(s_tap, ICON_TAP, holding ? COL_TEXT2 : COL_TEXT3);
    }
}

static void render(uint32_t now)
{
    decide_kind_t k = s_d.kind;
    bool all = !s_drawn || (int)k != s_shown_kind || s_d.paused != s_dim;
    s_dim = s_d.paused;
    if ((int)k != s_shown_kind) {
        s_shown_kind = (int)k;
        show(s_coin, k == DECIDE_COIN);
        show(s_ring, k == DECIDE_DIE);
        show(s_num, false); /* the die and the oracle show theirs below */
        show(s_hold, k == DECIDE_ORACLE);
        show(s_tap, false);
        show(s_answer, false);
        ui_pips_set(s_pips, DECIDE_KINDS, (int)k);
        ui_key_hint_set(s_hint[0], k == DECIDE_ORACLE ? ICON_TAP : ICON_RERUN);
    }
    if (k == DECIDE_COIN) {
        render_coin(now, all);
    } else if (k == DECIDE_DIE) {
        render_die(now, all);
    } else {
        render_oracle(now, all);
    }
    s_drawn = true;
}

/* ── feedback, through the toy gate: refusals are fine (silent mode, face down, an alert) ── */
static void cue(int which)
{
    if (s_rendered && s_cue_n[which]) {
        g_ui_actions->toy_play(sound_toy_pcm() + s_cue_at[which], s_cue_n[which], CUE_VOLUME);
    }
}

static void land(void)
{
    s_hold_src = HOLD_NONE;
    toy_motion_rearm(&s_motion); /* the next still hold needs the watch moved first */
    switch (s_d.kind) {
    case DECIDE_COIN:
        g_ui_actions->toy_buzz(s_land_coin, 1);
        cue(CUE_COIN);
        break;
    case DECIDE_DIE:
        g_ui_actions->toy_buzz(s_land_die, 1);
        cue(CUE_DIE);
        break;
    default:
        g_ui_actions->toy_buzz(s_land_oracle, 1);
        cue(CUE_ORACLE);
        if (!motion_ok()) decide_finish(&s_d, s_now);
        break;
    }
    dbg_event("ui", "decide", "\"kind\":\"%s\",\"result\":%d", s_kind[s_d.kind],
              s_d.kind == DECIDE_COIN  ? s_d.coin_face
              : s_d.kind == DECIDE_DIE ? s_d.die_value
                                       : s_d.answer);
}

/* Nothing that could sway an answer while a request waits (ui_toy.h not_over_request). */
static bool request_waits(void)
{
    if (!ui_pending_attention_sid()) return false;
    ui_toast_sign("Answer the request first", ICON_PERMISSION, NULL, RGB_AMBER);
    g_ui_actions->reject();
    return true;
}

static void do_throw(const char *via)
{
    if (request_waits()) return;
    if (!decide_throw(&s_d, s_now, entropy())) return;
    dbg_event("ui", "decide_throw", "\"kind\":\"%s\",\"via\":\"%s\"", s_kind[s_d.kind], via);
    if (!motion_ok()) decide_finish(&s_d, s_now);
}

static void hold(hold_src_t src, bool down, uint32_t from)
{
    if (down) {
        if (s_hold_src != HOLD_NONE || request_waits()) return;
        if (decide_hold(&s_d, true, from, entropy())) {
            s_hold_src = src;
            g_ui_actions->toy_detent();
        }
    } else if (s_hold_src == src) {
        s_hold_src = HOLD_NONE;
        decide_hold(&s_d, false, from, 0);
    }
}

/* ── the toy contract (ui_toy.h) ── */
static void decide_ui_init(lv_obj_t *root)
{
    s_coin = lv_obj_create(root);
    lv_obj_remove_style_all(s_coin);
    lv_obj_remove_flag(s_coin, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_coin, 2 * COIN_R + 3, 2 * COIN_R + 3);
    lv_obj_set_pos(s_coin, UI_CX - COIN_R - 1, UI_CX - COIN_R - 1);
    lv_obj_add_event_cb(s_coin, coin_draw, LV_EVENT_DRAW_MAIN, NULL);
    s_ring = ui_arc_create(root, UI_CX, UI_CX, DIE_R, DIE_W);
    s_num = lv_label_create(root);
    lv_obj_set_style_text_font(s_num, FONT_NUM, 0);
    lv_obj_set_style_text_color(s_num, COL_TEXT, 0);
    lv_obj_center(s_num);
    s_hold = ui_arc_create(root, UI_CX, UI_CX, HOLD_R, HOLD_W);
    s_tap = ui_glyph_create(root, FONT_ICON_XL, UI_CX, TAP_Y);
    s_answer = ui_glyph_create(root, FONT_ICON_HERO, UI_CX, UI_CX);
    s_pips = ui_pips_create(root, UI_CX, PIPS_Y);
    s_hint[0] = ui_key_hint_create(root, UI_KEY_A);
    s_hint[1] = ui_key_hint_create(root, UI_KEY_B);
    ui_key_hint_set(s_hint[1], ICON_NEXT);
    show(s_ring, false);
    show(s_num, false);
    show(s_hold, false);
    show(s_tap, false);
    show(s_answer, false);
    decide_init(&s_d, (uint32_t)hal_time_us()); /* the boot's microseconds, then every edge */
}

/* The cues go into the toy block each time: another toy may have used it meanwhile. */
static void render_cues(void)
{
    static const snd_step_t *const steps[CUES] = {s_cue_coin, s_cue_die, s_cue_oracle};
    static const uint8_t n[CUES] = {2, 2, 1};
    size_t at = 0;
    for (int i = 0; i < CUES; i++) {
        snd_sound_t s = {.tracks = {{steps[i], n[i]}}};
        s_cue_at[i] = at;
        s_cue_n[i] = sound_toy_render(&s, at);
        at += s_cue_n[i];
    }
    s_rendered = true;
}

static void decide_ui_open(uint32_t now)
{
    s_now = now;
    s_open = true;
    s_hold_src = HOLD_NONE;
    toy_motion_reset(&s_motion);
    render_cues();
    s_drawn = false;
    s_shown_kind = -1;
    render(now);
}

static void decide_ui_pause(uint32_t now)
{
    s_now = now;
    s_open = false;
    s_hold_src = HOLD_NONE;
    decide_pause(&s_d, now);
    s_drawn = false;
}

static bool resume_if_paused(void)
{
    if (!s_d.paused) return false;
    decide_resume(&s_d, s_now);
    g_ui_actions->tick();
    dbg_event("ui", "decide_resume", "\"kind\":\"%s\"", s_kind[s_d.kind]);
    return true;
}

static bool decide_ui_act(nav_action_t a, const gesture_evt_t *g)
{
    bool oracle = s_d.kind == DECIDE_ORACLE;
    switch (g->kind) {
    case G_KEY_DOWN:
        if (g->x != 0) return true; /* B acts on its release: B held goes back instead */
        if (resume_if_paused()) return true;
        if (oracle) {
            hold(HOLD_KEY, true, s_now);
        } else {
            do_throw("key");
        }
        return true;
    case G_KEY_UP:
        if (g->x == 0) {
            if (oracle) hold(HOLD_KEY, false, s_now);
            return true;
        }
        if (s_d.paused || !decide_next_kind(&s_d)) return true;
        s_hold_src = HOLD_NONE;
        g_ui_actions->toy_detent();
        dbg_event("ui", "decide_kind", "\"kind\":\"%s\"", s_kind[s_d.kind]);
        return true;
    case G_PRESS:
        if (oracle && !s_d.paused && nav_centre(g->x, g->y)) hold(HOLD_FINGER, true, s_now);
        return true;
    case G_RELEASE:
        if (oracle) hold(HOLD_FINGER, false, s_now);
        return true;
    default: break;
    }
    switch (a) {
    case NAV_COMMIT: /* a tap */
        if (resume_if_paused()) return true;
        if (!oracle) do_throw("tap");
        return true;
    case NAV_SCRUB: {
        int d = nav_list_scrub(&s_scrub_last, g->kind, g->x);
        if (s_d.kind == DECIDE_DIE && !s_d.paused && d && decide_step_die(&s_d, d)) {
            g_ui_actions->toy_detent();
        }
        return true;
    }
    default: return false;
    }
}

/* The flick and the still hold, from the 10 Hz gravity samples (motion.c's pose). */
static void feel(uint32_t now)
{
    int x, y, z;
    if (!motion_pose(&x, &y, &z)) return;
    /* every step: between samples the pose repeats, a change of 0, which is what still means */
    if (toy_motion_feed(&s_motion, x, y, z, now) && !s_d.paused && s_d.kind != DECIDE_ORACLE &&
        s_d.phase != DECIDE_THROWING) {
        toy_rand_stir(&s_d.rng, (uint32_t)(x ^ (y << 10) ^ (z << 20)));
        do_throw("flick");
    }
    if (s_d.kind != DECIDE_ORACLE || s_d.paused) return;
    uint32_t still = toy_motion_still_ms(&s_motion, now);
    if (still && s_hold_src == HOLD_NONE && s_d.phase != DECIDE_HOLDING) {
        hold(HOLD_STILL, true, now - still);
    } else if (!still && s_hold_src == HOLD_STILL) {
        hold(HOLD_STILL, false, now);
    }
}

static void decide_ui_tick(uint32_t now)
{
    s_now = now;
    if (!s_open) return;
    feel(now);
    unsigned ev = decide_step(&s_d, now);
    if (ev & DECIDE_EV_TURN) g_ui_actions->toy_detent();
    if (ev & DECIDE_EV_LAND) land();
    render(now);
}

static void decide_ui_report(ui_report_t *r)
{
    ui_report_add(r, "Decide");
    if (s_d.kind == DECIDE_DIE) {
        ui_report_add(r, "die d%d", decide_sides(&s_d));
    } else {
        ui_report_add(r, "%s", s_kind[s_d.kind]);
    }
    static const char *const phase[] = {"ready", "throwing", "holding", "shown"};
    ui_report_add(r, "%s", s_d.paused ? "paused" : phase[s_d.phase]);
    if (s_d.phase == DECIDE_HOLDING) ui_report_add(r, "hold %d", decide_hold_permille(&s_d, s_now));
    if (s_d.phase != DECIDE_SHOWN) return;
    switch (s_d.kind) {
    case DECIDE_COIN:
        ui_report_add(r, "result %s", s_d.coin_face == DECIDE_FACE_TICK ? "tick" : "cross");
        break;
    case DECIDE_DIE: ui_report_add(r, "result %d", s_d.die_value); break;
    default: ui_report_add(r, "result %s", s_answers[s_d.answer].word); break;
    }
}

const ui_toy_t ui_decide_toy = {
    .name = "Decide",
    .glyph = ICON_DICE,
    .raw_keys = true,
    .not_over_request = true,
    .init = decide_ui_init,
    .open = decide_ui_open,
    .pause = decide_ui_pause,
    .act = decide_ui_act,
    .tick = decide_ui_tick,
    .report = decide_ui_report,
};
