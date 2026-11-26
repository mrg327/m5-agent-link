/* Stored preferences: speaker volume (stepped by STATUS's Volume toggle), display orientation and
 * the personality switchboard. Read once at start from the HAL's persistent store (NVS on the
 * watch), written when changed. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { ORIENT_NORMAL = 0, ORIENT_FLIPPED = 1, ORIENT_AUTO = 2 } orient_mode_t;

/* Speaker volume steps 0 (mute) .. PREFS_VOL_MAX; the default is the full level the speaker always
 * had. */
#define PREFS_VOL_MAX 5
#define PREFS_VOL_DEFAULT PREFS_VOL_MAX

void prefs_init(void);
uint8_t prefs_volume(void);         /* step 0..PREFS_VOL_MAX */
uint8_t prefs_volume_percent(void); /* what hal_audio_set_volume gets */
void prefs_set_volume(uint8_t step);
orient_mode_t prefs_orient(void);
void prefs_set_orient(orient_mode_t m);
/* The `fun` word (persona.h FUN_* bits): a bit per fun feature and one for reduced motion;
 * FUN_DEFAULT until set, unknown bits dropped. Set from LABS > Personality until STATUS has rows;
 * every feature reads it through persona_allow(), never on its own. */
uint32_t prefs_fun(void);
void prefs_set_fun(uint32_t word);
/* The Mote's green days (mote.h mote_note_day: bits 0-15 the count, 16-31 the last day counted);
 * 0 until the first. Written at most once a day. */
uint32_t prefs_mote_days(void);
void prefs_set_mote_days(uint32_t word);
/* Finished turns this watch saw (the centuries egg, eggs.h); stored only when `store` (every
 * EGGS_SAVE_EVERY), so a reboot may lose the last few, never add any. */
uint32_t prefs_finishes(void);
void prefs_set_finishes(uint32_t n, bool store);
