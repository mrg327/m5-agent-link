/* nag.c (issue 20261126-a909): reminders that grow louder. The schedule scaled to the answer
 * window, the 5 s tail, one ladder for the most urgent request, the 10 s floor between any two
 * reminders, input ending the ladders, and clocks that wrap. */
#include <stdio.h>
#include <stdlib.h>

#include "nag.h"

static int g_fail;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail = 1;                                                  \
        }                                                                \
    } while (0)

/* The plan for a window, as seconds, with -1 after the last step. */
static void plan_is(uint32_t win_ms, int s0, int s1, int s2)
{
    uint32_t t[NAG_STEPS];
    int want[NAG_STEPS] = {s0, s1, s2};
    int n = nag_plan(win_ms, t);
    int want_n = 0;
    while (want_n < NAG_STEPS && want[want_n] >= 0) want_n++;
    if (n != want_n) {
        fprintf(stderr, "window %u ms: %d steps, want %d\n", (unsigned)win_ms, n, want_n);
    }
    CHECK(n == want_n);
    for (int i = 0; i < n && i < want_n; i++) {
        if (t[i] != (uint32_t)want[i] * 1000u) {
            fprintf(stderr, "window %u ms: step %d at %u ms, want %d s\n", (unsigned)win_ms, i,
                    (unsigned)t[i], want[i]);
        }
        CHECK(t[i] == (uint32_t)want[i] * 1000u);
    }
}

static void schedule(void)
{
    plan_is(60000, 25, 40, 50);
    plan_is(120000, 25, 60, 110);
    plan_is(300000, 25, 150, 290);
    plan_is(0, 25, 40, 55); /* no window (a daemon before 0.11) */
    /* a window under 40 s: R1 only, at its half; under 20 s none */
    uint32_t t[NAG_STEPS];
    CHECK(nag_plan(27000, t) == 1 && t[0] == 13500);
    CHECK(nag_plan(20000, t) == 1 && t[0] == 10000);
    CHECK(nag_plan(19999, t) == 0);
    CHECK(nag_plan(14000, t) == 0);
    /* nothing in the last 5 s: 45 s keeps R1 only (R2 at 40 s would be the tail) */
    plan_is(45000, 25, -1, -1);
    plan_is(40000, 25, -1, -1);
}

static nag_req_t req(uint32_t rid, uint32_t start, uint32_t win, uint32_t now)
{
    nag_req_t r = {.rid = rid, .start_ms = start, .arrived_ms = start, .win_ms = win};
    if (win) {
        int32_t left = (int32_t)(start + win - now);
        r.left_ms = left > 0 ? (uint32_t)left : 0;
    } else {
        r.left_ms = UINT32_MAX;
    }
    return r;
}

/* Run one request from base, a step every 100 ms; record when each level fired (ms after base). */
static void ladder(uint32_t base, uint32_t win, uint32_t at[NAG_STEPS + 1])
{
    nag_t n;
    nag_reset(&n);
    for (int i = 0; i <= NAG_STEPS; i++) at[i] = 0;
    for (uint32_t t = 0; t <= (win ? win : 70000); t += 100) {
        nag_req_t r = req(7, base, win, base + t);
        uint32_t rid = 0;
        int level = nag_step(&n, &r, 1, base + t, &rid);
        if (level) {
            CHECK(rid == 7);
            CHECK(level >= 1 && level <= NAG_STEPS && !at[level]);
            at[level] = t;
        }
    }
}

static void one_request(void)
{
    uint32_t at[NAG_STEPS + 1];
    ladder(1000, 60000, at);
    CHECK(at[1] == 25000 && at[2] == 40000 && at[3] == 50000);
    /* wrap-safe: the same across the 32-bit clock's wrap */
    ladder(0xFFFFFFFFu - 30000u, 60000, at);
    CHECK(at[1] == 25000 && at[2] == 40000 && at[3] == 50000);
    ladder(1000, 0, at);
    CHECK(at[1] == 25000 && at[2] == 40000 && at[3] == 55000);
    ladder(1000, 14000, at);
    CHECK(!at[1] && !at[2] && !at[3]);
}

static void input_stops(void)
{
    nag_t n;
    nag_reset(&n);
    nag_req_t r = req(7, 1000, 60000, 1000);
    CHECK(nag_step(&n, &r, 1, 1000, NULL) == 0);
    nag_note_input(&n, 5000); /* the user touched after it arrived */
    int fired = 0;
    for (uint32_t t = 5000; t <= 61000; t += 100) {
        r = req(7, 1000, 60000, t);
        fired += nag_step(&n, &r, 1, t, NULL) != 0;
    }
    CHECK(fired == 0);
    /* a request that arrives after the input still gets its own ladder */
    nag_req_t b = req(8, 6000, 60000, 31000);
    uint32_t rid = 0;
    CHECK(nag_step(&n, &b, 1, 31000, &rid) == 1 && rid == 8);
}

/* Two requests: the one with less time left is reminded; when it is answered the other starts its
 * own ladder, past steps skipped, never within 10 s of the last reminder. */
static void two_requests(void)
{
    nag_t n;
    nag_reset(&n);
    uint32_t rid = 0;
    /* 7: 120 s window from 0; 8: 60 s window from 2 s (less time left) */
    nag_req_t rs[2];
    int got[4] = {0};
    for (uint32_t t = 0; t <= 45000; t += 100) {
        rs[0] = req(7, 0, 120000, t);
        rs[1] = req(8, 2000, 60000, t);
        int level = nag_step(&n, rs, 2, t, &rid);
        if (level) {
            CHECK(rid == 8); /* only the most urgent */
            got[level] = (int)t;
        }
    }
    CHECK(got[1] == 27000 && got[2] == 42000 && !got[3]);
    /* 8 is answered at 45 s: 7's R1 (25 s) is past and skipped; its R2 is at 60 s */
    int fired_at = 0, level_at = 0;
    for (uint32_t t = 45100; t <= 70000; t += 100) {
        rs[0] = req(7, 0, 120000, t);
        int level = nag_step(&n, rs, 1, t, &rid);
        if (level && !fired_at) {
            fired_at = (int)t;
            level_at = level;
            CHECK(rid == 7);
        }
    }
    CHECK(fired_at == 60000 && level_at == 2);
}

static void floor_between_requests(void)
{
    nag_t n;
    nag_reset(&n);
    uint32_t rid = 0;
    /* 8 (30 s window from 0) reminds at 15 s; 7 (no window, from 0) would be due at 25 s too */
    nag_req_t rs[2];
    for (uint32_t t = 0; t <= 15000; t += 100) {
        rs[0] = req(8, 0, 30000, t);
        if (nag_step(&n, rs, 1, t, &rid)) CHECK(t == 15000 && rid == 8);
    }
    /* 8 is answered at 16 s; 7's R1 at 25 s is still ahead and 10 s after 8's: on time */
    int fired_at = 0;
    for (uint32_t t = 16000; t <= 30000; t += 100) {
        rs[0] = req(7, 0, 0, t);
        if (nag_step(&n, rs, 1, t, &rid) && !fired_at) fired_at = (int)t;
    }
    CHECK(fired_at == 25000);
    /* the floor holds a step that falls within 10 s of another request's reminder */
    nag_reset(&n);
    for (uint32_t t = 0; t <= 25000; t += 100) {
        rs[0] = req(7, 0, 0, t);
        (void)nag_step(&n, rs, 1, t, &rid); /* 7's R1 at 25 s */
    }
    fired_at = 0;
    for (uint32_t t = 25100; t <= 40000; t += 100) {
        rs[0] = req(7, 0, 0, t);
        rs[1] = req(9, 3000, 60000, t); /* less time left: its ladder, R1 due at 28 s */
        if (nag_step(&n, rs, 2, t, &rid) && !fired_at) {
            fired_at = (int)t;
            CHECK(rid == 9);
        }
    }
    CHECK(fired_at == 35000); /* its R1 (28 s) waits for 10 s after 7's (25 s) */
}

/* A step due after the floor fires on time; one in the window's last 5 s is dropped. */
static void floor_waits(void)
{
    nag_t n;
    nag_reset(&n);
    uint32_t rid = 0;
    nag_req_t rs[2];
    /* 7 (no window, from 0): R1 at 25 s */
    for (uint32_t t = 0; t <= 25000; t += 100) {
        rs[0] = req(7, 0, 0, t);
        (void)nag_step(&n, rs, 1, t, &rid);
    }
    /* 9 arrives at 25.1 s with a 60 s window: R1 due at 50.1 s; 7 is answered then */
    int fired_at = 0;
    for (uint32_t t = 25100; t <= 52000; t += 100) {
        rs[0] = req(9, 25100, 60000, t);
        if (nag_step(&n, rs, 1, t, &rid) && !fired_at) fired_at = (int)t;
    }
    CHECK(fired_at == 50100);
    /* the tail: a step due in the window's last 5 s is dropped */
    nag_reset(&n);
    nag_req_t late = req(5, 0, 60000, 56000);
    late.start_ms = 56000 - 25000; /* its R1 due now, but only 4 s left */
    late.left_ms = 4000;
    late.arrived_ms = 1;
    CHECK(nag_step(&n, &late, 1, 56000, NULL) == 0);
}

int main(void)
{
    schedule();
    one_request();
    input_stops();
    two_requests();
    floor_between_requests();
    floor_waits();
    if (!g_fail) printf("nag ok\n");
    return g_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
