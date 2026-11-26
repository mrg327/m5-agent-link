/* The Mote (issue 20261126-59e0, GLYPHS.md §3.14): a firefly on its own track inside the idle
 * face's ring that shows who is busy. It never asks, buzzes or sounds; it leaves the moment
 * anything needs you, and it lives only on the dock (the persona gate's PA_LOOP).
 *
 * Pure: mote_step() turns what the fleet is doing and the time into where the firefly is and how it
 * looks. Highest priority first: GONE (not allowed, or the idle face is not up), HOVER beside the
 * first error capsule, CHEER (a big finish: it drifts to the finished session's arc, loops once
 * round a 40 px circle, flares and lingers there) or LOOP (the centuries egg), VISIT (it drifts to
 * each working session's arc in turn, ring order, and lingers by it), REST (nothing runs: at 12
 * o'clock, under the ring's gap), EMBER (rested five minutes: small, dim, still). It travels the
 * short way, eased, at a calm speed; only the loops move faster. Its halo breathes where it may
 * loop. Its look never changes under the user: the level (days with green tests) is counted and
 * never shrinks, but is not shown yet.
 *
 * The look is reusable apart from this behaviour: mote_look() and mote_breath() give the firefly
 * at any size, and ui_orb_draw() (ui_orb.h) paints it anywhere, for the mascot on other screens. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "mote_look.h"

/* The track's centre line, inside the ring: the core (r 168-182) clears the usage gauges' glyphs
 * at 6 o'clock (r 184 and out), their beads (187) and arcs (191-197), the needs-you pips (196-210)
 * and the capsules (222-228); the halo's inner edge (r 145) clears the ambient clock's digits. */
#define MOTE_TRACK_R 175
/* The look comes from the shared profile (design/mote/mote.toml, generated into mote_look.h): the
 * idle face's firefly is size M (core r 7, halo to r 30), its levels and breath are the profile's.
 */
#define MOTE_RGB MOTE_LOOK_RGB               /* warm white, day and night: colour is for state */
#define MOTE_CORE_R (MOTE_LOOK_M_CORE_D / 2) /* the core's radius at scale 100 */
#define MOTE_HALO_R (MOTE_LOOK_M_HALO_D / 2) /* the halo's outer radius at scale 100 */
#define MOTE_BREATH_MS MOTE_LOOK_BREATH_MS
#define MOTE_BREATH_TIRED_MS 6000u /* plan use MOTE_TIRED_PCT or more: slower */
#define MOTE_BREATH_STEPS 8        /* levels a breath is sampled in: at most 14 repaints a breath */
#define MOTE_TIRED_PCT 80
#define MOTE_TIRED_HALO 70 /* tired: the halo at this % (dimmer) */
#define MOTE_NIGHT_HALO 70 /* 23:00 to 06:00: the halo at this % (the core at MOTE_LUM_NIGHT) */
/** A profile level (0..255) as a percentage of full. */
#define MOTE_LEVEL_PCT(level) (((level) * 100 + 127) / 255)
#define MOTE_LUM_LIT MOTE_LEVEL_PCT(MOTE_LOOK_LEVEL_LIT) /* the lit deck: full */
#define MOTE_LUM MOTE_LEVEL_PCT(MOTE_LOOK_LEVEL_IDLE)    /* the ambient face's brightness */
#define MOTE_LUM_NIGHT MOTE_LEVEL_PCT(MOTE_LOOK_LEVEL_NIGHT)
#define MOTE_LUM_EMBER MOTE_LEVEL_PCT(MOTE_LOOK_LEVEL_EMBER)
/* the flare's peak, from MOTE_LUM (in proportion at night): the profile's lit level */
#define MOTE_LUM_FLARE MOTE_LUM_LIT
#define MOTE_EMBER_R (MOTE_LOOK_EMBER_CORE_D / 2) /* the ember: this core and no halo */
#define MOTE_EMBER_MS 300000u                     /* rested this long: an ember */
#define MOTE_REST_DEG 0     /* 12 o'clock: under the ring's gap, clear of the gauges below */
#define MOTE_VISIT_MS 8000u /* it lingers this long by each working session */
#define MOTE_VISIT_MAX 16
#define MOTE_TRAVEL_MS 800u    /* a trip takes this plus MOTE_TRAVEL_MS_DEG a degree, eased */
#define MOTE_TRAVEL_MS_DEG 20u /* 180 degrees in 4.4 s: at most 62 degrees a second */
#define MOTE_HOVER_SWAY 3      /* degrees either side of the error, at 1 Hz */
#define MOTE_FLICKER_MS 600u
/* A big finish (issue 20261126-2de2): an ordinary trip to the finished arc, then one loop round a
 * 40 px circle tangent to its place, inside the track, eased; then the flare; then it lingers. */
#define MOTE_FIN_LOOP_MS 600u
#define MOTE_FIN_LOOP_R 20
#define MOTE_FIN_LINGER_MS 8000u /* = MOTE_VISIT_MS: by the finished arc, then on */
/* The flare (the profile's [flare], design/mote/mote.toml): the halo to 1.6 times its radius and
 * full strength, and the core to its peak, in 120 ms; back in 480 ms, easing out. */
#define MOTE_FLARE_RISE_MS ((uint32_t)MOTE_LOOK_FLARE_RISE_MS)
#define MOTE_FLARE_FALL_MS ((uint32_t)MOTE_LOOK_FLARE_FALL_MS)
#define MOTE_FLARE_MS (MOTE_FLARE_RISE_MS + MOTE_FLARE_FALL_MS)
#define MOTE_FLARE_PCT MOTE_LOOK_FLARE_SCALE_PCT
#define MOTE_LOOP_MS 1200u /* the centuries egg: once round a circle inside the track */
#define MOTE_LOOP_R 16
#define MOTE_LEVEL_MAX 7

/** What the firefly is doing. */
typedef enum {
    MOTE_GONE = 0, /**< not drawn */
    MOTE_HOVER,    /**< beside an error, swaying and flickering */
    MOTE_CHEER,    /**< a big finish: to its arc, one loop and a flare (then it lingers) */
    MOTE_LOOP,     /**< the centuries egg: a loop off the track */
    MOTE_VISIT,    /**< by a working session's arc, then the next one's */
    MOTE_REST,     /**< nothing runs: at 12 o'clock, breathing */
    MOTE_EMBER,    /**< rested five minutes: small, dim, still */
} mote_mode_t;

/** How the firefly looks: a core and a halo that falls off to nothing at halo_r. */
typedef struct {
    uint8_t core_r;   /**< the core's radius, px */
    uint8_t halo_r;   /**< the halo's outer radius, px (from the centre) */
    uint8_t lum;      /**< the whole orb's brightness (its level), % of full */
    uint8_t halo_pct; /**< the halo's alpha, % of the profile's falloff (mote_look.h); 0 none */
} mote_look_t;

/** The moment, as the screen sees it. */
typedef struct {
    bool face;      /**< the idle face is up, with its clock */
    bool may;       /**< the gate lets it exist: PA_LOOP with reduced motion set aside */
    bool reduced;   /**< reduced motion: a still firefly at its resting place */
    uint8_t n_work; /**< working sessions on the ring, at most MOTE_VISIT_MAX */
    /** Their slots' centres, degrees clockwise from 12, in ring order. */
    int16_t work_deg[MOTE_VISIT_MAX];
    uint8_t errors;    /**< sessions in error */
    int16_t error_deg; /**< the first error capsule's centre, degrees clockwise from 12 */
    uint8_t hour;      /**< local hour, 0..23; 255 unknown */
    uint8_t usage;     /**< the 5-hour plan use, percent; 255 unknown */
    uint32_t cheers;   /**< big finishes seen so far; a change cheers */
    bool fin_at;       /**< the latest big finish's session has a slot of its own on the ring */
    int16_t fin_deg;   /**< its slot's centre, degrees clockwise from 12 (with fin_at) */
    uint32_t loops;    /**< loops asked for so far (eggs); a change loops */
} mote_in_t;

/** What to draw. */
typedef struct {
    mote_mode_t mode; /**< the mode it is in */
    bool night;       /**< 23:00 to 06:00: dimmer */
    uint32_t rgb;     /**< its colour (MOTE_RGB) */
    mote_look_t look; /**< its core and halo */
    uint16_t deg;     /**< on the track, whole degrees clockwise from 12 */
    int8_t dx, dy;    /**< off the track (the loop), px */
} mote_out_t;

/** The firefly's state between steps; zero it to start. */
typedef struct {
    mote_mode_t mode;   /**< the mode last step */
    mote_mode_t base;   /**< the mode under any effect */
    bool shown;         /**< drawn last step */
    bool synced;        /**< the counts have been read once */
    bool travelling;    /**< on its way to target */
    bool arrived;       /**< at target since arrive_ms */
    int16_t target;     /**< where it is going or is, whole degrees; -1 none */
    int32_t pos;        /**< millidegrees clockwise from 12, 0..359999 */
    int32_t from;       /**< the trip's start, millidegrees */
    int32_t to;         /**< the trip's end, millidegrees */
    uint32_t t0;        /**< the trip began */
    uint32_t dur;       /**< the trip's length, ms */
    uint32_t arrive_ms; /**< it reached target */
    uint32_t idle_ms;   /**< nothing has run since (REST, then EMBER) */
    uint32_t fx_ms;     /**< the cheer's loop (once there) or the egg's loop began */
    int16_t fin_deg;    /**< the cheer's place: the finished arc, whole degrees */
    bool fin_here;      /**< the cheer has reached its place (fx_ms is set) */
    uint32_t cheers;    /**< the last count seen */
    uint32_t loops;     /**< the last count seen */
    mote_mode_t fx;     /**< MOTE_CHEER, MOTE_LOOP or MOTE_GONE (none) */
} mote_t;

/**
 * @brief Advance the firefly to @p now_ms.
 * @param[in,out] m  Its state. Must not be NULL.
 * @param[in]     in The moment. Must not be NULL.
 * @return What to draw; mode MOTE_GONE draws nothing.
 */
mote_out_t mote_step(mote_t *m, const mote_in_t *in, uint32_t now_ms);

/**
 * @brief The firefly's look at a size, a brightness and a breath.
 * @param[in] scale_pct 100 for size M, the idle face's firefly (core r 7, halo r 30); 200 for L.
 * @param[in] lum       The orb's level, % of full.
 * @param[in] breath_pm The breath, 0 (exhaled) to 1000 (inhaled): the halo's alpha only, 60 to
 *                      100 % (the profile's breath), never its radius.
 */
mote_look_t mote_look(uint16_t scale_pct, uint8_t lum, uint16_t breath_pm);

/**
 * @brief The flare at @p t_ms after it began: from @p base to its peak in MOTE_FLARE_RISE_MS (the
 * core at @p peak_lum, the halo at MOTE_FLARE_PCT of its radius and full strength), back to
 * @p base over MOTE_FLARE_FALL_MS, easing out; @p base itself from MOTE_FLARE_MS on.
 * @param[in] base     The look it flares from (mote_look()).
 * @param[in] peak_lum The core's brightness at the peak, % of full.
 */
mote_look_t mote_flare(mote_look_t base, uint8_t peak_lum, uint32_t t_ms);

/**
 * @brief The breath at @p t_ms: 0..1000 and back over @p period_ms, in MOTE_BREATH_STEPS levels.
 */
uint16_t mote_breath(uint32_t t_ms, uint32_t period_ms);

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
