/* While you were away on the bezel: see ui_replay.h. The history and the clock are pure
 * (replay.c); this file maps the ring's segments to it, sets each step's look on the ring with
 * ring_override() (change-only: a step repaints only the capsules whose state differs) and draws
 * the time arc, invalidating only the 18 degrees a step adds. With the Mote on (FUN_MOTE, issue
 * 20261126-9fe3), the orb rides the arc's head at the tucked size, fading with the arc; the arc is
 * drawn either way. */
#include "ui_replay.h"

#include "debug.h"
#include "model.h"
#include "persona.h"
#include "replay.h"
#include "ui_internal.h"
#include "ui_living.h"
#include "ui_mote_ride.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define ARC_R 216 /* the centre line: between the pips (r 210) and the capsules (r 219) */
#define ARC_W 2
#define ARC_INV_IN (ARC_R - ARC_W - 1)
#define ARC_INV_OUT (ARC_R + ARC_W + 1)

MODEL_EXT_BSS static replay_t s_rp; /* about 470 bytes, set up on the first tick */
static bool s_ready;
static lv_obj_t *s_arc;
static int s_arc_deg, s_arc_pct, s_frame = REPLAY_OFF;
static uint32_t s_input0;
static ui_mote_ride_t s_mote;

static const uint8_t s_ring_of[REPLAY_KINDS] = {RING_IDLE, RING_WORK, RING_DONE,
                                                RING_ASK,  RING_PERM, RING_ERROR};
static const uint32_t s_rgb_of[REPLAY_KINDS] = {RGB_IDLE, RGB_WORKING,    RGB_DONE,
                                                RGB_ASK,  RGB_PERMISSION, RGB_ERROR};

static uint8_t code_of(const ring_seg_t *s)
{
    uint8_t k;
    switch (s->kind) {
    case RING_WORK: k = REPLAY_WORK; break;
    case RING_ASK: k = REPLAY_ASK; break;
    case RING_PERM: k = REPLAY_PERM; break;
    case RING_DONE: k = REPLAY_DONE; break;
    case RING_ERROR: k = REPLAY_ERROR; break;
    default: k = REPLAY_IDLE; break;
    }
    return (uint8_t)(k | (s->dim ? REPLAY_DIM : 0u));
}

/* ───────── the time arc ───────── */

static void arc_draw(lv_event_t *e)
{
    if (s_arc_deg <= 0 || s_arc_pct <= 0) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = UI_CX;
    d.center.y = UI_CX;
    d.radius = ARC_R + ARC_W / 2;
    d.width = ARC_W;
    d.color = COL_TEXT;
    d.opa = (lv_opa_t)(LV_OPA_COVER * s_arc_pct / 100);
    lv_layer_t *l = lv_event_get_layer(e);
    if (s_arc_deg >= 360) { /* two halves: one 0..359 arc leaves a seam */
        d.start_angle = 0;
        d.end_angle = 181;
        lv_draw_arc(l, &d);
        d.start_angle = 180;
        d.end_angle = 1;
    } else { /* clockwise from 12 o'clock */
        d.start_angle = 270;
        d.end_angle = (lv_value_precise_t)((270 + s_arc_deg) % 360);
    }
    lv_draw_arc(l, &d);
}

static void arc_set(int deg, int pct)
{
    if (deg == s_arc_deg && pct == s_arc_pct) return;
    int lo = 0, hi = deg > s_arc_deg ? deg : s_arc_deg;
    if (pct == s_arc_pct) lo = deg < s_arc_deg ? deg : s_arc_deg; /* it grew: only the new part */
    s_arc_deg = deg;
    s_arc_pct = pct;
    ring_invalidate_arc(s_arc, lo - 2, hi + 2, ARC_INV_IN, ARC_INV_OUT);
}

/* A full-screen, see-through object just above the ring that draws nothing until a replay: created
 * on the first tick, with the deck, so it never costs a full repaint later. */
static void arc_create(lv_obj_t *ring)
{
    s_arc = lv_obj_create(lv_obj_get_parent(ring));
    lv_obj_remove_style_all(s_arc);
    lv_obj_set_size(s_arc, UI_SIZE, UI_SIZE);
    lv_obj_set_pos(s_arc, 0, 0);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(s_arc, lv_obj_get_index(ring) + 1);
    lv_obj_add_event_cb(s_arc, arc_draw, LV_EVENT_DRAW_MAIN, NULL);
    ui_mote_ride_create(&s_mote, lv_obj_get_parent(ring));
    lv_obj_move_to_index(s_mote.obj, lv_obj_get_index(s_arc) + 1);
}

/* The Mote on the hour hand's tip: part of the replay's one-shot, so it asks the gate for one too;
 * the arc is drawn whether or not it is there. */
static void mote_set(int deg, int pct)
{
    if (deg <= 0 || pct <= 0 || !(ui_living_allow(PF_MOTE) & PA_ONESHOT)) {
        ui_mote_ride_hide(&s_mote);
        return;
    }
    mote_look_t k = ui_mote_ride_look(100, (uint8_t)(UI_MOTE_RIDE_LIT * pct / 100));
    ui_mote_ride_at(&s_mote, UI_CX, UI_CX, ARC_R, deg, &k);
}

/* ───────── the replay ───────── */

static void show_step(lv_obj_t *ring, const ring_seg_t *segs, int n, int step)
{
    for (int i = 0; i < n; i++) {
        uint8_t c = replay_code(&s_rp, segs[i].id, step);
        if (c == REPLAY_UNKNOWN) {
            ring_override(ring, i, RING_LIVE, 0, false);
        } else if (c == REPLAY_ABSENT) {
            ring_override(ring, i, RING_HOLLOW, 0, false);
        } else {
            unsigned k = (c & 0x0Fu) < REPLAY_KINDS ? (c & 0x0Fu) : (unsigned)REPLAY_IDLE;
            ring_override(ring, i, s_ring_of[k], s_rgb_of[k], (c & REPLAY_DIM) != 0);
        }
    }
}

static void end(lv_obj_t *ring, const char *why)
{
    replay_stop(&s_rp);
    ring_override_end(ring);
    arc_set(0, 0);
    ui_mote_ride_hide(&s_mote);
    s_frame = REPLAY_OFF;
    dbg_event("fun", "replay", "\"act\":\"end\",\"why\":\"%s\"", why);
}

/* The first look after an away: replay only when the gate allows a one-shot (nothing needs you,
 * no reduced motion, the bit on), the deck is up and awake, and the hour holds a change. */
static void try_start(const replay_seg_t *rs, int n, uint32_t now_ms, uint32_t input_ms)
{
    const char *skip = NULL;
    int changes = replay_changes(&s_rp, rs, n);
    if (!(ui_living_allow(PF_REPLAY) & PA_ONESHOT)) {
        skip = "gate";
    } else if (ui_mode() != MODE_HOME || ui_dimmed() || !ui_animations_allowed()) {
        skip = "screen";
    } else if (changes < REPLAY_MIN_CHANGES) {
        skip = "still";
    }
    if (skip) {
        if (ui_living_ctx()->fun & FUN_REPLAY) {
            dbg_event("fun", "replay", "\"act\":\"skip\",\"why\":\"%s\"", skip);
        }
        return;
    }
    replay_start(&s_rp, now_ms);
    s_input0 = input_ms;
    s_frame = REPLAY_OFF;
    dbg_event("fun", "replay", "\"act\":\"start\",\"n\":%d,\"changes\":%d,\"mote\":%d", n, changes,
              (ui_living_allow(PF_MOTE) & PA_ONESHOT) ? 1 : 0);
}

void ui_replay_tick(lv_obj_t *ring, uint32_t now_ms, uint32_t input_ms)
{
    if (!ring) return;
    if (!s_ready) {
        replay_init(&s_rp);
        arc_create(ring);
        s_ready = true;
    }
    ring_seg_t segs[RING_MAX_SEGMENTS];
    replay_seg_t rs[RING_MAX_SEGMENTS];
    int n = ring_get(ring, segs, RING_MAX_SEGMENTS);
    for (int i = 0; i < n; i++) rs[i] = (replay_seg_t){segs[i].id, code_of(&segs[i])};
    replay_note(&s_rp, rs, n, now_ms);
    bool woke = replay_woke(&s_rp, ui_living_ctx()->display_on, now_ms);

    if (!s_rp.playing) {
        if (woke) try_start(rs, n, now_ms, input_ms);
        if (!s_rp.playing) return;
    }
    /* input is never held and alerts win: either ends it at once, on the live ring */
    if (input_ms != s_input0) {
        end(ring, "input");
        return;
    }
    if (!(ui_living_allow(PF_REPLAY) & PA_ONESHOT)) {
        end(ring, "gate");
        return;
    }
    if (ui_mode() != MODE_HOME) {
        end(ring, "screen");
        return;
    }
    int f = replay_frame(&s_rp, now_ms);
    if (f == REPLAY_OFF) {
        end(ring, "done");
        return;
    }
    if (f != s_frame) {
        if (f < REPLAY_STEPS) {
            show_step(ring, segs, n, f);
        } else {
            ring_override_end(ring); /* now, while the arc fades */
        }
        s_frame = f;
    }
    int deg = replay_arc_deg(f), pct = replay_arc_pct(&s_rp, now_ms);
    arc_set(deg, pct);
    mote_set(deg, pct);
}

bool ui_replay_playing(void) { return s_rp.playing; }
