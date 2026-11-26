/* A sigil's temperament (issue 20261126-98a6, GLYPHS.md §3.11): how a project's mark moves, from a
 * second slice of the same SHA-256 that draws it. The shape uses bytes 0..9, the character bytes
 * 10..19, so look and behaviour are independent but both fixed by the name.
 *
 * Pure. temper_pose() is the motion grammar for the mark: breath (brightness and a middle-ring
 * sway; at r 22 a scale is invisible), dip-settle, shiver, lean-then-still, droop. temper_hero()
 * is the same grammar for the hero glyph (one-shots only: it is too large to loop), temper_joy()
 * the radial burst of a finish. Nothing is random: the same name and the same event always give
 * the same gesture. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TEMPER_HASH 10         /* bytes of character: digest bytes 10..19 */
#define TEMPER_PERIOD_MIN 3000 /* breath period, ms */
#define TEMPER_PERIOD_MAX 6000
#define TEMPER_LUM_LOW 72 /* % brightness at the bottom of a working breath */
#define TEMPER_SWAY_MIN 2 /* degrees, middle ring */
#define TEMPER_SWAY_MAX 6
#define TEMPER_REACH 4    /* px a pose may move the mark's edge: the object holds r + 2 + this */
#define TEMPER_JOY_MS 450 /* the radial burst of a finish */
#define TEMPER_GESTURE_MS 300 /* the longest one-shot of a mark */

/** What a mark shows; the screen maps its session to one. */
typedef enum {
    TEMPER_STILL = 0, /**< ended, offline, nothing: as drawn */
    TEMPER_WORK,      /**< working or starting: the breath */
    TEMPER_IDLE,      /**< idle: a slower, shallower breath, no sway */
    TEMPER_WANTS,     /**< needs you: lean toward the hero glyph, then still */
    TEMPER_DONE,      /**< finished: dip and settle */
    TEMPER_ERROR,     /**< failed: a shiver */
    TEMPER_ASLEEP,    /**< snoozed: droop (dim, outer ring turned) */
} temper_state_t;

/** A mark's temperament. */
typedef struct {
    uint16_t period_ms;      /**< breath period, TEMPER_PERIOD_MIN..MAX */
    uint16_t sway_period_ms; /**< 3/2 or 2 breath periods: the sway drifts against the breath */
    uint16_t phase_ms;       /**< offset into the period: two marks never breathe together */
    uint8_t sway_deg;        /**< middle-ring sway amplitude, TEMPER_SWAY_MIN..MAX */
    int8_t dir;              /**< +1 / -1: which way the sway starts and the gestures turn */
    uint8_t spark_deg;       /**< rotation of the finish's sparks, 0..44 */
} temper_t;

/** Where a mark is drawn, relative to its rest. */
typedef struct {
    int8_t dx, dy;  /**< centre offset, px (a lean is +dy: the caller turns it to its hero) */
    int8_t dr;      /**< radius change, px */
    int8_t rot_mid; /**< middle ring turned, degrees clockwise */
    int8_t rot_out; /**< outer ring turned, degrees clockwise */
    uint8_t lum;    /**< brightness, % of the mark's colour */
} temper_pose_t;

extern const temper_pose_t temper_neutral;

/**
 * @brief Build the key a session's mark is hashed from (ui_sigil_set_session uses the same).
 * @param[in]  name Folder name; NULL or "" for none.
 * @param[in]  sid  Session id; with no name, the key is "\x01sid N" (no folder name can be it).
 * @param[out] key  The key. Must not be NULL.
 * @param[in]  n    Size of @p key (16 or more).
 * @return false when there is neither a name nor a sid (key is "").
 */
bool temper_key(const char *name, uint32_t sid, char *key, size_t n);
/** @brief Bytes 10..19 of the SHA-256 of the session's key (temper_key()). */
void temper_hash(const char *name, uint32_t sid, uint8_t t[TEMPER_HASH]);
/** @brief The temperament the character bytes give. */
temper_t temper_params(const uint8_t t[TEMPER_HASH]);
/** @brief temper_params(temper_hash(name, sid)). */
temper_t temper_of(const char *name, uint32_t sid);

/**
 * @brief The mark's pose in state @p st.
 * @param[in] tp       The temperament. Must not be NULL.
 * @param[in] st       What the mark shows.
 * @param[in] since_ms Time in that state (a one-shot plays from 0).
 * @param[in] now_ms   Clock for the loops.
 * @param[in] allow    persona_allow() bits: without PA_POSE neutral; without PA_ONESHOT a
 *                     gesture shows its end; without PA_LOOP no breath.
 * @param[in] r        The mark's outer radius, px.
 * @return The pose; it never reaches more than TEMPER_REACH px past the rest.
 */
temper_pose_t temper_pose(const temper_t *tp, temper_state_t st, uint32_t since_ms, uint32_t now_ms,
                          unsigned allow, int r);
/** @brief The hero glyph's offset (dx, dy) in state @p st: dip-settle on a finish, a shiver on an
 * error, otherwise still. Needs PA_ONESHOT. */
temper_pose_t temper_hero(const temper_t *tp, temper_state_t st, uint32_t since_ms, unsigned allow);
/** @brief Progress of a finish's radial burst, 0..1000, or -1 when none shows. */
int temper_joy(temper_state_t st, uint32_t since_ms, unsigned allow);
