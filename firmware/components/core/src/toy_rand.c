#include "toy_rand.h"

#define TOY_RAND_NONZERO 0x9e3779b9u /* the golden ratio's bits: any non-zero value works */

void toy_rand_seed(toy_rand_t *r, uint32_t seed) { r->s = seed ? seed : TOY_RAND_NONZERO; }

uint32_t toy_rand_next(toy_rand_t *r)
{
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

void toy_rand_stir(toy_rand_t *r, uint32_t entropy)
{
    r->s ^= entropy * TOY_RAND_NONZERO; /* spread the low bits a clock varies in over the word */
    if (!r->s) r->s = TOY_RAND_NONZERO;
    (void)toy_rand_next(r);
}

uint32_t toy_rand_below(toy_rand_t *r, uint32_t n)
{
    if (!n) return 0;
    /* 2^32 is no multiple of n: drop the top 2^32 mod n values, which favour the low ones */
    uint32_t limit = UINT32_MAX - (UINT32_MAX % n + 1) % n;
    uint32_t x;
    do {
        x = toy_rand_next(r);
    } while (x > limit);
    return x % n;
}
