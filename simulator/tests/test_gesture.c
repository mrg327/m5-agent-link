#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "gesture.h"

static gesture_evt_t seen[64];
static int n_seen;

static void on_gesture(const gesture_evt_t *e, void *ctx)
{
    (void)ctx;
    if (e->kind == G_PRESS || e->kind == G_RELEASE) return; /* raw edges: tested implicitly */
    if (n_seen < 64) seen[n_seen++] = *e;
}

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

static void reset(gesture_t *g, int sectors)
{
    n_seen = 0;
    gesture_init(g, on_gesture, NULL);
    gesture_set_sectors(g, sectors);
}

static int count(gesture_kind_t k)
{
    int n = 0;
    for (int i = 0; i < n_seen; i++) n += seen[i].kind == k;
    return n;
}

static void tap(gesture_t *g, int x, int y, uint32_t t)
{
    gesture_touch(g, true, x, y, t);
    gesture_touch(g, false, x, y, t + 60);
}

static void at_angle(gesture_t *g, float deg, float r, uint32_t t)
{
    float a = deg * (float)M_PI / 180.0f;
    gesture_touch(g, true, (int)lroundf(233 + r * sinf(a)), (int)lroundf(233 - r * cosf(a)), t);
}

/* Finger on the rim from a0 to a1 in 2-degree steps, 10 ms apart; stays down. Returns the next time. */
static uint32_t sweep(gesture_t *g, float a0, float a1, float r, uint32_t t)
{
    float step = a1 >= a0 ? 2.0f : -2.0f;
    for (float a = a0; step > 0 ? a <= a1 : a >= a1; a += step, t += 10) at_angle(g, a, r, t);
    return t;
}

int main(void)
{
    gesture_t g;

    reset(&g, 0);
    tap(&g, 200, 200, 1000);
    tap(&g, 205, 198, 1200);
    EXPECT(n_seen == 2 && seen[0].kind == G_TAP && seen[1].kind == G_TAP, "taps are immediate, no double-tap");

    reset(&g, 0);
    gesture_touch(&g, true, 233, 233, 0);
    gesture_tick(&g, 599);
    EXPECT(n_seen == 0, "no long press before 600 ms");
    gesture_tick(&g, 600);
    gesture_touch(&g, false, 233, 233, 900);
    EXPECT(n_seen == 1 && seen[0].kind == G_LONG_PRESS, "long press fires once, no tap on release");

    reset(&g, 0);
    gesture_touch(&g, true, 233, 150, 0);
    gesture_touch(&g, true, 233, 190, 50);
    gesture_touch(&g, true, 233, 240, 100);
    gesture_touch(&g, false, 233, 240, 120);
    EXPECT(n_seen == 1 && seen[0].kind == G_SWIPE_DOWN, "swipe down on the centre");

    reset(&g, 0);
    gesture_touch(&g, true, 300, 233, 0);
    gesture_touch(&g, true, 200, 233, 100);
    gesture_touch(&g, false, 200, 233, 600);
    EXPECT(n_seen == 0, "slow drag is not a swipe");

    /* ── rim scrub ── */
    reset(&g, 8);
    uint32_t t = sweep(&g, 10, 24, 200, 0); /* 14 degrees: below the start arc */
    gesture_touch(&g, false, 0, 0, t);
    EXPECT(count(G_SCRUB_START) == 0 && count(G_SCRUB_END) == 0, "under 15 degrees is not a scrub");

    reset(&g, 8);
    t = sweep(&g, 10, 30, 200, 0);
    EXPECT(count(G_SCRUB_START) == 1 && seen[0].x == 0, "scrub starts after 15 degrees, in sector 0");
    EXPECT(gesture_scrubbing(&g), "scrubbing flag");
    t = sweep(&g, 30, 50, 200, t);
    EXPECT(count(G_SCRUB) == 0, "5 degrees past a boundary does not switch sector (hysteresis 15)");
    t = sweep(&g, 50, 62, 200, t);
    EXPECT(count(G_SCRUB) == 1 && seen[n_seen - 1].x == 1, "12 more degrees switches to sector 1");
    t = sweep(&g, 62, 40, 200, t);
    EXPECT(count(G_SCRUB) == 1, "backing off the boundary keeps sector 1");
    t = sweep(&g, 40, 24, 200, t);
    EXPECT(count(G_SCRUB) == 2 && seen[n_seen - 1].x == 0, "well past the boundary returns to sector 0");
    gesture_tick(&g, t + 1000);
    gesture_touch(&g, false, 0, 0, t + 1000);
    EXPECT(count(G_SCRUB_END) == 1 && count(G_TAP) == 0 && count(G_LONG_PRESS) == 0 &&
               count(G_SWIPE_LEFT) + count(G_SWIPE_RIGHT) + count(G_SWIPE_UP) + count(G_SWIPE_DOWN) == 0,
           "a scrub suppresses tap, long press and swipe");

    reset(&g, 4);
    t = sweep(&g, 340, 358, 200, 0);
    t = sweep(&g, 2, 40, 200, t);
    EXPECT(seen[0].kind == G_SCRUB_START && seen[0].x == 3, "starts in the last sector");
    EXPECT(count(G_SCRUB) >= 1 && seen[n_seen - 1].x == 0, "clockwise across 12 o'clock wraps to sector 0");

    reset(&g, 4);
    t = sweep(&g, 20, 0, 200, 0);
    t = sweep(&g, 358, 300, 200, t);
    EXPECT(count(G_SCRUB_START) == 1 && seen[n_seen - 1].x == 3, "counter-clockwise wraps backwards");

    reset(&g, 8);
    sweep(&g, 10, 80, 150, 0);
    EXPECT(count(G_SCRUB_START) == 0, "a drag that starts inside r=170 is not a rim scrub");
    reset(&g, 8);
    sweep(&g, 10, 80, 100, 0);
    EXPECT(count(G_SCRUB_START) == 0, "a circular drag near the centre is not a rim scrub");
    reset(&g, 8);
    sweep(&g, 10, 80, 232, 0);
    EXPECT(count(G_SCRUB_START) == 0, "a drag outside r=225 is not a rim scrub");

    reset(&g, 8);
    gesture_touch(&g, true, 233, 233 - 195, 0);
    gesture_touch(&g, true, 233, 233 - 150, 20);
    gesture_touch(&g, true, 233, 233 - 80, 40);
    gesture_touch(&g, false, 233, 233 - 80, 60);
    EXPECT(count(G_SCRUB_START) == 0, "a radial swipe from the rim is not a scrub");

    reset(&g, 8);
    tap(&g, 233, 233 - 205, 0);
    EXPECT(n_seen == 1 && seen[0].kind == G_TAP, "a tap on the rim is still a tap");

    reset(&g, 8);
    t = sweep(&g, 10, 40, 200, 0);
    gesture_set_sectors(&g, 4); /* a Done group expanded or merged mid-scrub */
    sweep(&g, 40, 130, 200, t);
    EXPECT(seen[n_seen - 1].x == 1 && count(G_SCRUB) == 1, "changing the sector count mid-scrub re-syncs without a jump");

    /* ── buttons ── */
    reset(&g, 0);
    gesture_button(&g, 0, true, 0);
    gesture_button(&g, 0, false, 80);
    gesture_tick(&g, 200);
    EXPECT(n_seen == 0, "a single A waits out the double-press window");
    gesture_tick(&g, 80 + GESTURE_BTN_DOUBLE_MS);
    EXPECT(n_seen == 1 && seen[0].kind == G_BTN_A, "single A after the window");

    reset(&g, 0);
    gesture_button(&g, 0, true, 0);
    gesture_button(&g, 0, false, 80);
    gesture_button(&g, 0, true, 250);
    gesture_button(&g, 0, false, 320);
    gesture_tick(&g, 900);
    EXPECT(n_seen == 1 && seen[0].kind == G_BTN_A_DOUBLE, "double A, and no single A afterwards");

    reset(&g, 0);
    gesture_button(&g, 0, true, 0);
    gesture_button(&g, 0, false, 80);
    gesture_tick(&g, 400);
    gesture_button(&g, 0, true, 450);
    gesture_button(&g, 0, false, 520);
    gesture_tick(&g, 900);
    EXPECT(count(G_BTN_A) == 2 && count(G_BTN_A_DOUBLE) == 0, "slow second press is another single");

    reset(&g, 0);
    gesture_button(&g, 1, true, 0);
    gesture_button(&g, 1, false, 90);
    gesture_button(&g, 1, true, 200);
    gesture_tick(&g, 900);
    gesture_button(&g, 1, false, 950);
    EXPECT(n_seen == 2 && seen[0].kind == G_BTN_B && seen[1].kind == G_BTN_B_LONG, "B is immediate; long at 600 ms");

    reset(&g, 0);
    gesture_button(&g, 0, true, 0);
    gesture_button(&g, 1, true, 40);
    gesture_button(&g, 1, false, 200);
    gesture_button(&g, 0, false, 220);
    gesture_tick(&g, 1500);
    EXPECT(n_seen == 1 && seen[0].kind == G_CHORD, "A+B is one chord and swallows both presses");

    if (!failures) printf("gesture tests passed\n");
    return failures ? 1 : 0;
}
