#include <stdio.h>
#include <stdlib.h>

#include "gesture.h"

static gesture_kind_t seen[16];
static int n_seen;

static void on_gesture(const gesture_evt_t *e, void *ctx)
{
    (void)ctx;
    if (e->kind == G_PRESS || e->kind == G_RELEASE) return; /* raw edges: tested implicitly */
    seen[n_seen++] = e->kind;
}

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

static void reset(gesture_t *g) { n_seen = 0; gesture_init(g, on_gesture, NULL); }

static void tap(gesture_t *g, int x, int y, uint32_t t)
{
    gesture_touch(g, true, x, y, t);
    gesture_touch(g, false, x, y, t + 60);
}

int main(void)
{
    gesture_t g;

    reset(&g);
    tap(&g, 200, 200, 1000);
    EXPECT(n_seen == 1 && seen[0] == G_TAP, "single tap");

    tap(&g, 205, 198, 1200);
    EXPECT(n_seen == 2 && seen[1] == G_DOUBLE_TAP, "second tap within 300 ms is a double-tap");
    tap(&g, 205, 198, 1400);
    EXPECT(n_seen == 3 && seen[2] == G_TAP, "third tap starts a new pair");

    reset(&g);
    tap(&g, 100, 100, 1000);
    tap(&g, 100, 100, 1500);
    EXPECT(n_seen == 2 && seen[1] == G_TAP, "slow second tap is a plain tap");

    reset(&g);
    gesture_touch(&g, true, 233, 300, 0);
    gesture_tick(&g, 599);
    EXPECT(n_seen == 0, "no long press before 600 ms");
    gesture_tick(&g, 600);
    gesture_touch(&g, false, 233, 300, 900);
    EXPECT(n_seen == 1 && seen[0] == G_LONG_PRESS, "long press fires once, no tap on release");

    reset(&g);
    gesture_touch(&g, true, 233, 350, 0);
    gesture_touch(&g, true, 233, 300, 50);
    gesture_touch(&g, true, 233, 250, 100);
    gesture_touch(&g, false, 233, 250, 120);
    EXPECT(n_seen == 1 && seen[0] == G_SWIPE_UP, "swipe up (finger moves up)");

    reset(&g);
    gesture_touch(&g, true, 300, 233, 0);
    gesture_touch(&g, true, 200, 233, 100);
    gesture_touch(&g, false, 200, 233, 600);
    EXPECT(n_seen == 0, "slow drag is not a swipe");

    reset(&g);
    gesture_touch(&g, true, 100, 233, 0);
    gesture_touch(&g, true, 190, 240, 100);
    gesture_touch(&g, false, 190, 240, 130);
    EXPECT(n_seen == 1 && seen[0] == G_SWIPE_RIGHT, "swipe right");

    reset(&g);
    gesture_button(&g, 0, true, 0);
    gesture_button(&g, 0, false, 100);
    gesture_button(&g, 1, true, 200);
    gesture_tick(&g, 900);
    gesture_button(&g, 1, false, 950);
    EXPECT(n_seen == 2 && seen[0] == G_BTN_A && seen[1] == G_BTN_B_LONG, "button short and long");

    if (!failures) printf("gesture tests passed\n");
    return failures ? 1 : 0;
}
