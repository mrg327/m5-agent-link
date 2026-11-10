/* Sound bank: short chip-tune cues for the speaker (ES8311 + AW8737A), defined as data so the
 * Atlas can audition them in a browser (atlas/js/sound/synth.js) with the same synth the watch
 * will run. atlas/tools/extract.py copies the bank into atlas/data/facts.json.
 *
 * Synth (the contract both renderers follow):
 * - 16 kHz mono, one voice. A sound is a list of steps played back to back; the oscillator's
 *   phase carries over from step to step.
 * - note: MIDI number (60 = C4, 69 = A4 = 440 Hz); 0 is a rest.
 * - slide: MIDI number the pitch glides to over the step, linear in semitones; 0 = no glide.
 * - wave: SQ12 / SQ25 / SQ50 are pulse waves with 12.5 / 25 / 50 % duty, TRI a triangle,
 *   SAW a sawtooth, NOISE a 15-bit LFSR (taps 0 and 1, NES long mode) clocked 16 times per
 *   period of the note's frequency.
 * - vol: 0..15, amplitude vol / 15 x 0.5 of full scale (headroom for the amp).
 * - decay: 0 holds the level; 1 fades it linearly to 0 by the end of the step.
 * - Every step ramps in and out over 2 ms (at most a quarter of the step) so edges never click.
 */
#pragma once

#include <stdint.h>

#define SND_RATE 16000

typedef enum { SND_SQ12 = 0, SND_SQ25, SND_SQ50, SND_TRI, SND_SAW, SND_NOISE } snd_wave_t;

typedef struct {
    uint8_t wave;  /* snd_wave_t */
    uint8_t note;  /* MIDI, 0 = rest */
    uint8_t slide; /* MIDI glide target, 0 = none */
    uint8_t vol;   /* 0..15 */
    uint16_t ms;
    uint8_t decay; /* 0 hold, 1 fade to 0 */
} snd_step_t;

/* A moment the watch makes a sound for. Each role has several candidates; one is chosen. */
typedef struct {
    const char *id;   /* "done", "select", ... */
    const char *when; /* when it plays, for people */
} snd_role_t;

typedef struct {
    const char *id;   /* unique, "<role>-<name>" */
    const char *role; /* snd_role_t id */
    const char *name;
    const snd_step_t *steps;
    uint8_t n;
} snd_sound_t;

extern const snd_role_t snd_roles[];
extern const unsigned snd_role_count;
extern const snd_sound_t snd_bank[];
extern const unsigned snd_bank_count;
