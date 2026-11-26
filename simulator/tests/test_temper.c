/* temper.c: a sigil's temperament from the second hash, and the motion grammar's poses (issue
 * 20261126-98a6). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "persona.h"
#include "temper.h"

static int failures;
#define EXPECT(cond, msg)                                  \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

#define ALL (PA_POSE | PA_ONESHOT | PA_LOOP)

static bool same(temper_pose_t a, temper_pose_t b)
{
    return a.dx == b.dx && a.dy == b.dy && a.dr == b.dr && a.rot_mid == b.rot_mid &&
           a.rot_out == b.rot_out && a.lum == b.lum;
}

static bool neutral(temper_pose_t p) { return same(p, temper_neutral); }

static bool same_temper(temper_t a, temper_t b)
{
    return a.period_ms == b.period_ms && a.sway_period_ms == b.sway_period_ms &&
           a.phase_ms == b.phase_ms && a.sway_deg == b.sway_deg && a.dir == b.dir &&
           a.spark_deg == b.spark_deg;
}

static int reach(temper_pose_t p) { return abs(p.dx) + abs(p.dy) + (p.dr > 0 ? p.dr : 0); }

static void test_key(void)
{
    char k[32];
    EXPECT(temper_key("webapp", 7, k, sizeof k) && !strcmp(k, "webapp"), "a name is its key");
    EXPECT(temper_key("", 7, k, sizeof k) && !strcmp(k, "\x01sid 7"), "no folder: the sid key");
    EXPECT(temper_key(NULL, 7, k, sizeof k) && !strcmp(k, "\x01sid 7"), "NULL as no folder");
    EXPECT(!temper_key("", 0, k, sizeof k) && !k[0], "neither: no key");
}

static void test_ranges(void)
{
    int dirs[2] = {0, 0};
    for (int i = 0; i < 1000; i++) {
        char name[16];
        snprintf(name, sizeof name, "proj%d", i);
        temper_t t = temper_of(name, 0);
        EXPECT(t.period_ms >= TEMPER_PERIOD_MIN && t.period_ms <= TEMPER_PERIOD_MAX, "period");
        EXPECT(t.period_ms % 250 == 0, "period in 250 ms steps");
        EXPECT(t.sway_deg >= TEMPER_SWAY_MIN && t.sway_deg <= TEMPER_SWAY_MAX, "sway");
        EXPECT(t.sway_period_ms == t.period_ms * 3 / 2 || t.sway_period_ms == t.period_ms * 2,
               "the sway drifts against the breath");
        EXPECT(t.dir == 1 || t.dir == -1, "a direction");
        EXPECT(t.phase_ms < t.period_ms, "phase inside the period");
        EXPECT(t.spark_deg < 45, "spark rotation");
        dirs[t.dir > 0]++;
    }
    EXPECT(dirs[0] > 300 && dirs[1] > 300, "both directions are common");
}

/* Fixed names give fixed temperaments: a change to the hash slice or the formulas shows here. */
static void test_golden(void)
{
    temper_t a = temper_of("webapp", 0), b = temper_of("api", 0), c = temper_of("webapp", 99);
    printf("webapp: P %u sway %u/%u dir %d phase %u spark %u\n", a.period_ms, a.sway_deg,
           a.sway_period_ms, a.dir, a.phase_ms, a.spark_deg);
    printf("api:    P %u sway %u/%u dir %d phase %u spark %u\n", b.period_ms, b.sway_deg,
           b.sway_period_ms, b.dir, b.phase_ms, b.spark_deg);
    EXPECT(same_temper(a, c), "a named session's temper ignores its sid");
    EXPECT(!same_temper(a, b), "two names, two temperaments");
    EXPECT(a.period_ms == 3250 && a.sway_deg == 4 && a.sway_period_ms == 4875 && a.dir == 1 &&
               a.phase_ms == 482 && a.spark_deg == 42,
           "webapp golden");
    EXPECT(b.period_ms == 4500 && b.sway_deg == 2 && b.sway_period_ms == 6750 && b.dir == -1 &&
               b.phase_ms == 3832 && b.spark_deg == 31,
           "api golden");
}

static void test_breath(void)
{
    temper_t t = temper_of("webapp", 0);
    int changes = 0, lo = 100, hi = 0, swaymax = 0;
    temper_pose_t last = temper_pose(&t, TEMPER_WORK, 0, 0, ALL, 22);
    for (uint32_t ms = 0; ms < t.period_ms; ms += 10) {
        temper_pose_t p = temper_pose(&t, TEMPER_WORK, ms, ms, ALL, 22);
        if (!same(p, last)) changes++;
        last = p;
        if (p.lum < lo) lo = p.lum;
        if (p.lum > hi) hi = p.lum;
        if (abs(p.rot_mid) > swaymax) swaymax = abs(p.rot_mid);
        EXPECT(p.dx == 0 && p.dy == 0 && p.dr == 0 && p.rot_out == 0, "a breath does not move");
    }
    EXPECT(lo == TEMPER_LUM_LOW && hi == 100, "the breath runs 72..100 %");
    EXPECT(changes <= 16, "at most 16 redraws a breath");
    EXPECT(changes >= 8, "it does breathe");
    EXPECT(swaymax > 0 && swaymax <= t.sway_deg, "the middle ring sways within its amplitude");
    /* the same instant on another mark is another phase */
    temper_t u = temper_of("api", 0);
    int differ = 0;
    for (uint32_t ms = 0; ms < 6000; ms += 250) {
        temper_pose_t p = temper_pose(&t, TEMPER_WORK, ms, ms, ALL, 22);
        temper_pose_t q = temper_pose(&u, TEMPER_WORK, ms, ms, ALL, 22);
        differ += p.lum != q.lum;
    }
    EXPECT(differ > 0, "two marks do not breathe together");
    /* idle: slower, shallower, no sway */
    for (uint32_t ms = 0; ms < 2u * t.period_ms; ms += 50) {
        temper_pose_t p = temper_pose(&t, TEMPER_IDLE, ms, ms, ALL, 22);
        EXPECT(p.lum >= 80 && p.rot_mid == 0, "idle breath 80..100, no sway");
    }
}

static void test_no_loop(void)
{
    temper_t t = temper_of("webapp", 0);
    for (uint32_t ms = 0; ms < 8000; ms += 100) {
        EXPECT(neutral(temper_pose(&t, TEMPER_WORK, ms, ms, PA_POSE | PA_ONESHOT, 22)),
               "without PA_LOOP a working mark is still");
        EXPECT(neutral(temper_pose(&t, TEMPER_WORK, ms, ms, 0, 22)), "without PA_POSE neutral");
        EXPECT(neutral(temper_pose(&t, TEMPER_ASLEEP, ms, ms, 0, 22)), "feature off: no droop");
    }
}

static void test_gestures(void)
{
    temper_t t = temper_of("webapp", 0);
    for (int st = TEMPER_STILL; st <= TEMPER_ASLEEP; st++) {
        for (uint32_t ms = 0; ms < 1000; ms += 10) {
            temper_pose_t p = temper_pose(&t, (temper_state_t)st, ms, ms, ALL, 22);
            EXPECT(reach(p) <= TEMPER_REACH, "a pose stays within reach at r 22");
            p = temper_pose(&t, (temper_state_t)st, ms, ms, ALL, 52);
            EXPECT(reach(p) <= TEMPER_REACH, "a pose stays within reach at r 52");
        }
    }
    /* one-shots end at rest */
    EXPECT(neutral(temper_pose(&t, TEMPER_DONE, TEMPER_GESTURE_MS, 0, ALL, 22)), "dip settles");
    EXPECT(neutral(temper_pose(&t, TEMPER_ERROR, TEMPER_GESTURE_MS, 0, ALL, 22)), "shiver ends");
    EXPECT(temper_pose(&t, TEMPER_DONE, 0, 0, ALL, 22).dr < 0, "the dip goes in first");
    EXPECT(temper_pose(&t, TEMPER_DONE, 100, 0, ALL, 22).dr > 0, "then overshoots");
    EXPECT(temper_pose(&t, TEMPER_ERROR, 0, 0, ALL, 22).dx == 2 * t.dir,
           "the shiver starts the temper's way");
    EXPECT(neutral(temper_pose(&t, TEMPER_DONE, 0, 0, PA_POSE, 22)),
           "no one-shot: the dip shows its end");
    /* lean, then still */
    EXPECT(temper_pose(&t, TEMPER_WANTS, 0, 0, ALL, 22).dy == 1, "the lean starts at 1 px");
    temper_pose_t end = temper_pose(&t, TEMPER_WANTS, 200, 0, ALL, 22);
    EXPECT(end.dy == 3 && end.lum == 100, "and ends 3 px over, full bright");
    for (uint32_t ms = 200; ms < 10000; ms += 333) {
        temper_pose_t p = temper_pose(&t, TEMPER_WANTS, ms, ms, ALL, 22);
        EXPECT(same(end, p), "still after the lean");
    }
    EXPECT(temper_pose(&t, TEMPER_WANTS, 0, 0, PA_POSE, 22).dy == 3,
           "reduced motion: the lean's end at once");
    /* droop */
    temper_pose_t d = temper_pose(&t, TEMPER_ASLEEP, 0, 0, PA_POSE, 22);
    EXPECT(d.lum == 60 && d.rot_out == 6 * t.dir, "snoozed: dim, outer ring turned");
}

static void test_hero_and_joy(void)
{
    temper_t t = temper_of("webapp", 0);
    EXPECT(temper_hero(&t, TEMPER_DONE, 0, ALL).dy > 0, "the hero dips down");
    EXPECT(temper_hero(&t, TEMPER_DONE, 100, ALL).dy < 0, "and overshoots up");
    EXPECT(neutral(temper_hero(&t, TEMPER_DONE, TEMPER_GESTURE_MS, ALL)), "then rests");
    EXPECT(temper_hero(&t, TEMPER_ERROR, 0, ALL).dx == 3 * t.dir, "the hero shivers");
    EXPECT(neutral(temper_hero(&t, TEMPER_WORK, 50, ALL)), "the hero never loops");
    EXPECT(neutral(temper_hero(&t, TEMPER_DONE, 0, PA_POSE)), "no one-shot: still");
    EXPECT(temper_joy(TEMPER_DONE, 0, ALL) == 0, "the burst starts at a finish");
    EXPECT(temper_joy(TEMPER_DONE, 225, ALL) == 500, "half way");
    EXPECT(temper_joy(TEMPER_DONE, TEMPER_JOY_MS, ALL) == -1, "and is gone");
    EXPECT(temper_joy(TEMPER_DONE, 10, PA_POSE) == -1, "no one-shot: no burst");
    EXPECT(temper_joy(TEMPER_ERROR, 10, ALL) == -1, "no burst for an error");
}

int main(void)
{
    test_key();
    test_ranges();
    test_golden();
    test_breath();
    test_no_loop();
    test_gestures();
    test_hero_and_joy();
    printf(failures ? "%d failure(s)\n" : "temper: ok\n", failures);
    return failures ? 1 : 0;
}
