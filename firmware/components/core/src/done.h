/* The done summary line (docs/design/DONE-SUMMARY.md, proto 0.14): which parts of a finished turn's summary
 * are shown, in what words, and what gives way when the line is too wide. Plain C strings, no LVGL: the
 * screens measure with their font and the simulator's unit tests with a fake. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "model.h"

typedef enum { DONE_FILES, DONE_LINES, DONE_TESTS, DONE_TIME, DONE_SEGS } done_seg_t;

typedef struct {
    bool on[DONE_SEGS];   /* shown */
    bool fail[DONE_SEGS]; /* drawn in the error colour: only the tests, only when they failed */
    char txt[DONE_SEGS][48]; /* the segment's text, led by the separator when it is not the first shown */
} done_line_t;

/* The widest the line may be on the round glass at y 246 (px): inside the ring's pips (r 200) with a margin. */
#define DONE_MAX_W 372

/* True when the summary has any figure to show. */
bool done_known(const done_sum_t *d);

/* Measures a text in the line's font (px). */
typedef int (*done_measure_fn)(const char *text);

/* Builds the line from what is known: `3 files · +120 –14 · tests passed · 4m 12s`. When it is wider than
 * `max_w` it gives way in this order: "tests passed" becomes "passed"; then the lines, the time and the files
 * are dropped, in that order. The tests are never dropped. */
void done_line_build(const done_sum_t *d, const char *sep, int max_w, done_measure_fn measure, done_line_t *out);

/* The whole line as one string, for the Activity screen and the UI report. */
void done_line_text(const done_line_t *l, char *dst, size_t cap);
