/* While you were away (issue 20261126-9f99, GLYPHS.md §3.13): the ring's last hour, kept so that
 * when you come back the bezel can replay it in 2 s while the centre shows now. Pure: the screen
 * (ui_replay.c) hands it the ring's segments and whether the display is lit once a step, and asks
 * what each capsule looked like at each step of the replay.
 *
 * The history is a static table of REPLAY_TRACKS sessions by REPLAY_STEPS columns of 3 minutes:
 * per session and per 3 minutes, the most telling state the capsule showed in them (an error over
 * a request over a finish over work over idle), so a flash of orange that resolved itself inside
 * three minutes is still seen. About 420 bytes, no heap.
 *
 * Away: the display was dark (off, or only the alert glow) for at least REPLAY_AWAY_MS. The first
 * lit step after it is the first look. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define REPLAY_STEPS 20                    /**< columns: 20 steps of 3 min = the last hour */
#define REPLAY_STEP_MS (3u * 60u * 1000u)  /**< one column */
#define REPLAY_AWAY_MS (15u * 60u * 1000u) /**< dark this long is away */
#define REPLAY_PLAY_MS 2000u               /**< the whole hour on the bezel */
#define REPLAY_FRAME_MS (REPLAY_PLAY_MS / REPLAY_STEPS) /**< one step on screen: 100 ms */
#define REPLAY_FADE_MS 200u /**< the time arc fades after the last step */
/** Sessions remembered. The ring merges finished sessions from 9 on, so more than 16 distinct
 * capsules within an hour is rare; one without a track replays as it is now (the honest fallback),
 * and the track left the ring longest is reused first. */
#define REPLAY_TRACKS 16
/** Changes within the hour that make a replay worth 2 s: one finish is the common story. */
#define REPLAY_MIN_CHANGES 1

/** What a capsule showed, in rising order of how much it tells (the column keeps the highest). */
typedef enum {
    REPLAY_IDLE = 0,
    REPLAY_WORK,
    REPLAY_DONE,
    REPLAY_ASK,
    REPLAY_PERM,
    REPLAY_ERROR,
    REPLAY_KINDS
} replay_kind_t;
#define REPLAY_DIM 0x10u     /**< or'd into a code: the capsule was dimmed (seen, snoozed) */
#define REPLAY_ABSENT 0xFFu  /**< the session was not on the ring then: drawn hollow */
#define REPLAY_UNKNOWN 0xFEu /**< the watch was not recording then: drawn as now */
#define REPLAY_OFF (-1)      /**< replay_frame(): not playing */

/** One capsule of the ring now: its id and code (a replay_kind_t, maybe | REPLAY_DIM). */
typedef struct {
    uint32_t id;
    uint8_t code;
} replay_seg_t;

typedef struct {
    uint32_t id;
    uint16_t seen;              /**< the step serial it was last on the ring */
    uint8_t code[REPLAY_STEPS]; /**< by column; column `head` is the current 3 min */
    uint8_t live;               /**< its code now; REPLAY_ABSENT once it left the ring */
    bool used;
} replay_track_t;

typedef struct {
    replay_track_t t[REPLAY_TRACKS];
    uint32_t step_ms; /**< when the current column began */
    uint16_t serial;  /**< counts columns, for eviction */
    uint8_t head;     /**< the current column */
    uint8_t filled;   /**< columns recorded since start, up to REPLAY_STEPS */
    bool started;
    bool lit;         /**< the display as last fed */
    uint32_t dark_ms; /**< when it went dark */
    bool playing;
    uint8_t play_head; /**< head when the replay began: step k reads column play_head + 1 + k */
    uint32_t play_ms;  /**< when it began */
} replay_t;

/** @brief Forget everything; the display counts as lit. */
void replay_init(replay_t *r);
/**
 * @brief Record the ring as it is now (once a step).
 * @param[in] segs The capsules on the ring. May be NULL when @p n is 0.
 * @param[in] n    How many.
 */
void replay_note(replay_t *r, const replay_seg_t *segs, int n, uint32_t now_ms);
/**
 * @brief Feed whether the display is lit (once a step).
 * @return true on the first lit step after at least REPLAY_AWAY_MS dark: the first look.
 */
bool replay_woke(replay_t *r, bool lit, uint32_t now_ms);
/**
 * @brief The code of session @p id at replay step @p step (0: an hour ago, REPLAY_STEPS - 1: the
 *        current 3 min), as of when the replay began (or now, when not playing).
 * @return A code, REPLAY_ABSENT, or REPLAY_UNKNOWN (no track, or before the watch recorded).
 */
uint8_t replay_code(const replay_t *r, uint32_t id, int step);
/**
 * @brief How many times the capsules @p segs changed over the hour, ending at how they are now.
 *        An unknown step is not a change.
 */
int replay_changes(const replay_t *r, const replay_seg_t *segs, int n);
/** @brief Start playing at @p now_ms (the history stays frozen at its current column). */
void replay_start(replay_t *r, uint32_t now_ms);
/** @brief Stop at once. */
void replay_stop(replay_t *r);
/**
 * @brief Where the replay is.
 * @return REPLAY_OFF when not playing (it stops itself after the fade), a step 0..REPLAY_STEPS - 1,
 *         or REPLAY_STEPS during the fade (the capsules are now again).
 */
int replay_frame(replay_t *r, uint32_t now_ms);
/** @brief The time arc's length at @p frame, degrees: 18 a step, 360 from the last step on. */
int replay_arc_deg(int frame);
/** @brief The time arc's opacity at @p now_ms, percent: 100 while playing, then down to 0 over
 *         the fade. */
int replay_arc_pct(const replay_t *r, uint32_t now_ms);
