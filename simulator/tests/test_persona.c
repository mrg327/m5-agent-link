/* persona.c: the personality gate walks the seven guardrails (issue 20261126-98a6). */
#include <stdio.h>

#include "persona.h"

static int failures;
#define EXPECT(cond, msg)                                  \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

#define ALL (PA_POSE | PA_ONESHOT | PA_LOOP | PA_HAPTIC | PA_SOUND)
#define PICTURES (PA_POSE | PA_ONESHOT | PA_LOOP)

/* A lit watch on its dock, nothing waiting, every feature on. */
static persona_ctx_t calm(void)
{
    persona_ctx_t c = {0};
    c.usb = true;
    c.display_on = true;
    c.fun = 0x3FF;
    return c;
}

static void test_defaults(void)
{
    EXPECT(FUN_DEFAULT & FUN_TEMPER, "Living marks on by default");
    EXPECT(FUN_DEFAULT & FUN_CELEBRATE, "celebrations on by default");
    EXPECT(FUN_DEFAULT & FUN_HELLO, "hello on by default");
    EXPECT(FUN_DEFAULT & FUN_EGGS, "eggs on by default");
    EXPECT(!(FUN_DEFAULT & (FUN_MOTIF | FUN_MOTE | FUN_REPLAY)), "auditions off by default");
    EXPECT(!(FUN_DEFAULT & FUN_REDUCED), "reduced motion off by default");
    EXPECT((FUN_DEFAULT & ~FUN_KNOWN) == 0, "the default holds only known bits");
    for (int f = 0; f < PF_COUNT; f++) {
        EXPECT(persona_bit((persona_feat_t)f) == 1u << f, "feature f is bit f");
    }
    EXPECT(persona_bit(PF_COUNT) == 0, "no bit past the end");
}

static void test_calm(void)
{
    persona_ctx_t c = calm();
    EXPECT(persona_allow(PF_TEMPER, &c) == PICTURES, "calm, on the dock: pictures, no voice");
    c.user_started = true;
    EXPECT(persona_allow(PF_TOYS, &c) == ALL, "a toy the user started may buzz and sound");
    c.user_started = false;
    c.alerting = true;
    EXPECT(persona_allow(PF_CELEBRATE, &c) == ALL, "a finish that alerts carries its voice");
}

static void test_off_and_dark(void)
{
    persona_ctx_t c = calm();
    c.fun &= ~FUN_TEMPER;
    EXPECT(persona_allow(PF_TEMPER, &c) == 0, "bit off: nothing");
    EXPECT(persona_allow(PF_MOTE, &c) != 0, "only that bit");
    c = calm();
    c.display_on = false;
    c.user_started = true;
    EXPECT(persona_allow(PF_TOYS, &c) == 0, "nothing personal while dark");
    EXPECT(persona_allow(PF_COUNT, &c) == 0, "unknown feature: nothing");
}

/* guardrail 1: alerts always win */
static void test_alerts_win(void)
{
    persona_ctx_t c = calm();
    c.needs_you = 1;
    EXPECT(persona_allow(PF_TEMPER, &c) == PA_POSE, "needs you: every mark still");
    EXPECT(persona_allow(PF_MOTE, &c) == PA_POSE, "needs you: the Mote still");
    c.user_started = true;
    EXPECT(persona_allow(PF_TOYS, &c) == PA_POSE, "needs you: even a toy stops");
    c.user_started = false;
    c.alerting = true;
    EXPECT(persona_allow(PF_TEMPER, &c) == (PA_POSE | PA_ONESHOT),
           "the asking mark keeps its lean, never a loop");
    EXPECT(persona_allow(PF_CELEBRATE, &c) == (PA_HAPTIC | PA_SOUND),
           "a finish while needed: its buzz, not its picture");
}

/* guardrail 2: fun never initiates a buzz or a sound */
static void test_no_initiative(void)
{
    persona_ctx_t c = calm();
    for (int f = 0; f < PF_COUNT; f++) {
        EXPECT(!(persona_allow((persona_feat_t)f, &c) & (PA_HAPTIC | PA_SOUND)),
               "no voice unless started or alerting");
    }
}

static void test_quiet_and_silence(void)
{
    persona_ctx_t c = calm();
    c.user_started = true;
    c.face_down = true;
    EXPECT(!(persona_allow(PF_TOYS, &c) & (PA_HAPTIC | PA_SOUND)), "face down: silence");
    c.face_down = false;
    c.alert = 2;
    EXPECT(!(persona_allow(PF_TOYS, &c) & (PA_HAPTIC | PA_SOUND)), "silent: no voice");
    c.alert = 1;
    unsigned a = persona_allow(PF_TOYS, &c);
    EXPECT(!(a & PA_SOUND) && (a & PA_HAPTIC), "quiet: a started toy buzzes, no sound");
    c.alert = 0;
    c.quiet = true;
    c.user_started = false;
    c.alerting = true;
    a = persona_allow(PF_CELEBRATE, &c);
    EXPECT(!(a & PA_SOUND) && (a & PA_HAPTIC), "quiet mode: a finish buzzes, no sound");
    c = calm();
    c.alerting = true;
    c.hush = true;
    EXPECT(!(persona_allow(PF_CELEBRATE, &c) & (PA_HAPTIC | PA_SOUND)), "hush: no voice");
    EXPECT(persona_allow(PF_CELEBRATE, &c) & PA_ONESHOT, "hush: the picture stays");
}

/* guardrail 6 and reduced motion */
static void test_power_and_reduced(void)
{
    persona_ctx_t c = calm();
    c.usb = false;
    unsigned a = persona_allow(PF_TEMPER, &c);
    EXPECT(!(a & PA_LOOP) && (a & PA_ONESHOT) && (a & PA_POSE), "battery: one-shots, no loop");
    c = calm();
    c.fun |= FUN_REDUCED;
    a = persona_allow(PF_TEMPER, &c);
    EXPECT(a == PA_POSE, "reduced motion: static poses only");
    c.alerting = true;
    EXPECT(persona_allow(PF_CELEBRATE, &c) == (PA_POSE | PA_HAPTIC | PA_SOUND),
           "reduced motion keeps a finish's voice");
}

int main(void)
{
    test_defaults();
    test_calm();
    test_off_and_dark();
    test_alerts_win();
    test_no_initiative();
    test_quiet_and_silence();
    test_power_and_reduced();
    printf(failures ? "%d failure(s)\n" : "persona: ok\n", failures);
    return failures ? 1 : 0;
}
