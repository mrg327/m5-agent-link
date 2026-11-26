/* Reminders that grow louder (issue 20261126-a909, UI-V2 §4.3): the timing of the three reminders
 * of an unanswered request, as pure logic. No LVGL, no model, no clock of its own: the caller
 * passes the pending requests and the time, and plays what nag_step() returns.
 *
 * Three steps, scaled to the answer window `win`, counted from the window's start:
 *   R1 (level 1) at 25 s: one soft pulse;
 *   R2 (level 2) at max(40 s, win / 2): the kind's arrival pattern, and the glow on a dark display;
 *   R3 (level 3) at max(R2 + 10 s, win - 10 s): the pattern twice, the sound and the glow.
 * A window under 40 s gets R1 only, at win / 2, and one under 20 s none. Nothing in the last 5 s of
 * a window. Without a window: 25, 40 and 55 s. No fourth.
 *
 * One ladder runs at a time, for the request with the least time left (requests without a window
 * after those, the oldest rid first). When it is answered or gone the next request starts its own
 * ladder from its own start, skipping the steps already past, and never sooner than 10 s after the
 * last reminder of any request. Input ends the ladders of every request that arrived before it.
 * Clocks are compared wrap-safe (signed differences). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NAG_STEPS 3
#define NAG_R1_MS 25000u        /* the soft pulse */
#define NAG_R2_MS 40000u        /* the pattern and the glow, at the earliest */
#define NAG_R3_AFTER_MS 10000u  /* R3 at least this long after R2 */
#define NAG_R3_BEFORE_MS 10000u /* R3 this long before the window ends */
#define NAG_NO_WIN_R3_MS 55000u /* R3 for a request without a window (a daemon before 0.11) */
#define NAG_SHORT_WIN_MS 40000u /* a shorter window: R1 only, at its half */
#define NAG_MIN_WIN_MS 20000u   /* a shorter window: no reminder */
#define NAG_TAIL_MS 5000u       /* none in a window's last 5 s: it goes back to the computer */
#define NAG_FLOOR_MS 10000u     /* at least this between two reminders of any requests */

/** A pending request, as nag_step() needs it. */
typedef struct {
    uint32_t rid;
    uint32_t start_ms;   /**< when its ladder counts from: the window's start, else its arrival */
    uint32_t arrived_ms; /**< when it reached the watch (input after this ends its ladder) */
    uint32_t win_ms;     /**< the whole answer window; 0 = none known */
    uint32_t left_ms;    /**< time left in it; UINT32_MAX = no window */
} nag_req_t;

/** The ladder's state; zero it (nag_reset) before use. */
typedef struct {
    uint32_t rid;      /**< the request the ladder runs for, 0 = none */
    uint8_t done;      /**< steps of its plan already given or passed */
    bool fired;        /**< a reminder has fired (fired_ms is valid) */
    uint32_t fired_ms; /**< the last reminder of any request */
    bool input;        /**< input has been noted (input_ms is valid) */
    uint32_t input_ms; /**< the last input */
} nag_t;

/**
 * @brief Work out when the reminders of a request with answer window win_ms come.
 * @param[in]  win_ms The whole window; 0 for none.
 * @param[out] out_ms The steps' times from the window's start, ascending; out_ms[i] is level i + 1.
 * @return How many steps there are, 0 to NAG_STEPS.
 */
int nag_plan(uint32_t win_ms, uint32_t out_ms[NAG_STEPS]);

/** @brief Start with no ladder and no input. */
void nag_reset(nag_t *n);

/** @brief The user touched, pressed or raised the watch: the ladders of every request that arrived
 * until now end. */
void nag_note_input(nag_t *n, uint32_t now_ms);

/**
 * @brief Advance the ladder and say whether a reminder is due now.
 * @param[in,out] n      The ladder.
 * @param[in]     reqs   The unanswered requests that may be reminded (approvals and questions);
 *                       may be NULL when count is 0.
 * @param[in]     count  How many.
 * @param[in]     now_ms The clock.
 * @param[out]    rid    The request to remind, when the return is not 0. May be NULL.
 * @return The level to remind at now (1 to NAG_STEPS), or 0 for none.
 */
int nag_step(nag_t *n, const nag_req_t *reqs, int count, uint32_t now_ms, uint32_t *rid);
