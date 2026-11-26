/* Renders sound-bank entries to PCM (the C twin of atlas/js/sound/synth.js) and plays a role's
 * chosen sound on the speaker. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sound_bank.h"

/* Length of a sound in samples at SND_RATE, echo tail included. */
size_t snd_length(const snd_sound_t *s);
/* Renders up to `max` samples of 16-bit PCM; returns the number written. */
size_t snd_render(const snd_sound_t *s, int16_t *out, size_t max);

/* Samples a role plays: its pick `repeat` times with `gap_ms` between. */
size_t snd_role_length(const snd_role_t *r, const snd_sound_t *s);

/* Plays the chosen sound for a role ("done", "input"). Renders it on first use and keeps it. */
void sound_play(const char *role);

/* The toys' sound memory (EXPERIMENTS.md "Toys"): one static block, in PSRAM on the watch, shared
 * by whichever toy is open and apart from the alert cues above, which a toy never touches. A toy
 * renders into it with sound_toy_render() (a runtime note is a snd_sound_t on the stack) and plays
 * a span of it through g_ui_actions->toy_play, which the toy gate may refuse. Nothing is allocated.
 */
#define SOUND_TOY_SAMPLES ((size_t)100 * 1024) /* 6.4 s at SND_RATE, 200 KB */
int16_t *sound_toy_pcm(void);
/* Renders s at sample `at` of the block; returns the samples written (0 when it does not fit). */
size_t sound_toy_render(const snd_sound_t *s, size_t at);
