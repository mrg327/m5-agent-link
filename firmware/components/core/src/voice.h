/* Voice dictation, device side (issue 0ea9): capture from the mic, IMA ADPCM, stream to the daemon,
 * wait for the transcript. The UI (ui_voice.c) drives it; nothing here draws. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define VOICE_MAX_MS 60000  /* a recording stops by itself after this (issue f97a; it was 15 s) */
#define VOICE_WAIT_MS 45000 /* no transcript by then: give up (whisper on a minute of speech) */
/* The longest transcript the device holds, with its NUL (proto 0.16; 256 before). In PSRAM. */
#define VOICE_TEXT_MAX 2048

typedef enum { VOICE_IDLE, VOICE_REC, VOICE_WAIT, VOICE_TEXT, VOICE_FAILED } voice_state_t;

/* Starts a recording for `sid`; false when there is no mic or no link (the reason is in voice_error()). */
bool voice_start(uint32_t sid, uint32_t now_ms);
/* Ends the recording; the daemon transcribes it (state WAIT, then TEXT or FAILED). */
void voice_stop(uint32_t now_ms);
/* Abandons the recording or the transcript; the daemon simply never gets a dictate for it. */
void voice_cancel(void);
/* Pulls the mic, encodes and sends; stops at VOICE_MAX_MS; times out a missing transcript. */
void voice_tick(uint32_t now_ms);
/* A transcript arrived (the link hook). */
void voice_on_transcript(uint32_t aid, const char *text);

voice_state_t voice_state(void);
uint32_t voice_aid(void);
uint32_t voice_sid(void);
uint32_t voice_elapsed_ms(uint32_t now_ms); /* recording time so far (or in total once stopped) */
int voice_level(void);                       /* 0..100: loudness of the last 100 ms */
const char *voice_text(void);                /* the transcript, in state TEXT */
const char *voice_error(void);               /* why FAILED (or why voice_start refused) */

/* IMA ADPCM, one self-contained block: 4-byte header (first sample, step index) + 2 samples per byte,
 * low nibble first. `n` samples (odd, <= VOICE_BLOCK_SAMPLES) -> 4 + (n - 1) / 2 bytes. Exposed for tests. */
#define VOICE_BLOCK_BYTES 240
#define VOICE_BLOCK_SAMPLES (1 + (VOICE_BLOCK_BYTES - 4) * 2) /* 473 */
int voice_adpcm_block(const int16_t *pcm, int n, uint8_t *out, int *index_io);
