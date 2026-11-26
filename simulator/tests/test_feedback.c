/* feedback.c's haptic vocabulary (UI-V2 §5) as a check. Issue 20261126-e39b: the "sent" buzz and
 * the reject were three ticks each, 10 ms apart per gap, so "your answer went out" and "it did not"
 * felt the same; the finished buzz used 30 ms pulses, under the 35 ms floor. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "feedback.h"
#include "hal.h"
#include "model.h"
#include "motion.h"
#include "prefs.h"
#include "sound.h"

/* ── stubs: record the last pattern played ── */
static settings_t g_settings; /* alert = ALERT_FULL */
settings_t *model_settings(void) { return &g_settings; }
bool motion_face_down(void) { return false; }
static uint8_t g_volume, g_set_volume;
uint8_t prefs_volume(void) { return g_volume; }
uint8_t prefs_volume_percent(void) { return g_volume ? 60 : 0; }
static int g_cues; /* alert cues played */
void sound_play(const char *role) { (void)role, g_cues++; }
void hal_audio_set_volume(uint8_t percent) { g_set_volume = percent; }
static const int16_t *g_playing; /* what the speaker plays; NULL after a stop */
static int g_audio_stops;
void hal_audio_play(const int16_t *pcm, size_t n)
{
    g_playing = n ? pcm : NULL;
    if (!pcm) g_audio_stops++;
}
static uint64_t g_now_us;
uint64_t hal_time_us(void) { return g_now_us; }
static bool g_mic_on;
bool hal_mic_start(void) { return g_mic_on = true; }
void hal_mic_stop(void) { g_mic_on = false; }
const hal_board_info_t *hal_board_info(void)
{
    static const hal_board_info_t info = {.has_speaker = true};
    return &info;
}
/* the alert cues: "input" is 400 ms long, the others unknown */
static const snd_role_t s_role;
static const snd_sound_t s_sound;
const snd_role_t *snd_role(const char *id) { return strcmp(id, "input") ? NULL : &s_role; }
const snd_sound_t *snd_for(const char *role) { return strcmp(role, "input") ? NULL : &s_sound; }
size_t snd_role_length(const snd_role_t *r, const snd_sound_t *s)
{ return r && s ? 400u * SND_RATE / 1000u : 0; }

#define MAX_STEPS 32
typedef struct {
    uint16_t ms[MAX_STEPS];
    size_t n;
} pattern_t;
static pattern_t g_last;
void hal_haptic_play(const uint16_t *on_off_ms, size_t count)
{
    g_last.n = count < MAX_STEPS ? count : MAX_STEPS;
    if (on_off_ms) { memcpy(g_last.ms, on_off_ms, g_last.n * sizeof g_last.ms[0]); }
}

static int g_fail;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail = 1;                                                  \
        }                                                                \
    } while (0)

static void play_finished_done(void) { feedback_finished(false); }
static void play_finished_error(void) { feedback_finished(true); }
static void play_remind_1(void) { feedback_reminder(ATT_APPROVAL, 1); }
static void play_remind_2(void) { feedback_reminder(ATT_APPROVAL, 2); }
static void play_remind_3(void) { feedback_reminder(ATT_APPROVAL, 3); }
static void play_remind_q2(void) { feedback_reminder(ATT_QUESTION, 2); }
static void play_remind_q3(void) { feedback_reminder(ATT_QUESTION, 3); }

typedef struct {
    const char *name;
    void (*play)(void);
    int pulses;  /* expected number of "on" steps */
    bool commit; /* can follow the user's own commit: must differ from every other commit buzz */
} entry_t;

static const entry_t k_entries[] = {
    {"approved", feedback_approved, 1, true},
    {"denied", feedback_denied, 2, true},
    {"sent", feedback_reply_sent, 2, true},
    {"reject", feedback_reject, 3, true},
    {"tick", feedback_tick, 1, false},
    {"quiet", feedback_quiet_toggled, 2, false},
    {"finished_done", play_finished_done, 2, false},
    {"finished_error", play_finished_error, 3, false},
    {"scroll_stop", feedback_scroll_stop, 1, false},
    {"usage_warn", feedback_usage_warn, 1, false},
    /* reminders grow louder (issue 20261126-a909): one soft pulse, the pattern, the pattern twice
     */
    {"remind_1", play_remind_1, 1, false},
    {"remind_2", play_remind_2, 2, false},
    {"remind_3", play_remind_3, 4, false},
    {"remind_q2", play_remind_q2, 2, false},
    {"remind_q3", play_remind_q3, 4, false},
};
#define N_ENTRIES (sizeof k_entries / sizeof k_entries[0])

static pattern_t record(const entry_t *e)
{
    g_last.n = 0;
    e->play();
    return g_last;
}

/* Two patterns feel alike when they have as many steps and every step is within 30 ms. */
static bool alike(const pattern_t *a, const pattern_t *b)
{
    if (a->n != b->n) return false;
    for (size_t i = 0; i < a->n; i++) {
        int d = (int)a->ms[i] - (int)b->ms[i];
        if (d < -30 || d > 30) return false;
    }
    return true;
}

/* Toys (EXPERIMENTS.md "Toys"): the gate in place, and an alert always first. */
static void test_toys(void)
{
    static const int16_t tone[1600];
    static const uint16_t pip[] = {50};
    g_settings.alert = ALERT_FULL;
    g_volume = 3;
    g_now_us = 100000000ull;
    CHECK(feedback_toy_play(tone, 1600, 100));
    CHECK(g_playing == tone && g_set_volume == 60); /* never louder than the stored level */
    CHECK(feedback_toy_play(tone, 1600, 20) && g_set_volume == 20);
    CHECK(feedback_toy_buzz(pip, 1));
    CHECK(feedback_toy_mic(true) && g_mic_on);
    CHECK(!feedback_toy_play(tone, 1600, 20)); /* the microphone records */
    CHECK(feedback_toy_mic(false) == false && !g_mic_on);
    /* a toy sound runs (100 ms of 1600 samples), a toy mic too: a request arrives */
    CHECK(feedback_toy_play(tone, 1600, 20));
    CHECK(feedback_toy_mic(true));
    int cues = g_cues, stops = g_audio_stops;
    g_last.n = 0;
    feedback_attention(ATT_APPROVAL);
    CHECK(!g_mic_on);                           /* the microphone stopped before the cue */
    CHECK(g_audio_stops == stops + 1);          /* the toy's sound was cut */
    CHECK(g_cues == cues + 1 && g_last.n == 3); /* the alert's cue and buzz played */
    /* toys stay quiet while it plays, then may again */
    g_now_us += 1000000;
    CHECK(!feedback_toy_play(tone, 1600, 20) && !feedback_toy_buzz(pip, 1));
    CHECK(!feedback_toy_detent() && !feedback_toy_mic(true));
    g_now_us += 600000;
    CHECK(feedback_toy_play(tone, 1600, 20) && feedback_toy_buzz(pip, 1));
    /* the alert modes and the pose, as for alerts */
    g_now_us += 1000000;
    g_settings.alert = ALERT_QUIET;
    CHECK(!feedback_toy_play(tone, 1600, 20) && feedback_toy_buzz(pip, 1));
    g_settings.alert = ALERT_SILENT;
    CHECK(!feedback_toy_play(tone, 1600, 20) && !feedback_toy_buzz(pip, 1));
    g_settings.alert = ALERT_FULL;
    g_volume = 0;
    CHECK(!feedback_toy_play(tone, 1600, 20));
    g_volume = 3;
    /* a done holds toys for the hold-off too; closing a toy stops what it plays */
    feedback_attention(ATT_DONE);
    CHECK(!feedback_toy_play(tone, 1600, 20));
    g_now_us += 2000000;
    CHECK(feedback_toy_play(tone, 1600, 20));
    stops = g_audio_stops;
    feedback_toy_interrupt();
    CHECK(g_audio_stops == stops + 1 && g_playing == NULL);
    feedback_toy_interrupt(); /* nothing plays: nothing to stop, an alert is never cut */
    CHECK(g_audio_stops == stops + 1);
}

int main(void)
{
    pattern_t p[N_ENTRIES];
    for (size_t i = 0; i < N_ENTRIES; i++) {
        p[i] = record(&k_entries[i]);
        int pulses = (int)(p[i].n + 1) / 2;
        if (pulses != k_entries[i].pulses) {
            fprintf(stderr, "%s: %d pulses, want %d\n", k_entries[i].name, pulses,
                    k_entries[i].pulses);
        }
        CHECK(pulses == k_entries[i].pulses);
        CHECK(p[i].n % 2 == 1); /* ends on a pulse */
        for (size_t s = 0; s < p[i].n; s++) {
            /* pulses at the 35 ms floor or above; gaps at least 60 ms */
            if (s % 2 == 0) {
                CHECK(p[i].ms[s] >= 35);
            } else {
                CHECK(p[i].ms[s] >= 60);
            }
        }
    }
    /* no two buzzes that can follow a commit feel alike */
    for (size_t i = 0; i < N_ENTRIES; i++) {
        for (size_t j = i + 1; j < N_ENTRIES; j++) {
            if (!k_entries[i].commit || !k_entries[j].commit) continue;
            if (alike(&p[i], &p[j])) {
                fprintf(stderr, "%s and %s feel alike\n", k_entries[i].name, k_entries[j].name);
            }
            CHECK(!alike(&p[i], &p[j]));
        }
    }
    /* sent and denied are both two pulses: sent rises (short then long), denied is even */
    const pattern_t *sent = &p[2], *denied = &p[1];
    CHECK(sent->ms[2] >= 3 * sent->ms[0]);
    CHECK(denied->ms[0] == denied->ms[2]);
    /* the last reminder is the second one twice, so it stands out with the sound off too */
    pattern_t r2 = record(&k_entries[N_ENTRIES - 4]), r3 = record(&k_entries[N_ENTRIES - 3]);
    CHECK(r3.n == 2 * r2.n + 1);
    for (size_t i = 0; i < r2.n && r3.n == 2 * r2.n + 1; i++) {
        CHECK(r3.ms[i] == r2.ms[i] && r3.ms[r2.n + 1 + i] == r2.ms[i]);
    }
    /* the first is softer than the second: one pulse, shorter than the pattern's */
    pattern_t r1 = record(&k_entries[N_ENTRIES - 5]);
    CHECK(r1.n == 1 && r1.ms[0] < r2.ms[0]);
    /* the detent is the one sub-floor pulse: the 15 ms kick only */
    g_last.n = 0;
    feedback_detent();
    CHECK(g_last.n == 1 && g_last.ms[0] == 15);

    test_toys();
    if (!g_fail) printf("feedback haptics ok\n");
    return g_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
