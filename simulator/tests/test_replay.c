/* replay.c: the ring's last hour for "while you were away" (issue 20261126-9f99). */
#include <stdio.h>

#include "replay.h"

static int failures;
#define EXPECT(cond, msg)                                  \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

#define MIN (60u * 1000u)

static void note1(replay_t *r, uint32_t id, uint8_t code, uint32_t t)
{
    replay_seg_t s = {id, code};
    replay_note(r, &s, 1, t);
}

/* Note every 30 s from t0 to t1 (exclusive) with one session in `code`. */
static uint32_t hold(replay_t *r, uint32_t id, uint8_t code, uint32_t t0, uint32_t t1)
{
    for (uint32_t t = t0; t < t1; t += 30000) note1(r, id, code, t);
    return t1;
}

static void test_hour(void)
{
    replay_t r;
    replay_init(&r);
    uint32_t t = 1000;
    t = hold(&r, 7, REPLAY_WORK, t, t + 40 * MIN);
    t = hold(&r, 7, REPLAY_DONE, t, t + 20 * MIN);
    replay_seg_t now = {7, REPLAY_DONE};
    EXPECT(replay_code(&r, 7, 0) == REPLAY_WORK, "an hour ago it worked");
    EXPECT(replay_code(&r, 7, REPLAY_STEPS - 1) == REPLAY_DONE, "the last 3 min it is done");
    int first_done = -1;
    for (int s = 0; s < REPLAY_STEPS && first_done < 0; s++) {
        if (replay_code(&r, 7, s) == REPLAY_DONE) first_done = s;
    }
    EXPECT(first_done >= 12 && first_done <= 14, "it finished about 20 min ago (step 13)");
    EXPECT(replay_changes(&r, &now, 1) == 1, "one change in the hour");
    EXPECT(replay_code(&r, 99, 3) == REPLAY_UNKNOWN, "a session never seen is unknown");
    EXPECT(replay_code(&r, 7, REPLAY_STEPS) == REPLAY_UNKNOWN, "no step past the end");
}

static void test_flash_kept(void)
{
    replay_t r;
    replay_init(&r);
    uint32_t t = 0;
    t = hold(&r, 1, REPLAY_WORK, t, 30 * MIN);
    /* 40 s of a permission inside one 3-minute step, then work again */
    note1(&r, 1, REPLAY_PERM, t + 10000);
    note1(&r, 1, REPLAY_PERM, t + 50000);
    t = hold(&r, 1, REPLAY_WORK, t + 60000, t + 30 * MIN);
    int perms = 0;
    for (int s = 0; s < REPLAY_STEPS; s++) perms += replay_code(&r, 1, s) == REPLAY_PERM;
    EXPECT(perms == 1, "a request that came and went within 3 min shows for one step");
    replay_seg_t now = {1, REPLAY_WORK};
    EXPECT(replay_changes(&r, &now, 1) == 2, "orange and back");
}

static void test_absent_and_unknown(void)
{
    replay_t r;
    replay_init(&r);
    uint32_t t = 0;
    /* the watch has recorded 30 min; session 2 started 10 min ago */
    t = hold(&r, 1, REPLAY_IDLE, t, 20 * MIN);
    for (; t < 30 * MIN; t += 30000) {
        replay_seg_t s[2] = {{1, REPLAY_IDLE}, {2, REPLAY_WORK}};
        replay_note(&r, s, 2, t);
    }
    EXPECT(replay_code(&r, 1, 0) == REPLAY_UNKNOWN, "before the watch recorded: unknown");
    EXPECT(replay_code(&r, 2, 0) == REPLAY_UNKNOWN, "unknown for a newer session too");
    EXPECT(replay_code(&r, 2, 12) == REPLAY_ABSENT, "recorded but not yet started: absent");
    EXPECT(replay_code(&r, 2, REPLAY_STEPS - 1) == REPLAY_WORK, "working now");
    replay_seg_t now[2] = {{1, REPLAY_IDLE}, {2, REPLAY_WORK}};
    EXPECT(replay_changes(&r, now, 2) == 1, "a session that started is a change");
    /* session 2 leaves the ring: its track carries absent */
    t = hold(&r, 1, REPLAY_IDLE, t, t + 6 * MIN);
    EXPECT(replay_code(&r, 2, REPLAY_STEPS - 1) == REPLAY_ABSENT, "gone from the ring: absent");
}

static void test_dim_and_rank(void)
{
    replay_t r;
    replay_init(&r);
    note1(&r, 3, REPLAY_DONE, 0);
    note1(&r, 3, REPLAY_DONE | REPLAY_DIM, 1000);
    EXPECT(replay_code(&r, 3, REPLAY_STEPS - 1) == (REPLAY_DONE | REPLAY_DIM),
           "same kind: the latest look");
    note1(&r, 3, REPLAY_ERROR, 2000);
    note1(&r, 3, REPLAY_WORK, 3000);
    EXPECT(replay_code(&r, 3, REPLAY_STEPS - 1) == REPLAY_ERROR, "an error outranks work");
}

static void test_tracks_full(void)
{
    replay_t r;
    replay_init(&r);
    replay_seg_t s[REPLAY_TRACKS + 2];
    for (int i = 0; i < REPLAY_TRACKS + 2; i++) {
        s[i] = (replay_seg_t){100u + (uint32_t)i, REPLAY_WORK};
    }
    replay_note(&r, s, REPLAY_TRACKS + 2, 0);
    EXPECT(replay_code(&r, 100, REPLAY_STEPS - 1) == REPLAY_WORK, "the first sessions get tracks");
    EXPECT(replay_code(&r, 100u + REPLAY_TRACKS + 1, REPLAY_STEPS - 1) == REPLAY_UNKNOWN,
           "past the table: replays as now");
    /* later only two sessions: a new one reuses a track of one that left */
    replay_note(&r, s, 2, 4 * MIN);
    replay_seg_t n = {500, REPLAY_DONE};
    replay_seg_t two[3] = {s[0], s[1], n};
    replay_note(&r, two, 3, 5 * MIN);
    EXPECT(replay_code(&r, 500, REPLAY_STEPS - 1) == REPLAY_DONE, "a freed track is reused");
    EXPECT(replay_code(&r, 100, REPLAY_STEPS - 1) == REPLAY_WORK,
           "the ones on the ring keep theirs");
}

static void test_away(void)
{
    replay_t r;
    replay_init(&r);
    EXPECT(!replay_woke(&r, true, 0), "lit at start: no wake");
    EXPECT(!replay_woke(&r, false, 1000), "going dark");
    EXPECT(!replay_woke(&r, true, 1000 + 5 * MIN), "5 min dark is not away");
    EXPECT(!replay_woke(&r, true, 1000 + 6 * MIN), "only the first lit step counts");
    EXPECT(!replay_woke(&r, false, 10 * MIN), "dark again");
    EXPECT(!replay_woke(&r, false, 20 * MIN), "still dark");
    EXPECT(replay_woke(&r, true, 10 * MIN + REPLAY_AWAY_MS), "15 min dark: the first look");
    EXPECT(!replay_woke(&r, true, 10 * MIN + REPLAY_AWAY_MS + 10), "once");
    /* the millisecond clock wraps while dark */
    EXPECT(!replay_woke(&r, false, 0xFFFFF000u), "dark before the wrap");
    EXPECT(replay_woke(&r, true, 0xFFFFF000u + REPLAY_AWAY_MS), "away across the wrap");
}

static void test_play(void)
{
    replay_t r;
    replay_init(&r);
    note1(&r, 1, REPLAY_WORK, 0);
    EXPECT(replay_frame(&r, 0) == REPLAY_OFF, "not playing");
    replay_start(&r, 5000);
    EXPECT(replay_frame(&r, 5000) == 0, "step 0 at once");
    EXPECT(replay_arc_deg(0) == 18, "18 degrees a step");
    EXPECT(replay_frame(&r, 5099) == 0, "100 ms a step");
    EXPECT(replay_frame(&r, 5100) == 1, "then step 1");
    EXPECT(replay_frame(&r, 5000 + 1999) == REPLAY_STEPS - 1, "the last step before 2 s");
    EXPECT(replay_arc_deg(REPLAY_STEPS - 1) == 360, "a full turn at the end");
    EXPECT(replay_arc_pct(&r, 5000 + 1999) == 100, "the arc bright while playing");
    EXPECT(replay_frame(&r, 7000) == REPLAY_STEPS, "then the fade");
    EXPECT(replay_arc_pct(&r, 7100) == 50, "half way down the fade");
    EXPECT(replay_frame(&r, 7200) == REPLAY_OFF, "over after 2.2 s");
    EXPECT(!r.playing, "it stops itself");
    replay_start(&r, 9000);
    replay_stop(&r);
    EXPECT(replay_frame(&r, 9050) == REPLAY_OFF, "stopped at once");
}

static void test_frozen_while_playing(void)
{
    replay_t r;
    replay_init(&r);
    uint32_t t = hold(&r, 1, REPLAY_WORK, 0, 59 * MIN);
    t = hold(&r, 1, REPLAY_DONE, t, t + 30000);
    replay_start(&r, t);
    uint8_t last = replay_code(&r, 1, REPLAY_STEPS - 1);
    note1(&r, 1, REPLAY_ERROR, t + 3 * MIN); /* a column passes mid-replay */
    EXPECT(replay_code(&r, 1, REPLAY_STEPS - 1) == last, "the replay reads the hour it began on");
}

static void test_long_gap(void)
{
    replay_t r;
    replay_init(&r);
    note1(&r, 1, REPLAY_ERROR, 0);
    note1(&r, 1, REPLAY_IDLE, 1000);
    note1(&r, 1, REPLAY_IDLE, 5u * 60u * MIN); /* five hours without a note */
    for (int s = 0; s < REPLAY_STEPS; s++) {
        EXPECT(replay_code(&r, 1, s) == REPLAY_IDLE, "an hour of the carried state");
    }
    replay_seg_t now = {1, REPLAY_IDLE};
    EXPECT(replay_changes(&r, &now, 1) == 0, "nothing happened in the last hour");
}

int main(void)
{
    test_hour();
    test_flash_kept();
    test_absent_and_unknown();
    test_dim_and_rank();
    test_tracks_full();
    test_away();
    test_play();
    test_frozen_while_playing();
    test_long_gap();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("replay: ok\n");
    return 0;
}
