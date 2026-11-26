/* The Mote (issue 20261126-59e0, GLYPHS.md §3.13): a point of light on its own track of the idle
 * face that mirrors the fleet. It never asks, buzzes or sounds; it leaves the moment anything
 * needs you, and it lives only on the dock (the persona gate's PA_LOOP).
 *
 * Pure: mote_step() turns what the fleet is doing and the time into where the light is, how long
 * its tail is and how bright it is. Highest priority first: GONE (not allowed, or the idle face is
 * not up), HOVER at an error, CHEER (three laps on a big finish) or LOOP (the centuries egg), LAP
 * (something is working: a seconds hand), SETTLE (nothing running: at 6 o'clock, pulsing), EMBER
 * (settled five minutes: a small dim dot). It never moves faster than 6°/s except in CHEER, LOOP
 * and when it catches up with the seconds or an error (up to 24°/s), so a change never pops. The
 * level (the tail's length) grows by days with green tests and never shrinks. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MOTE_TRACK_R 216 /* the track's centre line: between the capsules (222) and the pips */
#define MOTE_HEAD_PX 8   /* the head's diameter */
#define MOTE_EMBER_PX 4
#define MOTE_CHEER_PX 12 /* the one frame after the cheer's laps */
#define MOTE_TAIL_W 4
#define MOTE_TAIL_DEG 14 /* at level 0; MOTE_TAIL_STEP more per level */
#define MOTE_TAIL_STEP 6
#define MOTE_TAIL_MIN 8 /* tired (plan use MOTE_TIRED_PCT or more) halves it to this at least */
#define MOTE_CHEER_TAIL 60
#define MOTE_TIRED_PCT 80
#define MOTE_LUM 45       /* % of full: the ambient face's brightness */
#define MOTE_LUM_NIGHT 30 /* 23:00 to 06:00, amber */
#define MOTE_LUM_EMBER 25
#define MOTE_SETTLE_DEG 180
#define MOTE_EMBER_MS 300000u /* settled this long: an ember */
#define MOTE_PULSE_MS 4000u   /* settled: a pulse of 4 levels; 6 s at night */
#define MOTE_PULSE_NIGHT_MS 6000u
#define MOTE_HOVER_SWAY 3 /* degrees either side of the error, at 1 Hz */
#define MOTE_FLICKER_MS 600u
#define MOTE_CALM_DPS 6     /* degrees a second: settling */
#define MOTE_CATCH_DPS 24   /* the most it ever moves outside CHEER and LOOP */
#define MOTE_CHEER_MS 1800u /* three laps */
#define MOTE_CHEER_HOLD_MS 90u
#define MOTE_LOOP_MS 1200u /* the centuries egg: once round a circle inside the ring */
#define MOTE_LOOP_R 20
#define MOTE_LEVEL_MAX 7

/** What the light is doing. */
typedef enum {
    MOTE_GONE = 0, /**< not drawn */
    MOTE_HOVER,    /**< at an error, swaying and flickering */
    MOTE_CHEER,    /**< three fast laps on a big finish */
    MOTE_LOOP,     /**< the centuries egg: a loop off the track */
    MOTE_LAP,      /**< something works: a seconds hand */
    MOTE_SETTLE,   /**< nothing runs: at 6 o'clock, pulsing */
    MOTE_EMBER,    /**< settled five minutes: small, dim, still */
} mote_mode_t;

/** The moment, as the screen sees it. */
typedef struct {
    bool face;          /**< the idle face is up, with its clock */
    bool may;           /**< the gate lets it exist: PA_LOOP with reduced motion set aside */
    bool reduced;       /**< reduced motion: a still light at its mode's resting place */
    uint8_t working;    /**< sessions working or starting */
    uint8_t errors;     /**< sessions in error */
    int16_t error_deg;  /**< the first error capsule's centre, degrees clockwise from 12 */
    uint32_t minute_ms; /**< ms into the local minute, 0..59999 */
    uint8_t hour;       /**< local hour, 0..23; 255 unknown */
    uint8_t usage;      /**< the 5-hour plan use, percent; 255 unknown */
    uint8_t level;      /**< mote_level() of the green days */
    uint32_t cheers;    /**< big finishes seen so far; a change cheers */
    uint32_t loops;     /**< loops asked for so far (eggs); a change loops */
} mote_in_t;

/** What to draw. */
typedef struct {
    mote_mode_t mode; /**< the mode it is in */
    bool amber;       /**< the night colour */
    uint8_t lum;      /**< the head's brightness, % of full; the tail is 100/55/25 % of it */
    uint8_t size;     /**< the head's diameter, px */
    uint8_t tail_deg; /**< the tail's length behind the head (counter-clockwise); 0 none */
    uint16_t deg;     /**< the head on the track, whole degrees clockwise from 12 */
    int8_t dx, dy;    /**< the head off the track (the loop), px */
} mote_out_t;

/** The light's state between steps; zero it to start. */
typedef struct {
    mote_mode_t mode; /**< the mode last step */
    mote_mode_t base; /**< the mode under any effect */
    bool shown;       /**< drawn last step */
    bool locked;      /**< caught up with its target */
    int32_t pos;      /**< millidegrees clockwise from 12, 0..359999 */
    uint32_t last_ms; /**< the last step */
    uint32_t since;   /**< the base mode began */
    uint32_t idle_ms; /**< nothing has run since (SETTLE, then EMBER) */
    uint32_t fx_ms;   /**< the cheer or loop began */
    uint32_t cheers;  /**< the last count seen */
    uint32_t loops;   /**< the last count seen */
    mote_mode_t fx;   /**< MOTE_CHEER, MOTE_LOOP or MOTE_GONE (none) */
    bool synced;      /**< the counts have been read once */
} mote_t;

/**
 * @brief Advance the light to @p now_ms.
 * @param[in,out] m  Its state. Must not be NULL.
 * @param[in]     in The moment. Must not be NULL.
 * @return What to draw; mode MOTE_GONE draws nothing.
 */
mote_out_t mote_step(mote_t *m, const mote_in_t *in, uint32_t now_ms);

/** @brief The level for @p days green days: thresholds 1, 3, 7, 14, 30, 60, 120 give 1..7. */
uint8_t mote_level(uint32_t days);

/**
 * @brief Count a green day in the stored word (bits 0-15 the count, 16-31 the last day counted).
 * @param[in] word The word as stored (0 at first).
 * @param[in] day  The local day number (local unix seconds / 86400).
 * @return The word to store: counted once per day, only forward, never less.
 */
uint32_t mote_note_day(uint32_t word, uint32_t day);
/** @brief The green days a stored word holds. */
uint32_t mote_days(uint32_t word);

/** A finished turn, from its done summary; -1 for a figure not known. */
typedef struct {
    bool error;    /**< it ended in error */
    int8_t tests;  /**< 1 passed, 0 failed, -1 unknown */
    int32_t secs;  /**< how long the turn ran */
    int32_t files; /**< files changed */
    int32_t lines; /**< lines added and removed */
} mote_finish_t;

/** @brief A finish with its tests green (counts a day). */
bool mote_finish_green(const mote_finish_t *f);
/** @brief A big finish (cheers): not an error, tests not failed, and 10 minutes or more, 5 files
 * or more, or 200 lines or more (the medium and large tiers of the celebrations brief). */
bool mote_finish_big(const mote_finish_t *f);
