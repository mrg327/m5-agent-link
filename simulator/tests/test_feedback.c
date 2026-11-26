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
uint8_t prefs_volume(void) { return 0; }
uint8_t prefs_volume_percent(void) { return 0; }
void sound_play(const char *role) { (void)role; }
void hal_audio_set_volume(uint8_t percent) { (void)percent; }
void hal_audio_play(const int16_t *pcm, size_t n) { (void)pcm, (void)n; }
uint64_t hal_time_us(void) { return 0; }

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
    /* the detent is the one sub-floor pulse: the 15 ms kick only */
    g_last.n = 0;
    feedback_detent();
    CHECK(g_last.n == 1 && g_last.ms[0] == 15);
    if (!g_fail) printf("feedback haptics ok\n");
    return g_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
