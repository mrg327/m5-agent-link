/* Sound bank: short game-style cues (8-bit blips to FM chimes and layered fanfares) for the
 * speaker (ES8311 + AW8737A), defined as data so the Atlas can audition them in a browser
 * (atlas/js/sound/synth.js) with the same synth the watch will run. atlas/tools/extract.py copies the bank into atlas/data/facts.json.
 *
 * Synth (the contract both renderers follow):
 * - 16 kHz mono. A sound has up to SND_TRACKS tracks rendered independently and summed (chords,
 *   bass under a lead, a click under a tone). A track is a list of steps played back to back;
 *   each track's oscillator phases and noise register carry over from step to step.
 * - note: MIDI number (60 = C4, 69 = A4 = 440 Hz); 0 is a rest.
 * - slide: MIDI number the pitch glides to over the step, linear in semitones; 0 = no glide.
 * - wave: SQ12 / SQ25 / SQ50 are pulse waves with 12.5 / 25 / 50 % duty, TRI a triangle,
 *   SAW a sawtooth, NOISE a 15-bit LFSR (taps 0 and 1, NES long mode) clocked 16 times per
 *   period of the note's frequency, SINE a sine. FM and BELL are two-operator FM:
 *   sin(2pi c + I env sin(2pi m)) where the modulator m runs at 2x (FM, I = 2.5, electric
 *   piano) or 3.5x (BELL, I = 3, inharmonic chime) the carrier c, from its own phase.
 * - vol: 0..15, amplitude vol / 15 x 0.5 of full scale.
 * - decay: 0 holds the level; 1 fades it linearly to 0 by the end of the step. env above is
 *   this level (with the ramps below) so FM tones mellow as they fade.
 * - Every step ramps in and out over 2 ms (at most a quarter of the step) so edges never click.
 * - echo_ms > 0 adds feedback echo after the mix: y[n] = x[n] + echo_pct / 100 y[n - d]. The
 *   sound then lasts the longest track plus 3 x echo_ms, and its final echo_ms fades to 0.
 * - The result is clamped to +-1; keep the mix (with echo) below that.
 */
#pragma once

#include <stdint.h>

#define SND_RATE 16000

typedef enum { SND_SQ12 = 0, SND_SQ25, SND_SQ50, SND_TRI, SND_SAW, SND_NOISE, SND_SINE, SND_FM, SND_BELL } snd_wave_t;

#define SND_TRACKS 3

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
    const char *pick; /* id of the chosen sound in snd_bank */
} snd_role_t;

typedef struct {
    const snd_step_t *steps;
    uint8_t n;
} snd_track_t;

typedef struct {
    const char *id;   /* unique, "<role>-<name>" */
    const char *role; /* snd_role_t id */
    const char *name;
    snd_track_t tracks[SND_TRACKS]; /* unused tracks are {0} */
    uint16_t echo_ms;               /* 0 = dry */
    uint8_t echo_pct;
} snd_sound_t;

extern const snd_role_t snd_roles[];
extern const unsigned snd_role_count;
extern const snd_sound_t snd_bank[];
extern const unsigned snd_bank_count;

/* The chosen sound for a role, or NULL for an unknown role. */
const snd_sound_t *snd_for(const char *role);
