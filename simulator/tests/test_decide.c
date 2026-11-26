/* Decision toys (decide.c, toy_rand.c, toy_motion.c; EXPERIMENTS.md "Toys", issue 20261126-74ee):
 * fixed seeds give fixed throws, the dice and the coin are flat, the timelines land where they
 * say, a pause freezes them, and a flick and a still hold are found whatever the axes' signs. */
#include <stdio.h>
#include <stdlib.h>

#include "decide.h"
#include "toy_motion.h"
#include "toy_rand.h"

static int failures;
#define EXPECT(cond, msg)                                  \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

static void test_rand(void)
{
    toy_rand_t a, b;
    toy_rand_seed(&a, 42);
    toy_rand_seed(&b, 42);
    bool same = true;
    for (int i = 0; i < 100; i++) same &= toy_rand_next(&a) == toy_rand_next(&b);
    EXPECT(same, "the same seed gives the same sequence");
    toy_rand_seed(&a, 0);
    EXPECT(toy_rand_next(&a) != 0, "seed 0 still runs");
    toy_rand_stir(&a, 0);
    EXPECT(a.s != 0, "stirring never leaves the state at 0");
    EXPECT(toy_rand_below(&a, 0) == 0 && toy_rand_below(&a, 1) == 0, "empty and one-value ranges");

    /* 60 000 rolls of each die near flat: within 3 % of the expected count, or 4.5 standard
     * deviations where 3 % is under that (a d20's 3000 a face); the coin within 1 % */
    static const int dice[] = {4, 6, 8, 12, 20};
    toy_rand_seed(&a, 7);
    for (int k = 0; k < 5; k++) {
        int n = dice[k], count[20] = {0};
        for (int i = 0; i < 60000; i++) count[toy_rand_below(&a, (uint32_t)n)]++;
        int expect = 60000 / n, worst = 0;
        for (int v = 0; v < n; v++) {
            int off = abs(count[v] - expect);
            if (off > worst) worst = off;
        }
        char msg[64];
        snprintf(msg, sizeof msg, "a d%d stays within 3 %% of flat", n);
        EXPECT(worst * 100 <= expect * 3 || worst * worst <= 20 * expect, msg);
    }
    int heads = 0;
    for (int i = 0; i < 60000; i++) heads += (int)toy_rand_below(&a, 2);
    EXPECT(abs(heads - 30000) * 100 <= 30000, "the coin within 1 %");
}

static void test_coin(void)
{
    decide_t d, e;
    decide_init(&d, 1234);
    decide_init(&e, 1234);
    EXPECT(d.kind == DECIDE_COIN && d.phase == DECIDE_READY, "the coin is up first, unthrown");
    EXPECT(decide_throw(&d, 1000, 99) && decide_throw(&e, 1000, 99), "throw");
    EXPECT(d.coin_face == e.coin_face, "a fixed seed and edge give a fixed face");
    EXPECT(!decide_throw(&d, 1100, 5), "no second throw while it turns");
    EXPECT(!decide_next_kind(&d), "nor another toy");
    EXPECT(decide_coin_deg(&d, 1000) == 0, "it starts square on");
    int turns = 0, last = 0;
    bool slowing = true;
    for (uint32_t t = 1000; t < 2000; t += 10) {
        unsigned ev = decide_step(&d, t);
        if (ev & DECIDE_EV_TURN) turns++;
        EXPECT(!(ev & DECIDE_EV_LAND), "no landing before DECIDE_COIN_MS");
        int deg = decide_coin_deg(&d, t);
        if (t > 1500 && deg - last > 30) slowing = false;
        last = deg;
    }
    EXPECT(slowing, "it slows toward the end");
    EXPECT(turns == d.coin_half, "one turn per half-turn");
    EXPECT(decide_step(&d, 2000) == DECIDE_EV_LAND, "lands at DECIDE_COIN_MS");
    EXPECT(decide_coin_face_at(&d, decide_coin_deg(&d, 2000)) == d.coin_face, "on the face drawn");
    EXPECT(d.phase == DECIDE_SHOWN && d.shown[DECIDE_COIN], "the face is up");
    /* the face just before landing is the result too: the turn count matches it */
    int at_end = d.coin_from ^ (d.coin_half & 1);
    EXPECT(at_end == d.coin_face, "the half-turns carry it onto the result");

    /* both faces come up over many throws */
    int seen[2] = {0, 0};
    uint32_t t = 10000;
    for (int i = 0; i < 40; i++, t += 2000) {
        decide_throw(&d, t, t);
        decide_step(&d, t + 1500);
        seen[d.coin_face]++;
    }
    EXPECT(seen[0] && seen[1], "both faces land");
}

static void test_die(void)
{
    decide_t d;
    decide_init(&d, 77);
    decide_next_kind(&d);
    EXPECT(d.kind == DECIDE_DIE && decide_sides(&d) == 6, "the die, six sides");
    EXPECT(decide_step_die(&d, -5) && decide_sides(&d) == 4, "turned far down: 4 sides");
    EXPECT(!decide_step_die(&d, -1), "past the end it stays put");
    EXPECT(decide_step_die(&d, 2) && decide_sides(&d) == 8, "the bezel picks 8");
    for (int i = 0; i < 6; i++) decide_step_die(&d, 1);
    EXPECT(decide_sides(&d) == 20, "up to 20");
    for (int i = 0; i < 6; i++) decide_step_die(&d, -1);
    EXPECT(decide_sides(&d) == 4, "down to 4");
    decide_step_die(&d, 4);
    EXPECT(decide_die_shown(&d, 0) == 0, "nothing shown before a throw");

    EXPECT(decide_throw(&d, 0, 3), "throw the d20");
    EXPECT(!decide_step_die(&d, -1), "the die stays while it rattles");
    int changes = 0, prev = 0;
    bool in_range = true;
    uint32_t land = 0;
    for (uint32_t t = 0; t < 3000 && !land; t += 5) {
        unsigned ev = decide_step(&d, t);
        int v = decide_die_shown(&d, t);
        in_range &= v >= 1 && v <= 20;
        if (ev & DECIDE_EV_TURN) {
            changes++;
            EXPECT(v != prev, "each rattle shows another number");
            prev = v;
        }
        if (ev & DECIDE_EV_LAND) land = t;
    }
    EXPECT(in_range, "every number is a face of the die");
    EXPECT(changes == DECIDE_RATTLES, "nine rattles");
    /* 60 + ... + 240 in nine equal steps: 9 x 150 */
    EXPECT(land >= 1350 && land < 1360, "lands after the nine growing intervals");
    EXPECT(decide_die_shown(&d, land) == d.die_value && d.die_value != prev,
           "the result, not the last rattle");
    decide_step_die(&d, -1);
    EXPECT(decide_die_shown(&d, land) == 0, "a new die clears the number");

    /* the result is drawn on the throw and is flat */
    int count[6] = {0};
    decide_init(&d, 5);
    decide_next_kind(&d);
    uint32_t t = 0;
    for (int i = 0; i < 6000; i++, t += 2000) {
        decide_throw(&d, t, (uint32_t)i);
        decide_step(&d, t + 1999);
        count[d.die_value - 1]++;
    }
    bool flat = true;
    for (int v = 0; v < 6; v++) flat &= abs(count[v] - 1000) < 100;
    EXPECT(flat, "the d6 lands flat over 6000 throws");
}

static void test_oracle(void)
{
    decide_t d;
    decide_init(&d, 9);
    decide_next_kind(&d);
    decide_next_kind(&d);
    EXPECT(d.kind == DECIDE_ORACLE, "the oracle third");
    EXPECT(!decide_throw(&d, 0, 0), "the oracle is held, not thrown");
    EXPECT(decide_hold(&d, true, 100, 1) && d.phase == DECIDE_HOLDING, "a hold begins");
    EXPECT(decide_hold_permille(&d, 1100) == 500, "half way at 1 s");
    EXPECT(decide_hold(&d, false, 0, 0) && d.phase == DECIDE_READY, "let go early: nothing");
    EXPECT(decide_step(&d, 5000) == 0, "and no answer later");
    decide_hold(&d, true, 6000, 2);
    EXPECT(decide_step(&d, 7999) == 0, "not before 2 s");
    EXPECT(decide_step(&d, 8000) == DECIDE_EV_LAND, "answers at 2 s");
    EXPECT(d.answer < DECIDE_ANSWERS && d.shown[DECIDE_ORACLE], "one of the six");
    EXPECT(decide_rise_px(&d, 8000) == DECIDE_RISE_PX, "it starts below its place");
    EXPECT(decide_rise_px(&d, 8000 + DECIDE_RISE_MS) == 0, "and rises into place");
    decide_finish(&d, 8001);
    EXPECT(decide_rise_px(&d, 8001) == 0, "reduced motion: in place at once");
    /* a hold that began in the past (the watch held still for a while) */
    decide_hold(&d, true, 9000, 3);
    EXPECT(decide_step(&d, 11000) == DECIDE_EV_LAND, "a hold dated back answers on time");
    /* every answer comes up */
    int seen[DECIDE_ANSWERS] = {0};
    uint32_t t = 20000;
    for (int i = 0; i < 300; i++, t += 3000) {
        decide_hold(&d, true, t, t);
        decide_step(&d, t + DECIDE_HOLD_MS);
        seen[d.answer]++;
    }
    bool all = true;
    for (int i = 0; i < DECIDE_ANSWERS; i++) all &= seen[i] > 0;
    EXPECT(all, "all six answers come up");
}

static void test_pause(void)
{
    decide_t d;
    decide_init(&d, 31);
    decide_next_kind(&d); /* the die */
    decide_throw(&d, 1000, 0);
    decide_step(&d, 1500);
    int shown = decide_die_shown(&d, 1500);
    decide_pause(&d, 1500);
    EXPECT(decide_step(&d, 9000) == 0, "paused: nothing lands");
    EXPECT(decide_die_shown(&d, 9000) == shown, "the frame is frozen");
    EXPECT(!decide_throw(&d, 9000, 0) && !decide_next_kind(&d), "nor throws or changes");
    decide_resume(&d, 20000);
    EXPECT(decide_die_shown(&d, 20000) == shown, "resumed where it was");
    unsigned ev = 0;
    for (uint32_t t = 20000; t < 21000; t += 10) ev |= decide_step(&d, t);
    EXPECT((ev & DECIDE_EV_LAND) && d.phase == DECIDE_SHOWN, "and lands after the rest of it");

    /* a hold is dropped by a takeover */
    decide_next_kind(&d);
    decide_hold(&d, true, 30000, 0);
    decide_pause(&d, 31000);
    decide_resume(&d, 40000);
    EXPECT(d.phase == DECIDE_READY && decide_step(&d, 45000) == 0, "the hold ended");

    /* reduced motion: the end frame at once, the landing still reported */
    decide_next_kind(&d); /* the coin */
    decide_throw(&d, 50000, 1);
    decide_finish(&d, 50000);
    EXPECT(decide_step(&d, 50000) == DECIDE_EV_LAND, "finish lands on the next step");
}

static void test_motion(void)
{
    toy_motion_t m;
    toy_motion_reset(&m);
    uint32_t t = 0;
    EXPECT(!toy_motion_feed(&m, 0, 0, 1000, t), "the first sample is no jolt");
    for (int i = 0; i < 30; i++) toy_motion_feed(&m, 0, 0, 1000, t += 100);
    EXPECT(toy_motion_still_ms(&m, t) == 0, "left on the desk: never a hold by itself");
    /* a flick either way along x, and with x and y swapped: all jolts */
    static const int flick[4][2] = {{600, 0}, {-600, 0}, {0, 600}, {0, -600}};
    for (int k = 0; k < 4; k++) {
        t += 1000;
        bool j = toy_motion_feed(&m, flick[k][0], flick[k][1], 700, t);
        EXPECT(j, "a flick is a jolt whatever its direction");
        EXPECT(!toy_motion_feed(&m, 0, 0, 1000, t + 100), "the swing back is the same shake");
    }
    t += 1000;
    EXPECT(!toy_motion_feed(&m, 100, 100, 1000, t), "a small move is no jolt");
    /* armed by the moves: now still and face up */
    toy_motion_feed(&m, 0, 0, 1000, t += 100);
    uint32_t from = t + 100; /* the first still sample */
    for (int i = 0; i < 25; i++) toy_motion_feed(&m, 30, -20, 990, t += 100);
    EXPECT(toy_motion_still_ms(&m, t) == t - from, "held still face up: counts");
    toy_motion_feed(&m, 0, -1000, 0, t += 100);
    EXPECT(toy_motion_still_ms(&m, t) == 0, "turned upright: not face up");
    toy_motion_rearm(&m);
    for (int i = 0; i < 25; i++) toy_motion_feed(&m, 0, 0, 1000, t += 100);
    EXPECT(toy_motion_still_ms(&m, t) > 0, "laid down face up is a move, then still");
    toy_motion_rearm(&m);
    for (int i = 0; i < 25; i++) toy_motion_feed(&m, 0, 0, 1000, t += 100);
    EXPECT(toy_motion_still_ms(&m, t) == 0, "after a hold is used it needs a move again");
}

int main(void)
{
    test_rand();
    test_coin();
    test_die();
    test_oracle();
    test_pause();
    test_motion();
    if (!failures) printf("decide tests passed\n");
    return failures ? 1 : 0;
}
