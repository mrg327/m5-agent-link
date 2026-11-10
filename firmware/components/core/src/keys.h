/* The KEYS swipe keyboard, device side (issue 1e57, PROTOCOL.md 6a): the key layout, the stroke
 * recorder fed from the touch driver, and the link glue. The daemon decodes; nothing here knows a
 * word. ui_keys.c draws and decides what a finished stroke means. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEYS_MAX_KEYS 64
#define KEYS_MAX_PTS 200      /* points in one stroke (1200 B on the wire) */
#define KEYS_SAMPLE_MS 30     /* touch samples are kept at least this far apart (~30 Hz) */
#define KEYS_TAP_PX 18        /* a stroke whose extent stays under this is a tap */
#define KEYS_TOP_Y 170        /* top of the key block; the cap with the candidates lies above it */
#define KEYS_BOTTOM_Y 386     /* bottom of the key block; a double tap below it leaves the keyboard */
#define KEYS_BACK_DOUBLE_MS 400 /* the second tap of that double tap must land within this */
#define KEYS_MAX_CANDS 4
#define KEYS_CAND_LEN 32

/* Key code points the daemon knows besides letters and symbols that type themselves. */
#define KEYS_CP_BKSP 0x08
#define KEYS_CP_LAYER 0x0E
#define KEYS_CP_SPACE 0x20

typedef struct {
    uint16_t cp;
    int16_t x, y;
    uint16_t w, h; /* top-left rectangle in display px */
} keys_key_t;

typedef struct {
    int16_t x, y;
    uint16_t t_ms; /* since touch-down */
} keys_pt_t;

typedef struct {
    keys_pt_t pts[KEYS_MAX_PTS];
    uint16_t n;
} keys_stroke_t;

/* The keys of layer 0 (letters) or 1 (digits and symbols); returns how many (<= cap). */
int keys_layout(int layer, keys_key_t *out, int cap);

/* Opens the keyboard for `sid`: new kid, sends kb_start with layer 0. False when offline. */
bool keys_open(uint32_t sid, uint32_t now_ms);
/* Closes it: kb_end (erase = the daemon removes what it typed and was not sent) and drops any stroke. */
void keys_close(bool erase);
bool keys_active(void);
void keys_tick(uint32_t now_ms); /* re-announces the keyboard after a reconnect */
int keys_layer(void);
/* Switches layer and resends kb_start with the same kid. */
bool keys_set_layer(int layer);
uint32_t keys_kid(void);
uint32_t keys_seq(void); /* the seq the next stroke will carry */

/* Raw touch sample (core: the touch read callback). True while KEYS owns the contact, so the gesture
 * recognizer must not see it. */
bool keys_touch(bool down, int x, int y, uint32_t now_ms);
/* The stroke being drawn, or the last one until the next touch-down. */
const keys_stroke_t *keys_live(bool *down);
/* Oldest finished stroke not taken yet (NULL when none); keys_stroke_done() releases it. */
const keys_stroke_t *keys_next_stroke(void);
void keys_stroke_done(void);
bool keys_is_tap(const keys_stroke_t *s);

/* Sends kb_stroke for it (seq counts up). */
bool keys_send_stroke(const keys_stroke_t *s);
bool keys_pick(int idx);
bool keys_del(void);
bool keys_send(uint32_t hold_ms);

/* Latest candidates (link hook below fills them). */
typedef struct {
    int n;
    char word[KEYS_MAX_CANDS][KEYS_CAND_LEN];
    int typed; /* -1: nothing typed */
    int err;   /* -1: none, else a result_err value */
    uint32_t rev;
} keys_cands_t;
const keys_cands_t *keys_cands(void);
void keys_cands_clear(void);
void keys_on_cands(uint32_t kid, uint32_t seq, const char *const *words, int n, int typed, int err);
