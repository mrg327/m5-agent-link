/* Bench (BENCH.md, proto 0.24): a finger-stroke player under the touch read path, and a timeline of every frame,
 * touch, gesture and command on the watch's own clock, dumped to the daemon after a run. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

/* Record kinds (BENCH.md §3). */
enum { BENCH_FRAME = 0, BENCH_DOWN = 1, BENCH_UP = 2, BENCH_GESTURE = 3, BENCH_MARK = 4, BENCH_CMD = 5 };
enum { BENCH_OP_START = 0, BENCH_OP_STOP = 1, BENCH_OP_MARK = 2 };

#define BENCH_REC_BYTES 16
#define BENCH_MAX_RECS 4096
#define BENCH_MAX_POINTS 512
#define BENCH_RECS_PER_MSG 64

/* Hook the display's refresh events (once, after the display is created). */
void bench_init(lv_display_t *disp);

/* dbg_touch: play one stroke. `pts` holds int16 LE x, y pairs in the coordinates the user sees. */
void bench_on_touch(uint32_t dt_ms, const uint8_t *pts, size_t len, uint32_t now_ms);
/* dbg_bench: start (clear, record), stop (send the records) or mark. */
void bench_on_ctl(int op, uint32_t arg);

/* From the touch read callback, with the user-facing point the panel reported. When a stroke plays, replaces it
 * with the stroke's and returns true. Also records touch-down and lift for real and played touches alike. */
bool bench_touch(uint32_t now_ms, bool *down, int32_t *x, int32_t *y);
/* True while a stroke plays (it counts as user activity). */
bool bench_playing(void);

/* A gesture the UI dispatched, or a command the watch sent. */
void bench_note(int kind, uint32_t arg);

/* Every core_step: streams the records after a stop, a few messages per call. */
void bench_tick(uint32_t now_ms);

/* For tests: the records kept so far and how many were lost. */
const uint8_t *bench_records(size_t *count, uint32_t *lost);
