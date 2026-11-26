/* The personality gate (issue 20261126-98a6, GLYPHS.md §3.11): every fun feature asks
 * persona_allow() what it may do right now, and tests none of the conditions itself. Pure: the glue
 * (app.c) fills a persona_ctx_t once per step from the model, power, motion and the stored `fun`
 * word; a feature copies it, sets the fields that belong to its own event and asks.
 *
 * The seven guardrails, as rules here: alerts always win (anything that needs you stops fun at
 * once); fun never starts a buzz or a sound (only the user or an event that already alerts does);
 * continuous motion only on USB power; reduced motion honoured; face down and quiet respected. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/** One bit per fun feature in the stored word (prefs_fun()), plus reduced motion. */
enum {
    FUN_TEMPER = 1u << 0,    /**< Living marks: the sigil's temperament and the motion grammar */
    FUN_CELEBRATE = 1u << 1, /**< right-sized celebrations (off: today's single finish) */
    FUN_MOTIF = 1u << 2,     /**< a three-note motif per project under the done cue */
    FUN_MOTE = 1u << 3,      /**< the Mote: a light on the idle ring */
    FUN_HELLO = 1u << 4,     /**< morning hello and evening close */
    FUN_REPLAY = 1u << 5,    /**< while you were away: the last hour replayed */
    FUN_EGGS = 1u << 6,      /**< a few rare easter eggs */
    FUN_TRAIL = 1u << 7,     /**< the trail of days */
    FUN_CUP = 1u << 8,       /**< the break cup */
    FUN_TOYS = 1u << 9,      /**< coin, die and oracle in Experiments */
    FUN_REDUCED = 1u << 15,  /**< reduced motion: no loops, one-shots show their end frame */
};
/** What a watch that was never set has: the house style on, the auditions off. */
#define FUN_DEFAULT \
    (FUN_TEMPER | FUN_CELEBRATE | FUN_HELLO | FUN_EGGS | FUN_TRAIL | FUN_CUP | FUN_TOYS)
/** Every bit a build knows; prefs drops the others. */
#define FUN_KNOWN (0x3FFu | FUN_REDUCED)

/** A fun feature; its bit in the word is 1 << the value. */
typedef enum {
    PF_TEMPER = 0,
    PF_CELEBRATE,
    PF_MOTIF,
    PF_MOTE,
    PF_HELLO,
    PF_REPLAY,
    PF_EGGS,
    PF_TRAIL,
    PF_CUP,
    PF_TOYS,
    PF_COUNT
} persona_feat_t;

/** What a feature may do: the bits persona_allow() returns. */
enum {
    PA_POSE = 1,    /**< a static pose that shows state (the lean's end, the droop) */
    PA_ONESHOT = 2, /**< a short gesture (at most 900 ms) */
    PA_LOOP = 4,    /**< continuous motion (a breath) */
    PA_HAPTIC = 8,  /**< a buzz */
    PA_SOUND = 16,  /**< a sound */
};

/** The moment a feature asks about. app.c fills the shared part; a feature sets its own. */
typedef struct {
    uint8_t needs_you; /**< requests waiting and sessions waiting for input; > 0 stops fun */
    uint8_t alert;     /**< alert_mode_t: 0 full, 1 quiet, 2 silent */
    bool quiet;        /**< quiet mode is on (model_quiet_active) */
    bool face_down;    /**< the watch lies face down: silence */
    bool usb;          /**< on external power: the only time anything loops */
    bool display_on;   /**< lit and not just glowing: nothing personal wakes the display */
    bool ambient;      /**< the dimmed idle face is up */
    bool hush;         /**< the user is looking at the event's terminal (PRESENCE.md) */
    bool user_started; /**< the user started this (a toy, a timer they set): may buzz and sound */
    bool alerting;     /**< the gesture belongs to an event that already alerts (a request's own
                            lean, a finish that buzzes by app.c's rule) */
    uint32_t fun;      /**< the stored word: FUN_* bits */
} persona_ctx_t;

/**
 * @brief Decide what feature @p f may do in the moment @p c.
 *
 * Rules in order: the feature's bit off, or the display dark: nothing. Something needs you: a
 * finish keeps only its buzz and sound, the asking mark keeps its gesture (alerting), all else
 * keeps at most its static pose. Face down, silent, hush: no buzz, no sound. Quiet: no sound, a
 * buzz only when the user started it or the event alerts. Battery: no loop. Reduced motion: no
 * loop and no one-shot (the caller shows the end frame). Neither started by the user nor riding
 * on an alert: no buzz, no sound.
 *
 * @param[in] f The feature asking; PF_COUNT or above gets 0.
 * @param[in] c The moment. Must not be NULL.
 * @return PA_* bits.
 */
unsigned persona_allow(persona_feat_t f, const persona_ctx_t *c);

/** @brief The bit of feature @p f in the stored word (0 for PF_COUNT or above). */
uint32_t persona_bit(persona_feat_t f);
