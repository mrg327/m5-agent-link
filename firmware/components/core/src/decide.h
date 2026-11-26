/* Decision toys (EXPERIMENTS.md "Toys", issue 20261126-74ee): a coin, a die and an oracle, for
 * "tabs or spaces, who reviews, which task first". Five seconds, then back to work.
 *
 * Pure, with an explicit clock: which toy is up, the throw's timeline and its result (toy_rand.h).
 * The result is drawn when the throw starts; the timeline only shows it arriving.
 * - Coin: turns five or six half-turns about its vertical axis in DECIDE_COIN_MS, slowing, and
 *   lands on a tick or a cross.
 * - Die: 4, 6, 8, 12 or 20 sides. The number rattles through DECIDE_RATTLES values at growing
 *   intervals (DECIDE_RATTLE_FIRST_MS to DECIDE_RATTLE_LAST_MS), then lands.
 * - Oracle: held for DECIDE_HOLD_MS (a finger, key A, or the watch face up and still), then one
 *   of DECIDE_ANSWERS answers rises DECIDE_RISE_PX into place.
 *
 * A pause (a request takes the screen) freezes the timeline where it is and drops an oracle hold;
 * resume carries on from there. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "toy_rand.h"

typedef enum { DECIDE_COIN, DECIDE_DIE, DECIDE_ORACLE, DECIDE_KINDS } decide_kind_t;

typedef enum {
    DECIDE_READY,    /* nothing thrown yet (or the die's sides changed) */
    DECIDE_THROWING, /* coin turning, die rattling */
    DECIDE_HOLDING,  /* the oracle is being held */
    DECIDE_SHOWN,    /* the result is up */
} decide_phase_t;

#define DECIDE_COIN_MS 1000
#define DECIDE_COIN_HALF_TURNS 5 /* or one more, so it lands on the face drawn */
#define DECIDE_DICE 5            /* the dice the bezel picks from */
#define DECIDE_DIE_DEFAULT 1     /* six sides */
#define DECIDE_RATTLES 9
#define DECIDE_RATTLE_FIRST_MS 60
#define DECIDE_RATTLE_LAST_MS 240
#define DECIDE_HOLD_MS 2000
#define DECIDE_ANSWERS 6
#define DECIDE_RISE_MS 300
#define DECIDE_RISE_PX 20

/* What decide_step() saw happen since the last call. */
enum {
    DECIDE_EV_TURN = 1, /* the coin showed its other face, or the die a new number */
    DECIDE_EV_LAND = 2, /* the throw landed, or the oracle answered */
};

/* The coin's faces. */
enum { DECIDE_FACE_TICK = 0, DECIDE_FACE_CROSS = 1 };

typedef struct {
    toy_rand_t rng;
    decide_kind_t kind;
    decide_phase_t phase;
    bool paused;
    bool settled;             /* the result shows its end frame (no rise) */
    uint32_t t0;              /* the phase's start */
    uint32_t frozen;          /* the phase's time when paused */
    bool shown[DECIDE_KINDS]; /* each toy keeps its last result while another is up */
    uint8_t coin_face;        /* the result (the face up once it lands) */
    uint8_t coin_from;        /* the face up when the throw began */
    uint8_t coin_half;        /* half-turns of this throw */
    uint8_t coin_seen;        /* half-turns already reported */
    uint8_t die;              /* index into the dice */
    uint8_t die_value;        /* 1..sides */
    uint8_t rattle[DECIDE_RATTLES];
    uint8_t rattle_seen; /* rattle values already reported */
    uint8_t answer;      /* the oracle's answer, 0..DECIDE_ANSWERS - 1 */
} decide_t;

/** @brief The coin up, nothing thrown, the six-sided die, from @p seed (toy_rand_seed). */
void decide_init(decide_t *d, uint32_t seed);
/** @brief Sides of die @p i (4, 6, 8, 12, 20); 0 out of range. */
int decide_sides_of(int i);
int decide_sides(const decide_t *d);
/** @brief The next toy (coin, die, oracle, coin ...). Refused (false) mid-throw; drops a hold. */
bool decide_next_kind(decide_t *d);
/** @brief The die @p dir steps along the dice, clamped. False when unchanged or mid-throw. */
bool decide_step_die(decide_t *d, int dir);
/**
 * @brief Throw the coin or the die. @p entropy is stirred in first (the edge's microseconds).
 * @return false for the oracle, mid-throw or while paused.
 */
bool decide_throw(decide_t *d, uint32_t now_ms, uint32_t entropy);
/** @brief The oracle: a hold begins (at @p from_ms, which may be in the past) or ends. */
bool decide_hold(decide_t *d, bool down, uint32_t from_ms, uint32_t entropy);
/** @brief Advance to @p now_ms: DECIDE_EV_* bits. Nothing moves while paused. */
unsigned decide_step(decide_t *d, uint32_t now_ms);
/** @brief Jump the throw or the rise to its end frame (reduced motion); the land still reports. */
void decide_finish(decide_t *d, uint32_t now_ms);
void decide_pause(decide_t *d, uint32_t now_ms);
void decide_resume(decide_t *d, uint32_t now_ms);

/** @brief The coin's turn so far, in degrees (0 = the starting face square on). */
int decide_coin_deg(const decide_t *d, uint32_t now_ms);
/** @brief The face toward the viewer at turn @p deg. */
int decide_coin_face_at(const decide_t *d, int deg);
/** @brief The number the die shows now, 0 for none. */
int decide_die_shown(const decide_t *d, uint32_t now_ms);
/** @brief How far the oracle's hold ring is filled, 0..1000. */
int decide_hold_permille(const decide_t *d, uint32_t now_ms);
/** @brief How far below its place the oracle's answer is, DECIDE_RISE_PX..0. */
int decide_rise_px(const decide_t *d, uint32_t now_ms);
