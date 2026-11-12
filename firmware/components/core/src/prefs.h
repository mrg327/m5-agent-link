/* Stored preferences that have no row on the STATUS screen yet: speaker volume and display orientation.
 * Read once at start from the HAL's persistent store (NVS on the watch), written when changed. */
#pragma once
#include <stdint.h>

typedef enum { ORIENT_NORMAL = 0, ORIENT_FLIPPED = 1, ORIENT_AUTO = 2 } orient_mode_t;

/* Speaker volume steps 0 (mute) .. PREFS_VOL_MAX; the default is the full level the speaker always had. */
#define PREFS_VOL_MAX 5
#define PREFS_VOL_DEFAULT PREFS_VOL_MAX

void prefs_init(void);
uint8_t prefs_volume(void);        /* step 0..PREFS_VOL_MAX */
uint8_t prefs_volume_percent(void); /* what hal_audio_set_volume gets */
void prefs_set_volume(uint8_t step);
orient_mode_t prefs_orient(void);
void prefs_set_orient(orient_mode_t m);
