/* mote.c: the Mote's state machine (issue 20261126-59e0). */
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

static int ang_diff(int a, int b)
{
    int d = ((a - b) % 360 + 360) % 360;
    return d > 180 ? 360 - d : d;
}

/* Runs from t0 to t1 in ticks, with minute_ms following the clock; returns the last output and
 * the largest move between two ticks (degrees). */
static mote_out_t run(mote_t *m, mote_in_t *in, uint32_t t0, uint32_t t1, int *max_move)
{
    mote_out_t o = {0}, prev = {0};
    bool first = true;
    if (max_move) *max_move = 0;
    for (uint32_t t = t0; t <= t1; t += TICK) {
        in->minute_ms = t % 60000u;
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

static void test_priority(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    mote_out_t o = mote_step(&m, &in, 1000);
    EXPECT(o.mode == MOTE_SETTLE && o.deg == 180, "nothing runs: settled at 6 o'clock");
    EXPECT(o.size == MOTE_HEAD_PX && o.lum <= MOTE_LUM, "an 8 px head at the ambient brightness");
    in.working = 2;
    o = mote_step(&m, &in, 1030);
    EXPECT(o.mode == MOTE_LAP, "something works: a lap");
    in.errors = 1;
    in.error_deg = 90;
    o = mote_step(&m, &in, 1060);
    EXPECT(o.mode == MOTE_HOVER, "an error outranks the lap");
    in.may = false;
    o = mote_step(&m, &in, 1090);
    EXPECT(o.mode == MOTE_GONE, "not allowed: gone in the same step");
    in.may = true;
    in.face = false;
    o = mote_step(&m, &in, 1120);
    EXPECT(o.mode == MOTE_GONE, "no idle face: gone");
}

static void test_lap_is_a_seconds_hand(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    in.working = 1;
    for (uint32_t s = 0; s < 60; s += 7) {
        in.minute_ms = s * 1000;
        mote_out_t o = mote_step(&m, &in, 100000 + s * 1000);
        EXPECT(o.deg == s * 6, "appearing in a lap: angle = seconds x 6");
        m = (mote_t){0};
    }
    /* following the clock: one degree at a time */
    int mv;
    mote_out_t o = run(&m, &in, 0, 60000, &mv);
    EXPECT(mv <= 1, "a lap moves a degree at a time");
    EXPECT(o.deg == 0, "on the minute: at 12");
}

static void test_settle_short_way(void)
{
    /* from either side of 6 o'clock, and from near it */
    const int starts[] = {30, 330, 170, 190};
    for (int i = 0; i < 4; i++) {
        mote_t m = {0};
        mote_in_t in = calm();
        in.working = 1;
        uint32_t t0 = (uint32_t)starts[i] * 1000 / 6; /* the second at that angle */
        in.minute_ms = t0;
        mote_out_t o = mote_step(&m, &in, t0);
        EXPECT(o.deg == starts[i], "starts where the seconds are");
        in.working = 0;
        int prev = o.deg, mv = 0;
        bool wrong = false;
        for (uint32_t t = t0 + TICK; t < t0 + 40000; t += TICK) {
            o = mote_step(&m, &in, t);
            int d = ang_diff(o.deg, prev);
            if (d > mv) mv = d;
            /* the distance to 180 never grows: the short way */
            if (ang_diff(o.deg, 180) > ang_diff(prev, 180)) wrong = true;
            prev = o.deg;
        }
        EXPECT(o.deg == 180 && o.mode == MOTE_SETTLE, "settles at 180");
        EXPECT(!wrong, "the short way");
        EXPECT(mv <= 1, "settling is slow (6 degrees a second)");
    }
}

static void test_ember(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    mote_out_t o = run(&m, &in, 1000, 1000 + MOTE_EMBER_MS - TICK, NULL);
    EXPECT(o.mode == MOTE_SETTLE, "settled under five minutes");
    o = mote_step(&m, &in, 1000 + MOTE_EMBER_MS);
    EXPECT(o.mode == MOTE_EMBER && o.size == MOTE_EMBER_PX && o.lum == MOTE_LUM_EMBER,
           "an ember after exactly five minutes");
    EXPECT(o.tail_deg == 0, "an ember has no tail");
    /* the ember does not pulse */
    uint8_t lum = o.lum;
    for (uint32_t t = 1000 + MOTE_EMBER_MS; t < 1000 + MOTE_EMBER_MS + 10000; t += TICK) {
        o = mote_step(&m, &in, t);
        EXPECT(o.lum == lum, "an ember is still");
    }
    /* a turn starts: back to a lap */
    in.working = 1;
    o = mote_step(&m, &in, 1000 + MOTE_EMBER_MS + 10000);
    EXPECT(o.mode == MOTE_LAP && o.size == MOTE_HEAD_PX, "a turn wakes the ember");
}

static void test_pulse(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    int lo = 255, hi = 0;
    for (uint32_t t = 0; t < 8000; t += TICK) {
        mote_out_t o = mote_step(&m, &in, t);
        if (o.lum < lo) lo = o.lum;
        if (o.lum > hi) hi = o.lum;
    }
    EXPECT(hi == MOTE_LUM && lo < MOTE_LUM && lo >= MOTE_LUM * 70 / 100, "settled: a pulse");
}

static void test_speed(void)
{
    /* entering a lap from 6 o'clock catches up, never faster than 24 degrees a second */
    mote_t m = {0};
    mote_in_t in = calm();
    in.minute_ms = 0;
    mote_step(&m, &in, 0);
    in.working = 1;
    int mv;
    mote_out_t o = run(&m, &in, 15000, 45000, &mv);
    EXPECT(mv <= 1, "catching up: at most 24 degrees a second (under a degree a tick)");
    EXPECT(o.deg == 45000 % 60000 * 6 / 1000, "caught up with the seconds");
    /* the clock is set 20 s on: it catches up, it does not jump */
    int prev = o.deg;
    for (uint32_t t = 45030; t < 57030; t += TICK) {
        in.minute_ms = (t + 20000) % 60000u;
        o = mote_step(&m, &in, t);
        EXPECT(ang_diff(o.deg, prev) <= 1, "a clock set does not make it jump");
        prev = o.deg;
    }
    EXPECT(o.deg == (57000 + 20000) % 60000 * 6 / 1000, "caught up with the new time");
    in.minute_ms = 45000;
    m = (mote_t){0};
    mote_step(&m, &in, 45000);
    /* to an error and back */
    in.errors = 1;
    in.error_deg = 300;
    o = run(&m, &in, 45030, 60000, &mv);
    EXPECT(mv <= 1, "to the error at catch-up speed");
    EXPECT(ang_diff(o.deg, 300) <= MOTE_HOVER_SWAY, "hovers at the error, swaying 3 degrees");
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
        if (o.lum < lo) lo = o.lum;
        if (o.lum > hi) hi = o.lum;
    }
    EXPECT(dmin == -MOTE_HOVER_SWAY && dmax == MOTE_HOVER_SWAY, "sways 3 degrees either way");
    EXPECT(hi == MOTE_LUM && lo == MOTE_LUM * 60 / 100, "two brightness levels");
}

static void test_cheer(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    in.working = 1;
    run(&m, &in, 0, 3000, NULL);
    in.cheers = 1;
    int laps = 0, prev = -1;
    mote_out_t o;
    uint32_t t;
    for (t = 3030; t < 3030 + MOTE_CHEER_MS; t += TICK) {
        in.minute_ms = t;
        o = mote_step(&m, &in, t);
        EXPECT(o.mode == MOTE_CHEER && o.tail_deg == MOTE_CHEER_TAIL, "cheer: a long tail");
        if (prev >= 0 && o.deg < prev) laps++;
        prev = o.deg;
    }
    EXPECT(laps == 3 || laps == 2, "three laps (crossing 12 two or three times)");
    in.minute_ms = t + 30;
    o = mote_step(&m, &in, t + 30);
    EXPECT(o.mode == MOTE_CHEER && o.size == MOTE_CHEER_PX, "one large frame at the end");
    o = run(&m, &in, t + 150, t + 3000, NULL);
    EXPECT(o.mode == MOTE_LAP, "then on");
    /* a cheer while gone is not kept for later */
    in.face = false;
    mote_step(&m, &in, t + 4000);
    in.cheers = 2;
    mote_step(&m, &in, t + 4030);
    in.face = true;
    o = mote_step(&m, &in, t + 4060);
    EXPECT(o.mode == MOTE_LAP, "no cheer saved up");
    /* reduced motion: no cheer */
    in.reduced = true;
    in.cheers = 3;
    o = mote_step(&m, &in, t + 4090);
    EXPECT(o.mode == MOTE_LAP && o.deg == 0, "reduced: still at its resting place, no cheer");
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
        EXPECT(o.mode == MOTE_LOOP && o.deg == 180, "the loop leaves from where it is");
        int d = abs(o.dx) + abs(o.dy);
        if (d > far) far = d;
        /* at 6 o'clock inside the ring is up the screen */
        EXPECT(o.dy <= 0, "the loop is inside the ring");
    }
    EXPECT(far >= 2 * MOTE_LOOP_R - 2, "a 40 px circle");
    o = mote_step(&m, &in, 30 + MOTE_LOOP_MS);
    EXPECT(o.mode == MOTE_SETTLE && o.dx == 0 && o.dy == 0, "back on the track");
}

static void test_tail_and_night(void)
{
    mote_t m = {0};
    mote_in_t in = calm();
    for (uint8_t l = 0; l <= MOTE_LEVEL_MAX; l++) {
        in.level = l;
        mote_out_t o = mote_step(&m, &in, 0);
        EXPECT(o.tail_deg == MOTE_TAIL_DEG + MOTE_TAIL_STEP * l, "tail 14 + 6 per level");
    }
    in.level = 7;
    in.usage = 85;
    EXPECT(mote_step(&m, &in, 0).tail_deg == 28, "tired: the tail halves");
    in.level = 0;
    EXPECT(mote_step(&m, &in, 0).tail_deg == MOTE_TAIL_MIN, "tired: 8 degrees at least");
    in.usage = 79;
    EXPECT(mote_step(&m, &in, 0).tail_deg == MOTE_TAIL_DEG, "under 80 %: as it is");
    in.hour = 23;
    mote_out_t o = mote_step(&m, &in, 0);
    EXPECT(o.amber && o.lum <= MOTE_LUM_NIGHT, "after 23:00: amber and dimmer");
    in.hour = 5;
    EXPECT(mote_step(&m, &in, 0).amber, "until 06:00");
    in.hour = 6;
    EXPECT(!mote_step(&m, &in, 0).amber, "day from 06:00");
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
    test_lap_is_a_seconds_hand();
    test_settle_short_way();
    test_ember();
    test_pulse();
    test_speed();
    test_hover();
    test_cheer();
    test_loop();
    test_tail_and_night();
    test_level();
    test_finish();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("test_mote: ok\n");
    return 0;
}
