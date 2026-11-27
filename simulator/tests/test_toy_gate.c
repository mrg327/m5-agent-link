/* The toy feedback gate (toy_gate.c, EXPERIMENTS.md "Toys"): an alert always wins over a toy. */
#include <stdio.h>

#include "toy_gate.h"

static int failures;
#define EXPECT(cond, msg)                                  \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

int main(void)
{
    toy_gate_t g = {0};
    toy_gate_in_t in = {.alert = ALERT_FULL, .volume = 3};

    EXPECT(toy_gate_may_buzz(&g, &in, 0) && toy_gate_may_sound(&g, &in, 0),
           "full mode, face up, volume on: a toy may buzz and sound");

    /* the alert modes, as for alerts */
    in.alert = ALERT_QUIET;
    EXPECT(toy_gate_may_buzz(&g, &in, 0) && !toy_gate_may_sound(&g, &in, 0),
           "quiet mode: buzz only");
    in.alert = ALERT_SILENT;
    EXPECT(!toy_gate_may_buzz(&g, &in, 0) && !toy_gate_may_sound(&g, &in, 0),
           "silent mode: neither");
    in.alert = ALERT_FULL;

    in.volume = 0;
    EXPECT(toy_gate_may_buzz(&g, &in, 0) && !toy_gate_may_sound(&g, &in, 0),
           "volume 0: no sound, the buzz stays");
    in.volume = 3;
    in.face_down = true;
    EXPECT(!toy_gate_may_buzz(&g, &in, 0) && !toy_gate_may_sound(&g, &in, 0), "face down: nothing");
    in.face_down = false;
    in.mic_on = true;
    EXPECT(toy_gate_may_buzz(&g, &in, 0) && !toy_gate_may_sound(&g, &in, 0),
           "microphone on: no sound");
    in.mic_on = false;
    in.fun_off = true;
    EXPECT(!toy_gate_may_buzz(&g, &in, 0) && !toy_gate_may_sound(&g, &in, 0),
           "the fun bit off: nothing");
    in.fun_off = false;

    /* an alert holds toys back for TOY_ALERT_HOLD_MS, or its own length when longer */
    toy_gate_alert(&g, 1000, 300);
    EXPECT(!toy_gate_may_buzz(&g, &in, 1000) && !toy_gate_may_sound(&g, &in, 1000),
           "an alert just started: nothing");
    EXPECT(!toy_gate_may_sound(&g, &in, 1000 + TOY_ALERT_HOLD_MS - 1), "held to the last ms");
    EXPECT(toy_gate_may_sound(&g, &in, 1000 + TOY_ALERT_HOLD_MS), "free once the hold-off ran");
    EXPECT(!toy_gate_holding(&g, 1000 + TOY_ALERT_HOLD_MS), "the hold-off ended");

    toy_gate_alert(&g, 5000, 2600);
    EXPECT(!toy_gate_may_buzz(&g, &in, 7000), "a 2.6 s cue holds for 2.6 s");
    EXPECT(toy_gate_may_buzz(&g, &in, 7600), "then free");

    /* a short alert inside a long one does not shorten the hold */
    toy_gate_alert(&g, 10000, 3000);
    toy_gate_alert(&g, 10500, 0);
    EXPECT(!toy_gate_may_sound(&g, &in, 12900), "the longer hold-off still runs");
    EXPECT(toy_gate_may_sound(&g, &in, 13000), "and ends where it would have");

    /* across the wrap of the millisecond clock */
    toy_gate_alert(&g, 0xFFFFFF00u, 0);
    EXPECT(!toy_gate_may_sound(&g, &in, 0x100u), "held across the wrap");
    EXPECT(toy_gate_may_sound(&g, &in, 0xFFFFFF00u + TOY_ALERT_HOLD_MS), "free after it");

    if (!failures) printf("toy gate tests passed\n");
    return failures ? 1 : 0;
}
