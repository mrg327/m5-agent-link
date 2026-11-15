/* Plan usage and the session readouts (docs/design/GAUGES.md, proto 0.10): the model of the plan's
 * rate-limit windows, when a figure may be shown, and the words for it. Plain data and C strings, no
 * LVGL, so the simulator's unit tests and every screen share one set of rules. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "model.h"

/* A reading older than this is unknown: the figure gives way to a dash (the daemon re-sends every 5 min
 * while readings arrive). */
#define GAUGES_STALE_S 3600
/* One buzz when the 5-hour window first crosses this in a window (quiet and mute honoured). */
#define GAUGES_WARN_PCT 80
/* Colour steps: the 5-hour and weekly arcs, and the context capsule. */
#define GAUGES_USAGE_HOT 95
#define GAUGES_CTX_WARN 70
#define GAUGES_CTX_HOT 90

typedef struct {
    bool known;     /* the daemon sent this window */
    uint8_t pct;    /* used, 0-100 */
    uint32_t resets; /* unix */
    uint32_t out;   /* unix: runs out at the current pace before it resets; 0 = not */
} usage_win_t;

typedef struct usage_s {
    uint32_t at; /* when the daemon read it (unix); 0 = never received */
    usage_win_t h5, wk;
} usage_t;

void gauges_set_usage(const usage_t *u); /* bumps the model revision */
const usage_t *gauges_usage(void);
void gauges_reset(void); /* model_init() time: nothing known */

/* The window as it may be shown at `now`: false while the clock is unset, the window is unknown, it has
 * reset, or the reading is stale. A false window is drawn as a dash, never as 0 %. */
bool gauges_live(const usage_win_t *w, uint32_t at, uint32_t now);
bool gauges_h5(uint32_t now, usage_win_t *out);
bool gauges_wk(uint32_t now, usage_win_t *out);

/* True once per 5-hour window, when a reading at or above GAUGES_WARN_PCT follows one below it in the same
 * window. A watch that connects with the figure already high does not buzz: it never saw it cross. */
bool gauges_should_warn(const usage_t *prev, const usage_t *now);

/* The lengths of the two windows: the gauges draw time along the same arc as the usage (GLYPHS.md rule 4). */
#define GAUGES_H5_S (5u * 3600u)
#define GAUGES_WK_S (7u * 24u * 3600u)
/* How far through its window the moment `at` is, 0..1000 per mille: the window is `len` seconds long and ends at
 * w->resets. Drawn as a bead on the window's arc: the arc left beyond the bead is the time until the reset, and
 * usage that has filled past the bead is ahead of the clock. */
int gauges_window_pm(const usage_win_t *w, uint32_t at, uint32_t len);

/* "2h 10m", "45m", "3d 4h": a duration to the minute, rounded up. */
void gauges_span(char *dst, size_t cap, uint32_t secs);
/* "14:30": unix time `at` in local time (tz_off minutes east). */
void gauges_clock(char *dst, size_t cap, uint32_t at, int32_t tz_off_min);
/* The detail line of a window: "resets in 2h 10m", "runs out 14:30", "limit until 16:00". */
void gauges_detail(char *dst, size_t cap, const usage_win_t *w, uint32_t now, int32_t tz_off_min);

/* Session readouts for the AGENT header: "Opus 4.7 · Plan · PR 128 approved". The mode only when it is not
 * the default (status by exception); the pull request only with a number. "" when there is nothing. */
void gauges_readout(char *dst, size_t cap, const session_t *s, const char *sep);
const char *gauges_mode_word(uint8_t pm); /* NULL for the default mode */
