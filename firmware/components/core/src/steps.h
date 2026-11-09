/* Per-session step timeline (UI-V2 §4.7): the last few tool steps of the current turn, built from the
 * session deltas' `n` (steps so far), `ok` (result of step n) and the step text (sum, else tool).
 * Pure data and logic: no LVGL, no model, no logging, so it is unit-tested on its own. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define STEPS_MAX 4 /* the current step and the three before it */
#define STEP_TEXT_MAX 41

typedef enum { STEP_RUNNING = 0, STEP_OK = 1, STEP_FAIL = 2 } step_state_t;

typedef struct {
    uint32_t n;
    uint8_t state; /* step_state_t */
    char text[STEP_TEXT_MAX];
} step_t;

typedef struct {
    uint32_t last_n;       /* `n` of the newest step seen, 0 = none this turn */
    uint8_t count;
    step_t s[STEPS_MAX]; /* oldest first */
} steps_t;

void steps_clear(steps_t *t);
/* One session delta. has_n: the delta carried `n`. A larger n pushes a running step (the oldest drops
 * out when full); n == 0 or a smaller n starts a new turn and clears the timeline. has_ok: the delta
 * carried `ok`, the result of the step numbered last_n. text: the current step's text (sum, else tool);
 * NULL or "" when unknown. A text that arrives while the newest step still runs replaces its text. */
void steps_update(steps_t *t, bool has_n, uint32_t n, bool has_ok, bool ok, const char *text);
/* Seeds a timeline from a snapshot: one step numbered n (nothing is known of the earlier ones). */
void steps_seed(steps_t *t, uint32_t n, step_state_t state, const char *text);
/* The step numbered n, or NULL. */
const step_t *steps_find(const steps_t *t, uint32_t n);
