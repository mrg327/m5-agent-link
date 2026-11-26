/* mote.c: the Mote's state machine, the firefly (issue 20261126-59e0). */
#include <stdio.h>
#include <stdlib.h>

#include "mote.h"

static int failures;
#define EXPECT(cond, msg)                                  \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

#define TICK 30

/* The dock, the idle face up, nothing running, the afternoon. */
static mote_in_t calm(void)
{
    mote_in_t in = {0};
    in.face = true;
    in.may = true;
    in.hour = 14;
    in.usage = 255;
    return in;
}

static void working(mote_in_t *in, int n, const int *deg)
{
    in->n_work = (uint8_t)n;
    for (int i = 0; i < n; i++) in->work_deg[i] = (int16_t)deg[i];
}

static int ang_diff(int a, int b)
{
    int d = ((a - b) % 360 + 360) % 360;
    return d > 180 ? 360 - d : d;
}

/* Runs from t0 to t1 in ticks; returns the last output and the largest move between two ticks. */
static mote_out_t run(mote_t *m, const mote_in_t *in, uint32_t t0, uint32_t t1, int *max_move)
{
    mote_out_t o = {0}, prev = {0};
    bool first = true;
    if (max_move) *max_move = 0;
    for (uint32_t t = t0; t <= t1; t += TICK) {
        o = mote_step(m, in, t);
        if (!first && max_move && o.mode != MOTE_GONE && prev.mode != MOTE_GONE) {
            int d = ang_diff(o.deg, prev.deg);
            if (d > *max_move) *max_move = d;
        }
        prev = o;
        first = false;
    }
    return o;
}

/* Runs until the firefly first stands at deg; returns that time, or 0 if it never does. Notes the
 * largest move a tick and whether the distance to deg ever grew (not the short way). */
static uint32_t until_at(mote_t *m, const mote_in_t *in, uint32_t t0, uint32_t t1, int deg,
                         int *max_move, bool *short_way)
{
    int prev = -1;
    if (max_move) *max_move = 0;
    if (short_way) *short_way = true;
    for (uint32_t t = t0; t <= t1; t += TICK) {
        mote_out_t o = mote_step(m, in, t);
        if (prev >= 0) {
            int d = ang_diff(o.deg, prev);
            if (max_move && d > *max_move) *max_move = d;
            if (short_way && ang_diff(o.deg, deg) > ang_diff(prev, deg)) *short_way = false;
        }
        if (o.deg == deg) return t;
        prev = o.deg;
    }
    return 0;
}

static void test_priority(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    mote_out_t o = mote_step(&m, &in, 1000);
    EXPECT(o.mode == MOTE_REST && o.deg == MOTE_REST_DEG, "nothing runs: resting at 12 o'clock");
    EXPECT(o.look.core_r == MOTE_CORE_R && o.look.lum == MOTE_LUM, "a 7 px core, ambient bright");
    EXPECT(o.look.halo_r >= MOTE_HALO_R && o.look.halo_r <= MOTE_HALO_R + MOTE_BREATH_PX,
           "a halo of about 30 px");
    EXPECT(o.rgb == MOTE_RGB && o.dx == 0 && o.dy == 0, "warm white, on its track");
    const int d[] = {90, 270};
    working(&in, 2, d);
    o = mote_step(&m, &in, 1030);
    EXPECT(o.mode == MOTE_VISIT, "something works: it visits");
    in.errors = 1;
    in.error_deg = 120;
    o = mote_step(&m, &in, 1060);
    EXPECT(o.mode == MOTE_HOVER, "an error outranks a visit");
    in.may = false;
    o = mote_step(&m, &in, 1090);
    EXPECT(o.mode == MOTE_GONE, "not allowed (anything needs you): gone in the same step");
    in.may = true;
    in.face = false;
    o = mote_step(&m, &in, 1120);
    EXPECT(o.mode == MOTE_GONE, "no idle face: gone");
}

static void test_visit_cycles(void)
{
    /* two working sessions: it appears by the nearer, lingers 8 s, crosses the short way, eased */
    mote_t m = {0};
    mote_in_t in = calm();
    const int two[] = {90, 270};
    working(&in, 2, two);
    mote_out_t o = mote_step(&m, &in, 0);
    EXPECT(o.mode == MOTE_VISIT && o.deg == 90, "appears by the first working session");
    o = run(&m, &in, TICK, MOTE_VISIT_MS - TICK, NULL);
    EXPECT(o.deg == 90, "lingers by it for 8 s");
    int mv;
    bool sw;
    uint32_t t = until_at(&m, &in, MOTE_VISIT_MS, MOTE_VISIT_MS + 10000, 270, &mv, &sw);
    uint32_t trip = MOTE_TRAVEL_MS + 180 * MOTE_TRAVEL_MS_DEG;
    EXPECT(t && t - MOTE_VISIT_MS <= trip + 2 * TICK && t - MOTE_VISIT_MS >= trip - 4 * TICK,
           "then on to the next, a trip of 4.4 s for half the ring");
    EXPECT(mv <= 2, "calm: at most 2 degrees a 30 ms tick (about 62 degrees a second)");
    o = run(&m, &in, t + TICK, t + MOTE_VISIT_MS - TICK, NULL);
    EXPECT(o.deg == 270, "lingers by the second");
    t = until_at(&m, &in, t + MOTE_VISIT_MS, t + MOTE_VISIT_MS + 10000, 90, NULL, NULL);
    EXPECT(t != 0, "and back to the first: it cycles");

    /* three: in ring order, each the short way */
    m = (mote_t){0};
    const int three[] = {60, 180, 300};
    working(&in, 3, three);
    o = mote_step(&m, &in, 0);
    EXPECT(o.deg == 60, "nearest to where it appears (12 o'clock): 60");
    uint32_t at = 0;
    const int order[] = {180, 300, 60, 180};
    for (int k = 0; k < 4; k++) {
        o = run(&m, &in, at + TICK, at + MOTE_VISIT_MS - TICK, NULL);
        EXPECT(o.deg == (k ? order[k - 1] : 60), "lingers 8 s by each");
        at = until_at(&m, &in, at + MOTE_VISIT_MS, at + MOTE_VISIT_MS + 8000, order[k], &mv, &sw);
        EXPECT(at != 0 && sw, "the next clockwise, the short way");
        EXPECT(mv <= 2, "calm on the way");
    }

    /* one: it stays by that one */
    m = (mote_t){0};
    const int one[] = {200};
    working(&in, 1, one);
    run(&m, &in, 0, 30000, &mv);
    EXPECT(mote_step(&m, &in, 30030).deg == 200 && mv == 0, "one working session: it stays by it");
}

static void test_visit_changes(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    const int two[] = {90, 270};
    working(&in, 2, two);
    run(&m, &in, 0, 3000, NULL);
    /* the one it is by finishes: it goes to the one still working */
    const int left[] = {270};
    working(&in, 1, left);
    bool sw;
    uint32_t t = until_at(&m, &in, 3030, 12000, 270, NULL, &sw);
    EXPECT(t != 0 && sw, "its session stopped: on to the one still working");
    /* the ring re-lays (a third session): the slots move; it follows to the nearest */
    const int moved[] = {60, 180, 300};
    working(&in, 3, moved);
    t = until_at(&m, &in, t + TICK, t + 8000, 300, NULL, &sw);
    EXPECT(t != 0 && sw, "the slots moved: to the nearest of them, not a jump");
}

static void test_rest_and_ember(void)
{
    /* from a visit at 150 back to 12 the short way (anticlockwise) */
    mote_t m = {0};
    mote_in_t in = calm();
    const int one[] = {150};
    working(&in, 1, one);
    mote_step(&m, &in, 0);
    in.n_work = 0;
    int mv;
    bool sw;
    uint32_t t = until_at(&m, &in, TICK, 10000, MOTE_REST_DEG, &mv, &sw);
    EXPECT(t && sw && mv <= 2, "nothing runs: to 12 o'clock, the short way, calm");
    EXPECT(mote_step(&m, &in, t + TICK).mode == MOTE_REST, "resting");
    /* it breathes: the halo between its exhale and its inhale, in a few levels */
    int lo = 255, hi = 0, changes = 0;
    mote_out_t prev = mote_step(&m, &in, t + 2 * TICK);
    for (uint32_t u = t + 3 * TICK; u < t + 3 * TICK + MOTE_BREATH_MS; u += TICK) {
        mote_out_t o = mote_step(&m, &in, u);
        if (o.look.halo_r < lo) lo = o.look.halo_r;
        if (o.look.halo_r > hi) hi = o.look.halo_r;
        if (o.look.halo_r != prev.look.halo_r || o.look.halo_pct != prev.look.halo_pct) changes++;
        EXPECT(o.look.lum == MOTE_LUM && o.deg == 0, "the core holds still");
        prev = o;
    }
    EXPECT(lo == MOTE_HALO_R && hi == MOTE_HALO_R + MOTE_BREATH_PX, "the halo breathes 26-30 px");
    EXPECT(changes >= 4 && changes <= 2 * (MOTE_BREATH_STEPS - 1), "in a few steps a breath");
    /* five minutes resting (from the first step without work, at TICK): an ember */
    mote_out_t o = run(&m, &in, t + 4000, MOTE_EMBER_MS, NULL);
    EXPECT(o.mode == MOTE_REST, "rested under five minutes");
    o = mote_step(&m, &in, TICK + MOTE_EMBER_MS);
    EXPECT(o.mode == MOTE_EMBER && o.look.core_r < MOTE_CORE_R && o.look.lum == MOTE_LUM_EMBER,
           "an ember after exactly five minutes: smaller and dim");
    mote_look_t k = o.look;
    for (uint32_t u = TICK + MOTE_EMBER_MS; u < TICK + MOTE_EMBER_MS + 10000; u += TICK) {
        o = mote_step(&m, &in, u);
        EXPECT(o.look.halo_r == k.halo_r && o.look.halo_pct == k.halo_pct && o.deg == 0,
               "an ember is still: no breathing");
    }
    working(&in, 1, one);
    o = mote_step(&m, &in, TICK + MOTE_EMBER_MS + 10000);
    EXPECT(o.mode == MOTE_VISIT && o.look.core_r == MOTE_CORE_R, "a turn wakes the ember");
}

static void test_hover(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    in.errors = 1;
    in.error_deg = 120;
    int lo = 255, hi = 0, dmin = 999, dmax = -999;
    for (uint32_t t = 0; t < 3000; t += TICK) {
        mote_out_t o = mote_step(&m, &in, t);
        int d = (int)o.deg - 120;
        if (d < dmin) dmin = d;
        if (d > dmax) dmax = d;
        if (o.look.lum < lo) lo = o.look.lum;
        if (o.look.lum > hi) hi = o.look.lum;
    }
    EXPECT(dmin == -MOTE_HOVER_SWAY && dmax == MOTE_HOVER_SWAY, "sways 3 degrees either way");
    EXPECT(hi == MOTE_LUM && lo == MOTE_LUM * 60 / 100, "flickers between two levels");
    /* an error appears while it rests: it travels there, calm */
    m = (mote_t){0};
    in.errors = 0;
    mote_step(&m, &in, 0);
    in.errors = 1;
    in.error_deg = 300;
    int mv;
    bool sw;
    uint32_t t = until_at(&m, &in, TICK, 10000, 300, &mv, &sw);
    EXPECT(t && sw && mv <= 2, "to the error the short way, calm");
}

/* A big finish (issue 20261126-2de2): to the finished arc (an ordinary trip, the short way), one
 * loop round a 40 px circle inside the track, the flare, 8 s by the arc, then on to the working
 * one. */
static void test_cheer(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    const int two[] = {90, 200};
    working(&in, 2, two);
    run(&m, &in, 0, 3000, NULL);
    EXPECT(mote_step(&m, &in, 3000).deg == 90, "by the nearest working session");
    /* the session at 200 finishes; the other still works */
    const int one[] = {90};
    working(&in, 1, one);
    in.cheers = 1;
    in.fin_at = true;
    in.fin_deg = 200;
    mote_out_t o = mote_step(&m, &in, 3030);
    EXPECT(o.mode == MOTE_CHEER && o.dx == 0 && o.dy == 0,
           "a cheer: first it drifts, on the track");
    int mv;
    bool sw;
    uint32_t t = until_at(&m, &in, 3060, 12000, 200, &mv, &sw);
    EXPECT(t && sw && mv <= 3, "to the finished arc the short way, at the calm speed");
    for (int i = 0; i < 10 && !m.fin_here; i++) mote_step(&m, &in, t += TICK);
    EXPECT(m.fin_here, "there: the loop starts");
    uint32_t t0 = m.fx_ms;
    int far = 0, max_halo = 0, max_lum = 0, max_pct = 0;
    bool inside = true, still_on = true;
    for (t = t0; t < t0 + MOTE_FIN_LOOP_MS; t += TICK) {
        o = mote_step(&m, &in, t);
        EXPECT(o.mode == MOTE_CHEER && o.deg == 200, "the loop, from the finished arc");
        int d = abs(o.dx) + abs(o.dy);
        if (d > far) far = d;
        /* inside the track at 200 degrees: towards the centre, which is up and a little right */
        if (o.dy > 1) inside = false;
    }
    EXPECT(inside, "the loop is inside the track");
    EXPECT(far >= 2 * MOTE_FIN_LOOP_R - 4, "a 40 px circle");
    for (; t < t0 + MOTE_FIN_LOOP_MS + MOTE_FLARE_MS; t += TICK) {
        o = mote_step(&m, &in, t);
        if (o.mode != MOTE_CHEER || o.deg != 200 || o.dx || o.dy) still_on = false;
        if (o.look.halo_r > max_halo) max_halo = o.look.halo_r;
        if (o.look.lum > max_lum) max_lum = o.look.lum;
        if (o.look.halo_pct > max_pct) max_pct = o.look.halo_pct;
    }
    EXPECT(still_on, "the flare, in place on the track");
    EXPECT(max_halo >= (MOTE_HALO_R + MOTE_BREATH_PX) * MOTE_FLARE_PCT / 100 - 2 &&
               max_lum >= MOTE_LUM_FLARE - 5 && max_pct >= 95,
           "a bright flare, the halo 1.6 times as wide");
    _Static_assert(MOTE_FIN_LOOP_MS + MOTE_FLARE_MS == 1200, "loop and flare in 1.2 s");
    /* it lingers 8 s by the finished arc, breathing, then goes on to the working one */
    o = run(&m, &in, t, t0 + MOTE_FIN_LOOP_MS + MOTE_FLARE_MS + MOTE_FIN_LINGER_MS - TICK, NULL);
    EXPECT(o.mode == MOTE_VISIT && o.deg == 200, "lingers by the finished arc");
    t = until_at(&m, &in, t0 + 9200 + TICK, t0 + 20000, 90, NULL, NULL);
    EXPECT(t, "then on to the session still working");
}

/* A cheer with no arc, cut by an error, while gone, with reduced motion. */
static void test_cheer_edges(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    const int one[] = {90};
    working(&in, 1, one);
    uint32_t t = 5000;
    run(&m, &in, 0, t, NULL);
    mote_out_t o;

    /* the arc unknown (inside a merged group): the loop and flare where it is */
    in.cheers = 1;
    in.fin_at = false;
    o = mote_step(&m, &in, t + TICK);
    EXPECT(o.mode == MOTE_CHEER && m.fin_here && o.deg == 90, "unknown arc: at once, in place");

    /* an error wins: the cheer ends and it goes to hover */
    in.cheers = 2;
    in.fin_at = true;
    in.fin_deg = 300;
    mote_step(&m, &in, t + 2 * TICK);
    in.errors = 1;
    in.error_deg = 10;
    o = mote_step(&m, &in, t + 3 * TICK);
    EXPECT(o.mode == MOTE_HOVER && m.fx != MOTE_CHEER, "an error ends the cheer");
    in.errors = 0;
    run(&m, &in, t + 4 * TICK, t + 20000, NULL);
    t += 20000;
    /* a cheer while gone is not kept for later */
    in.face = false;
    mote_step(&m, &in, t + 4000);
    in.cheers = 3;
    mote_step(&m, &in, t + 4030);
    in.face = true;
    o = mote_step(&m, &in, t + 4060);
    EXPECT(o.mode == MOTE_VISIT, "no cheer saved up");
    /* reduced motion: no cheer */
    in.reduced = true;
    in.cheers = 4;
    o = mote_step(&m, &in, t + 4090);
    EXPECT(o.mode == MOTE_VISIT && o.deg == 90, "reduced: still, no cheer");
}

/* The flare on its own (the pairing screen uses it): up in 120 ms, back in 480 ms, the base after.
 */
static void test_flare(void)
{
    mote_look_t base = mote_look(100, 100, 0), o;
    o = mote_flare(base, 100, 0);
    EXPECT(o.halo_r == base.halo_r && o.lum == base.lum, "starts from the base");
    o = mote_flare(base, 100, MOTE_FLARE_RISE_MS);
    EXPECT(o.halo_r == base.halo_r * MOTE_FLARE_PCT / 100 && o.halo_pct == 100, "the peak");
    mote_look_t mid = mote_flare(base, 100, MOTE_FLARE_RISE_MS + MOTE_FLARE_FALL_MS / 2);
    EXPECT(mid.halo_r > base.halo_r && mid.halo_r < o.halo_r, "falling back");
    o = mote_flare(base, 100, MOTE_FLARE_MS);
    EXPECT(o.halo_r == base.halo_r && o.halo_pct == base.halo_pct, "the base once over");
}

static void test_reduced(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    in.reduced = true;
    const int two[] = {120, 240};
    working(&in, 2, two);
    mote_out_t first = mote_step(&m, &in, 0), o = first;
    for (uint32_t t = TICK; t < 30000; t += TICK) {
        o = mote_step(&m, &in, t);
        EXPECT(o.deg == 120 && o.look.halo_r == first.look.halo_r &&
                   o.look.halo_pct == first.look.halo_pct,
               "reduced: still by the first working session, no cycling, no breathing");
    }
    in.n_work = 0;
    o = mote_step(&m, &in, 30000);
    EXPECT(o.mode == MOTE_REST && o.deg == MOTE_REST_DEG, "reduced: at rest at once, no trip");
    EXPECT(o.look.halo_r == MOTE_HALO_R, "reduced: the halo exhaled, still");
}

static void test_loop(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    mote_step(&m, &in, 0);
    in.loops = 1;
    int far = 0;
    mote_out_t o;
    for (uint32_t t = 30; t < 30 + MOTE_LOOP_MS; t += TICK) {
        o = mote_step(&m, &in, t);
        EXPECT(o.mode == MOTE_LOOP && o.deg == MOTE_REST_DEG, "the loop leaves from where it is");
        int d = abs(o.dx) + abs(o.dy);
        if (d > far) far = d;
        /* at 12 o'clock inside the ring is down the screen */
        EXPECT(o.dy >= 0, "the loop is inside the track");
    }
    EXPECT(far >= 2 * MOTE_LOOP_R - 2, "a 32 px circle");
    o = mote_step(&m, &in, 30 + MOTE_LOOP_MS);
    EXPECT(o.mode == MOTE_REST && o.dx == 0 && o.dy == 0, "back on the track");
}

static void test_night(void)
{
    /* the colours that mean state (ui_theme.h): working, ask, permission, done, error, idle */
    static const uint32_t s_state_rgb[] = {0x56b4e9, 0xf0e442, 0xe69f00,
                                           0x009e73, 0xd55e00, 0x999999};
    mote_t m = {0};
    mote_in_t in = calm();
    in.reduced = true; /* the look alone, without the breath */
    mote_out_t day = mote_step(&m, &in, 0);
    in.hour = 23;
    mote_out_t o = mote_step(&m, &in, 0);
    EXPECT(o.night && o.look.lum == MOTE_LUM_NIGHT && o.look.lum < day.look.lum,
           "after 23:00: dimmer");
    EXPECT(o.look.halo_pct < day.look.halo_pct, "and a dimmer halo");
    EXPECT(o.rgb == day.rgb && o.rgb == MOTE_RGB, "the same warm white, not amber");
    for (size_t i = 0; i < sizeof s_state_rgb / sizeof s_state_rgb[0]; i++) {
        EXPECT(o.rgb != s_state_rgb[i] && day.rgb != s_state_rgb[i], "never a state colour");
    }
    in.hour = 5;
    EXPECT(mote_step(&m, &in, 0).night, "until 06:00");
    in.hour = 6;
    EXPECT(!mote_step(&m, &in, 0).night, "day from 06:00");
}

/* The level is counted (test_level) but never shown: mote_in_t does not carry it, so the look
 * never changes under the user. */
static void test_tired(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    /* tired: slower, dimmer breathing */
    int fresh = 0, tired = 0, pct_fresh = 0, pct_tired = 0;
    m = (mote_t){0};
    mote_out_t a = mote_step(&m, &in, 0), b;
    for (uint32_t t = TICK; t < 12000; t += TICK) {
        b = mote_step(&m, &in, t);
        if (b.look.halo_r != a.look.halo_r) fresh++;
        if (b.look.halo_pct > pct_fresh) pct_fresh = b.look.halo_pct;
        a = b;
    }
    in.usage = 85;
    m = (mote_t){0};
    a = mote_step(&m, &in, 0);
    for (uint32_t t = TICK; t < 12000; t += TICK) {
        b = mote_step(&m, &in, t);
        if (b.look.halo_r != a.look.halo_r) tired++;
        if (b.look.halo_pct > pct_tired) pct_tired = b.look.halo_pct;
        a = b;
    }
    EXPECT(tired < fresh, "tired (80 % or more): a slower breath");
    EXPECT(pct_tired < pct_fresh, "and a dimmer one");
}

static void test_look(void)
{
    mote_look_t k = mote_look(100, 45, 0);
    EXPECT(k.core_r == 7 && k.halo_r == 26 && k.lum == 45, "the idle face's firefly");
    mote_look_t big = mote_look(200, 80, 1000);
    EXPECT(big.core_r == 14 && big.halo_r == 60, "any size: twice as large");
    EXPECT(big.halo_pct == MOTE_HALO_PCT && k.halo_pct == MOTE_HALO_PCT * MOTE_BREATH_LO / 100,
           "inhaled: the halo at its peak; exhaled: 70 % of it");
    mote_look_t tiny = mote_look(5, 10, 0);
    EXPECT(tiny.core_r >= 1 && tiny.halo_r > tiny.core_r, "never vanishes");
    EXPECT(mote_breath(0, MOTE_BREATH_MS) == 0, "a breath: out at 0");
    EXPECT(mote_breath(MOTE_BREATH_MS / 2, MOTE_BREATH_MS) == 1000, "in at half the period");
    EXPECT(mote_breath(123, 0) == 0, "no period: still");
}

static void test_level(void)
{
    EXPECT(mote_level(0) == 0 && mote_level(1) == 1 && mote_level(2) == 1, "first day: level 1");
    EXPECT(mote_level(3) == 2 && mote_level(7) == 3 && mote_level(14) == 4, "3, 7, 14");
    EXPECT(mote_level(30) == 5 && mote_level(60) == 6 && mote_level(120) == 7, "30, 60, 120");
    EXPECT(mote_level(100000) == MOTE_LEVEL_MAX, "never past 7");
    /* 200 days with gaps: counted once a day, never less */
    uint32_t w = 0, days = 0;
    uint8_t lvl = 0;
    uint32_t seed = 7;
    for (uint32_t day = 20000; day < 20200; day++) {
        seed = seed * 1103515245u + 12345u;             /* a fixed walk of days off and busy days */
        if (seed >> 16 & 1 && seed >> 17 & 1) continue; /* a day off: nothing lost */
        int finishes = 1 + (int)(seed >> 20 & 3);
        for (int k = 0; k < finishes; k++) w = mote_note_day(w, day);
        days++;
        EXPECT(mote_days(w) == days, "one count a day however many finishes");
        EXPECT(mote_level(mote_days(w)) >= lvl, "the level never falls");
        lvl = mote_level(mote_days(w));
    }
    EXPECT(mote_note_day(w, 20100) == w, "a clock set back counts nothing");
    EXPECT(mote_note_day(w, 0) == w, "no clock counts nothing");
}

static void test_finish(void)
{
    mote_finish_t f = {.error = false, .tests = 1, .secs = 40, .files = 1, .lines = 10};
    EXPECT(mote_finish_green(&f) && !mote_finish_big(&f), "green, small");
    f.tests = -1;
    EXPECT(!mote_finish_green(&f), "no verdict: not green");
    f.secs = 600;
    EXPECT(mote_finish_big(&f), "ten minutes: big");
    f.tests = 0;
    EXPECT(!mote_finish_big(&f), "failed tests never cheer");
    f = (mote_finish_t){.tests = -1, .secs = -1, .files = 5, .lines = -1};
    EXPECT(mote_finish_big(&f), "five files: big");
    f = (mote_finish_t){.tests = -1, .secs = -1, .files = -1, .lines = 200};
    EXPECT(mote_finish_big(&f), "200 lines: big");
    f.error = true;
    EXPECT(!mote_finish_big(&f) && !mote_finish_green(&f), "an error is neither");
}

int main(void)
{
    test_priority();
    test_visit_cycles();
    test_visit_changes();
    test_rest_and_ember();
    test_hover();
    test_cheer();
    test_cheer_edges();
    test_flare();
    test_reduced();
    test_loop();
    test_night();
    test_tired();
    test_look();
    test_level();
    test_finish();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("test_mote: ok\n");
    return 0;
}
