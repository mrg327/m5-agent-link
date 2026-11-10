/* Synth for the sound bank, following the contract in sound_bank.h step for step like
 * atlas/js/sound/synth.js. The mix is kept in Q14 (1.0 = 16384) so tracks and echo can sum past
 * full scale before the final clamp, as the float renderer allows. */
#include "sound.h"

#include <math.h>
#include <string.h>

#include "hal.h"

#define TAU 6.28318530718f
#define Q14 16384.0f

static size_t samples(uint32_t ms) { return (ms * SND_RATE + 500) / 1000; }

static size_t track_samples(const snd_track_t *t)
{
    size_t n = 0;
    for (unsigned i = 0; i < t->n; i++) n += samples(t->steps[i].ms);
    return n;
}

size_t snd_length(const snd_sound_t *s)
{
    size_t n = 0;
    for (unsigned t = 0; t < SND_TRACKS; t++) {
        size_t m = track_samples(&s->tracks[t]);
        if (m > n) n = m;
    }
    return n + (s->echo_ms ? samples(3u * s->echo_ms) : 0);
}

static int16_t sat(float v)
{
    if (v > 32767.f) return 32767;
    if (v < -32768.f) return -32768;
    return (int16_t)lrintf(v);
}

static float wrap(float x) { return x - floorf(x); }

static void render_track(const snd_track_t *t, int16_t *out, size_t max)
{
    float phase = 0, mphase = 0, nphase = 0;
    uint16_t lfsr = 1;
    size_t i0 = 0;
    for (unsigned k = 0; k < t->n; k++) {
        const snd_step_t *s = &t->steps[k];
        size_t len = samples(s->ms);
        size_t ramp = len / 4 < 32 ? len / 4 : 32; /* 2 ms at 16 kHz, at most a quarter of the step */
        if (ramp < 1) ramp = 1;
        float amp = s->vol / 15.f * 0.5f;
        float ratio = s->wave == SND_FM ? 2.f : 3.5f, index = s->wave == SND_FM ? 2.5f : 3.f;
        float duty = s->wave == SND_SQ12 ? 0.125f : s->wave == SND_SQ25 ? 0.25f : 0.5f;
        for (size_t i = 0; s->note && amp > 0 && i < len && i0 + i < max; i++) {
            float n = s->slide ? s->note + (float)(s->slide - s->note) * i / len : s->note;
            float inc = 440.f * powf(2.f, (n - 69.f) / 12.f) / SND_RATE;
            float edge = fminf(1.f, fminf((float)i / ramp, (float)(len - i) / ramp));
            float env = (s->decay ? 1.f - (float)i / len : 1.f) * edge;
            float v;
            switch (s->wave) {
            case SND_NOISE:
                nphase += 16 * inc;
                while (nphase >= 1) {
                    nphase -= 1;
                    uint16_t fb = (lfsr ^ (lfsr >> 1)) & 1;
                    lfsr = (uint16_t)((lfsr >> 1) | (fb << 14));
                }
                v = lfsr & 1 ? -1.f : 1.f;
                break;
            case SND_TRI: v = 4 * fabsf(phase - 0.5f) - 1; break;
            case SND_SAW: v = 2 * phase - 1; break;
            case SND_SINE: v = sinf(TAU * phase); break;
            case SND_FM:
            case SND_BELL:
                v = sinf(TAU * phase + index * env * sinf(TAU * mphase));
                mphase = wrap(mphase + ratio * inc);
                break;
            default: v = phase < duty ? 1.f : -1.f; break;
            }
            phase = wrap(phase + inc);
            out[i0 + i] = sat(out[i0 + i] + v * amp * env * Q14);
        }
        i0 += len;
    }
}

size_t snd_render(const snd_sound_t *s, int16_t *out, size_t max)
{
    size_t n = snd_length(s);
    if (n > max) n = max;
    memset(out, 0, n * sizeof *out);
    for (unsigned t = 0; t < SND_TRACKS; t++)
        if (s->tracks[t].n) render_track(&s->tracks[t], out, n);
    size_t d = samples(s->echo_ms);
    if (d && d < n) {
        float fb = s->echo_pct / 100.f;
        for (size_t i = d; i < n; i++) out[i] = sat(out[i] + fb * out[i - d]);
        size_t full = snd_length(s);
        for (size_t i = full - d; i < n; i++) out[i] = sat(out[i] * (float)(full - i) / d);
    }
    for (size_t i = 0; i < n; i++) out[i] = sat(out[i] * (32767.f / Q14));
    return n;
}

size_t snd_role_length(const snd_role_t *r, const snd_sound_t *s)
{
    unsigned times = r->repeat ? r->repeat : 1;
    return times * snd_length(s) + (times - 1) * samples(r->gap_ms);
}

void sound_play(const char *role)
{
    static struct {
        const snd_role_t *r;
        int16_t *pcm;
        size_t n;
    } cache[4];
    const snd_role_t *r = snd_role(role);
    const snd_sound_t *s = snd_for(role);
    if (!r || !s || !hal_board_info()->has_speaker) return;
    unsigned i = 0;
    while (i < sizeof cache / sizeof *cache && cache[i].r && cache[i].r != r) i++;
    if (i == sizeof cache / sizeof *cache) return;
    if (!cache[i].r) {
        size_t n = snd_role_length(r, s), one = snd_length(s), gap = samples(r->gap_ms);
        int16_t *pcm = hal_audio_alloc(n * sizeof *pcm);
        if (!pcm) return;
        memset(pcm, 0, n * sizeof *pcm);
        for (size_t at = 0; at + one <= n; at += one + gap) snd_render(s, pcm + at, one);
        cache[i].n = n;
        cache[i].pcm = pcm;
        cache[i].r = r;
    }
    /* A finish can arrive as both an attention and a session change: play it once, not restarted. */
    static const snd_role_t *last;
    static uint64_t last_us;
    uint64_t now = hal_time_us();
    if (r == last && now - last_us < 300000 && now - last_us < cache[i].n * 1000000ull / SND_RATE) return;
    last = r;
    last_us = now;
    hal_audio_play(cache[i].pcm, cache[i].n);
}
