#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal.h"
#include "sound.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

static hal_board_info_t s_info = {.has_speaker = true};
static uint64_t s_now_us = 1000000;
static int s_plays;
static size_t s_last_n;

const hal_board_info_t *hal_board_info(void) { return &s_info; }
uint64_t hal_time_us(void) { return s_now_us; }
void *hal_audio_alloc(size_t bytes) { return malloc(bytes); }
void hal_audio_play(const int16_t *pcm, size_t n)
{
    (void)pcm;
    s_plays++;
    s_last_n = n;
}

int main(void)
{
    for (unsigned i = 0; i < snd_bank_count; i++) {
        const snd_sound_t *s = &snd_bank[i];
        size_t n = snd_length(s);
        int16_t *pcm = malloc(n * sizeof *pcm);
        EXPECT(snd_render(s, pcm, n) == n, s->id);
        int peak = 0;
        for (size_t k = 0; k < n; k++) peak = abs(pcm[k]) > peak ? abs(pcm[k]) : peak;
        if (peak < 1000 || peak >= 32767 * 98 / 100) printf("  %s: peak %d\n", s->id, peak);
        EXPECT(peak >= 1000, "audible");
        EXPECT(peak < 32767 * 98 / 100, "headroom, as the atlas test asks");
        EXPECT(abs(pcm[0]) < 200 && abs(pcm[n - 1]) < 200, "starts and ends near silence");
        free(pcm);
    }

    const snd_sound_t *done = snd_for("done");
    sound_play("done");
    EXPECT(s_plays == 1 && s_last_n == snd_length(done), "done plays its picked sound");
    s_now_us += 100000;
    sound_play("done");
    EXPECT(s_plays == 1, "a repeat while it still plays is dropped");
    s_now_us += 2000000;
    sound_play("done");
    EXPECT(s_plays == 2, "plays again once finished");
    const snd_sound_t *input = snd_for("input");
    sound_play("input");
    EXPECT(s_plays == 3 && s_last_n == 3 * snd_length(input) + 2 * 90 * SND_RATE / 1000,
           "input plays its pick three times with gaps");
    s_now_us += 100000;
    sound_play("input");
    EXPECT(s_plays == 3, "a second request during the triple is dropped");
    s_now_us += 1000000;
    sound_play("input");
    EXPECT(s_plays == 4, "input plays again once finished");
    sound_play("nope");
    EXPECT(s_plays == 4, "unknown role is silent");
    s_info.has_speaker = false;
    s_now_us += 2000000;
    sound_play("done");
    EXPECT(s_plays == 4, "no speaker, no sound");

    if (failures) return 1;
    printf("sound: ok\n");
    return 0;
}
