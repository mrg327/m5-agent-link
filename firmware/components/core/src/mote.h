/* The Mote (issue 20261126-59e0, GLYPHS.md §3.14): a firefly on its own track inside the idle
 * face's ring that shows who is busy. It never asks, buzzes or sounds; it leaves the moment
 * anything needs you, and it lives only on the dock (the persona gate's PA_LOOP).
 *
 * Pure: mote_step() turns what the fleet is doing and the time into where the firefly is and how it
 * looks. Highest priority first: GONE (not allowed, or the idle face is not up), HOVER beside the
 * first error capsule, CHEER (one quick loop round the track and a flare, on a big finish) or LOOP
 * (the centuries egg), VISIT (it drifts to each working session's arc in turn, ring order, and
 * lingers by it), REST (nothing runs: at 12 o'clock, under the ring's gap), EMBER (rested five
 * minutes: small, dim, still). It travels the short way, eased, at a calm speed; only CHEER and
 * LOOP move faster. Its halo breathes where it may loop. Its look never changes under the user:
 * the level (days with green tests) is counted and never shrinks, but is not shown yet.
 *
 * The look is reusable apart from this behaviour: mote_look() and mote_breath() give the firefly
 * at any size, and ui_orb_draw() (ui_orb.h) paints it anywhere, for the mascot on other screens. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The track's centre line, inside the ring: the core (r 168-182) clears the usage gauges' glyphs
 * at 6 o'clock (r 184 and out), their beads (187) and arcs (191-197), the needs-you pips (196-210)
 * and the capsules (222-228); the halo's inner edge (r 145) clears the ambient clock's digits. */
#define MOTE_TRACK_R 175
#define MOTE_RGB 0xFFE2B8 /* warm white, day and night: colour is for state */
#define MOTE_CORE_R 7     /* the core's radius at scale 100 */
#define MOTE_HALO_R 26    /* the halo's outer radius, exhaled; +MOTE_BREATH_PX inhaled */
#define MOTE_HALO_PCT 80  /* the halo's peak, % of the core's brightness */
#define MOTE_BREATH_PX 4
#define MOTE_BREATH_LO 70 /* the halo exhaled, % of its brightness inhaled */
#define MOTE_BREATH_MS 3600u
#define MOTE_BREATH_TIRED_MS 6000u /* plan use MOTE_TIRED_PCT or more: slower */
#define MOTE_BREATH_STEPS 8        /* levels a breath is sampled in: at most 14 repaints a breath */
#define MOTE_TIRED_PCT 80
#define MOTE_TIRED_HALO 70 /* tired: the halo at this % (dimmer) */
#define MOTE_NIGHT_HALO 70 /* 23:00 to 06:00: the halo at this % (the core at MOTE_LUM_NIGHT) */
#define MOTE_LUM 45        /* % of full: the ambient face's brightness */
#define MOTE_LUM_NIGHT 28
#define MOTE_LUM_EMBER 22
#define MOTE_LUM_FLARE 90 /* the cheer's flare, falling back to MOTE_LUM */
#define MOTE_EMBER_SCALE 60
#define MOTE_EMBER_MS 300000u /* rested this long: an ember */
#define MOTE_REST_DEG 0       /* 12 o'clock: under the ring's gap, clear of the gauges below */
#define MOTE_VISIT_MS 8000u   /* it lingers this long by each working session */
#define MOTE_VISIT_MAX 16
#define MOTE_TRAVEL_MS 800u    /* a trip takes this plus MOTE_TRAVEL_MS_DEG a degree, eased */
#define MOTE_TRAVEL_MS_DEG 20u /* 180 degrees in 4.4 s: at most 62 degrees a second */
#define MOTE_HOVER_SWAY 3      /* degrees either side of the error, at 1 Hz */
#define MOTE_FLICKER_MS 600u
#define MOTE_CHEER_MS 1200u /* once round the track, eased */
#define MOTE_FLARE_MS 400u  /* then the halo flares and falls back */
#define MOTE_FLARE_PX 10
#define MOTE_LOOP_MS 1200u /* the centuries egg: once round a circle inside the track */
#define MOTE_LOOP_R 16
#define MOTE_LEVEL_MAX 7

/** What the firefly is doing. */
typedef enum {
    MOTE_GONE = 0, /**< not drawn */
    MOTE_HOVER,    /**< beside an error, swaying and flickering */
    MOTE_CHEER,    /**< one quick loop round the track and a flare, on a big finish */
    MOTE_LOOP,     /**< the centuries egg: a loop off the track */
    MOTE_VISIT,    /**< by a working session's arc, then the next one's */
    MOTE_REST,     /**< nothing runs: at 12 o'clock, breathing */
    MOTE_EMBER,    /**< rested five minutes: small, dim, still */
} mote_mode_t;

/** How the firefly looks: a core and a halo that falls off to nothing at halo_r. */
typedef struct {
    uint8_t core_r;   /**< the core's radius, px */
    uint8_t halo_r;   /**< the halo's outer radius, px (from the centre) */
    uint8_t lum;      /**< the core's brightness, % of full */
    uint8_t halo_pct; /**< the halo's peak, % of the core's brightness; 0 none */
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
    uint32_t fx_ms;     /**< the cheer or loop began */
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
 * @param[in] scale_pct 100 for the idle face's firefly (core r 7, halo r 26-30); any size.
 * @param[in] lum       The core's brightness, % of full.
 * @param[in] breath_pm The breath, 0 (exhaled) to 1000 (inhaled): the halo grows and brightens.
 */
mote_look_t mote_look(uint16_t scale_pct, uint8_t lum, uint16_t breath_pm);

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
