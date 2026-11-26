/* A few rare easter eggs (issue 20261126-7770, GLYPHS.md §3.15): small surprises with a date or a
 * number behind them. Each is under 2 s, silent, without haptic, once per trigger, never while
 * anything needs you and never waking the display; all follow FUN_EGGS (the screen asks the
 * persona gate for PA_ONESHOT and passes the answer as `may`).
 *
 * - Clock slip: when the clock on show reads 13:37 or 04:04, its digits slip 3 px right for one
 *   90 ms frame and back, twice; once per such minute.
 * - Centuries: the 100th and the 1000th finished turn this watch saw. The Mote, when it is on
 *   the face, loops once off its track; otherwise the sigil's rings each make one full turn in
 *   800 ms. A century waits up to 10 minutes for a moment it may play in, then lapses.
 *
 * Pure: when each egg is due and its frames; the screen draws them. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define EGGS_SLIP_PX 3
#define EGGS_SLIP_FRAME_MS 90u
#define EGGS_SLIP_MS (3u * EGGS_SLIP_FRAME_MS) /* right, back, right; then at rest */
#define EGGS_SPIN_MS 800u
#define EGGS_CENTURY_WAIT_MS 600000u
#define EGGS_SAVE_EVERY 5 /* the finish count is stored every 5 (and at a century) */

/** The eggs' latches. Zero it to start. */
typedef struct {
    uint32_t slip_minute; /**< the local minute (unix / 60) of the last slip; 0 none */
    uint32_t slip_ms;     /**< when it began */
    bool slipping;        /**< a slip is playing */
    bool century;         /**< a century waits to play */
    uint32_t century_ms;  /**< since when */
} eggs_t;

/** @brief The local time @p local_unix (seconds) reads 13:37 or 04:04. */
bool eggs_slip_time(uint32_t local_unix);
/**
 * @brief Start the clock slip when it is due: the time is right, @p may, and it has not played
 * in this minute.
 * @param[in,out] e          The latches. Must not be NULL.
 * @param[in]     local_unix Local time, seconds; 0 or less than a day: clock unknown, never.
 * @param[in]     may        A clock is on show and the gate allows a one-shot.
 * @return true on the step it starts.
 */
bool eggs_slip_due(eggs_t *e, uint32_t local_unix, bool may, uint32_t now_ms);
/** @brief The clock's offset now, px (0 or EGGS_SLIP_PX); the slip ends by itself. */
int eggs_slip_dx(eggs_t *e, uint32_t now_ms);

/** @brief The finish count after one more (it stops at UINT32_MAX). */
uint32_t eggs_count(uint32_t n);
/** @brief @p n is a century: 100 or 1000. */
bool eggs_is_century(uint32_t n);
/** @brief The count @p n is written to storage (every EGGS_SAVE_EVERY, and every century). */
bool eggs_save(uint32_t n);
/** @brief A turn finished and the count is now @p n: a century waits to play. */
void eggs_finished(eggs_t *e, uint32_t n, uint32_t now_ms);
/**
 * @brief Play the waiting century now, if @p may; it lapses after EGGS_CENTURY_WAIT_MS.
 * @return true on the step it plays.
 */
bool eggs_century_due(eggs_t *e, bool may, uint32_t now_ms);
/** @brief The sigil's turn @p t_ms into the spin, degrees 0..360 (`inout`); 0 past its end. */
int eggs_spin_deg(uint32_t t_ms);
