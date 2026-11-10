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

/* Plays the chosen sound for a role ("done", "select"). Renders it on first use and keeps it. */
void sound_play(const char *role);
