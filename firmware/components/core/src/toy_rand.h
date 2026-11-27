/* Random numbers for the toys (EXPERIMENTS.md "Toys"): xorshift32, stirred with what a person
 * cannot repeat (the microsecond of a key or touch edge, the last tilt sample). hal.h has no
 * hardware random source, so this needs no HAL change; on the watch the stirring is the entropy.
 * A simulator script seeds it and the toy then stops stirring, so its throws repeat each run.
 *
 * Pure. Ranges are drawn by rejection, so every value of a range is equally likely. */
#pragma once
#include <stdint.h>

typedef struct { uint32_t s; /* never 0 */ } toy_rand_t;

/** @brief Start from @p seed (0 is replaced by a fixed non-zero value). */
void toy_rand_seed(toy_rand_t *r, uint32_t seed);
/** @brief Mix @p entropy (a clock's low bits, a sensor reading) into the state. */
void toy_rand_stir(toy_rand_t *r, uint32_t entropy);
/** @brief The next 32 random bits. */
uint32_t toy_rand_next(toy_rand_t *r);
/** @brief A uniform value in 0 .. @p n - 1 (0 when @p n is 0). */
uint32_t toy_rand_below(toy_rand_t *r, uint32_t n);
