#include "decide.h"

#include <string.h>

static const uint8_t s_dice[DECIDE_DICE] = {4, 6, 8, 12, 20};

int decide_sides_of(int i) { return i >= 0 && i < DECIDE_DICE ? s_dice[i] : 0; }
int decide_sides(const decide_t *d) { return s_dice[d->die]; }

void decide_init(decide_t *d, uint32_t seed)
{
    memset(d, 0, sizeof *d);
    toy_rand_seed(&d->rng, seed);
    d->kind = DECIDE_COIN;
    d->phase = DECIDE_READY;
    d->die = DECIDE_DIE_DEFAULT;
    d->settled = true;
}

static uint32_t elapsed(const decide_t *d, uint32_t now)
{ return d->paused ? d->frozen : now - d->t0; }

/* When rattle value i (0..DECIDE_RATTLES) shows; i = DECIDE_RATTLES is the landing. */
static uint32_t rattle_at(int i)
{
    uint32_t t = 0;
    for (int k = 0; k < i; k++) {
        t += DECIDE_RATTLE_FIRST_MS + (uint32_t)(DECIDE_RATTLE_LAST_MS - DECIDE_RATTLE_FIRST_MS) *
                                          (uint32_t)k / (DECIDE_RATTLES - 1);
    }
    return t;
}

/* Rattle values shown by time e: 1..DECIDE_RATTLES. */
static int rattles_by(uint32_t e)
{
    int n = 1;
    while (n < DECIDE_RATTLES && rattle_at(n) <= e) n++;
    return n;
}

static uint32_t throw_ms(const decide_t *d)
{ return d->kind == DECIDE_COIN ? DECIDE_COIN_MS : rattle_at(DECIDE_RATTLES); }

static void drop_hold(decide_t *d)
{
    if (d->phase == DECIDE_HOLDING) {
        d->phase = d->shown[DECIDE_ORACLE] ? DECIDE_SHOWN : DECIDE_READY;
    }
}

bool decide_next_kind(decide_t *d)
{
    if (d->phase == DECIDE_THROWING) return false;
    drop_hold(d);
    d->kind = (decide_kind_t)((d->kind + 1) % DECIDE_KINDS);
    d->phase = d->shown[d->kind] ? DECIDE_SHOWN : DECIDE_READY;
    d->settled = true; /* a result already up does not rise again */
    return true;
}

bool decide_step_die(decide_t *d, int dir)
{
    if (d->phase == DECIDE_THROWING) return false;
    int i = d->die + dir;
    if (i < 0) i = 0;
    if (i >= DECIDE_DICE) i = DECIDE_DICE - 1;
    if (i == d->die) return false;
    d->die = (uint8_t)i;
    d->shown[DECIDE_DIE] = false; /* a number of the old die means nothing on the new one */
    if (d->kind == DECIDE_DIE) d->phase = DECIDE_READY;
    return true;
}

bool decide_throw(decide_t *d, uint32_t now, uint32_t entropy)
{
    if (d->paused || d->kind == DECIDE_ORACLE || d->phase == DECIDE_THROWING) return false;
    toy_rand_stir(&d->rng, entropy);
    if (d->kind == DECIDE_COIN) {
        d->coin_from = d->shown[DECIDE_COIN] ? d->coin_face : DECIDE_FACE_TICK;
        d->coin_face = (uint8_t)toy_rand_below(&d->rng, 2);
        /* an odd number of half-turns shows the other face */
        d->coin_half = DECIDE_COIN_HALF_TURNS + (d->coin_face == d->coin_from ? 1 : 0);
        d->coin_seen = 0;
    } else {
        int sides = decide_sides(d);
        d->die_value = (uint8_t)(toy_rand_below(&d->rng, (uint32_t)sides) + 1);
        /* the rattle, drawn back from the result: every value differs from the one after it, so
         * each step is seen to change; the result itself stays uniform */
        int next = d->die_value;
        for (int i = DECIDE_RATTLES - 1; i >= 0; i--) {
            int v = (int)toy_rand_below(&d->rng, (uint32_t)sides - 1) + 1;
            if (v >= next) v++;
            d->rattle[i] = (uint8_t)v;
            next = v;
        }
        d->rattle_seen = 0;
    }
    d->phase = DECIDE_THROWING;
    d->t0 = now;
    d->settled = false;
    return true;
}

bool decide_hold(decide_t *d, bool down, uint32_t from, uint32_t entropy)
{
    if (d->kind != DECIDE_ORACLE || d->paused) return false;
    if (!down) {
        if (d->phase != DECIDE_HOLDING) return false;
        drop_hold(d);
        return true;
    }
    if (d->phase == DECIDE_HOLDING) return false;
    toy_rand_stir(&d->rng, entropy);
    d->phase = DECIDE_HOLDING;
    d->t0 = from;
    return true;
}

unsigned decide_step(decide_t *d, uint32_t now)
{
    if (d->paused) return 0;
    uint32_t e = elapsed(d, now);
    unsigned ev = 0;
    if (d->phase == DECIDE_HOLDING && e >= DECIDE_HOLD_MS) {
        d->answer = (uint8_t)toy_rand_below(&d->rng, DECIDE_ANSWERS);
        d->phase = DECIDE_SHOWN;
        d->shown[DECIDE_ORACLE] = true;
        d->t0 += DECIDE_HOLD_MS; /* the answer rises from when it came */
        d->settled = false;
        return DECIDE_EV_LAND;
    }
    if (d->phase != DECIDE_THROWING) return 0;
    if (d->kind == DECIDE_COIN) {
        int faces = (decide_coin_deg(d, now) + 90) / 180; /* face changes so far */
        if (faces > d->coin_seen) {
            d->coin_seen = (uint8_t)faces;
            ev |= DECIDE_EV_TURN;
        }
    } else {
        int n = rattles_by(e);
        if (n > d->rattle_seen) {
            d->rattle_seen = (uint8_t)n;
            ev |= DECIDE_EV_TURN;
        }
    }
    if (e >= throw_ms(d)) {
        d->phase = DECIDE_SHOWN;
        d->shown[d->kind] = true;
        ev = DECIDE_EV_LAND; /* the landing is the last change: one cue, not two */
    }
    return ev;
}

void decide_finish(decide_t *d, uint32_t now)
{
    if (d->phase == DECIDE_THROWING) {
        if (d->paused) {
            d->frozen = throw_ms(d);
        } else {
            d->t0 = now - throw_ms(d);
        }
        d->coin_seen = d->coin_half;
        d->rattle_seen = DECIDE_RATTLES;
    }
    d->settled = true;
}

void decide_pause(decide_t *d, uint32_t now)
{
    if (d->paused) return;
    drop_hold(d); /* a hold is a finger or a key: it ended with the takeover */
    d->frozen = now - d->t0;
    d->paused = true;
}

void decide_resume(decide_t *d, uint32_t now)
{
    if (!d->paused) return;
    d->t0 = now - d->frozen;
    d->paused = false;
}

int decide_coin_deg(const decide_t *d, uint32_t now)
{
    if (d->kind != DECIDE_COIN || d->phase != DECIDE_THROWING) return 0;
    uint32_t e = elapsed(d, now);
    if (e > DECIDE_COIN_MS) e = DECIDE_COIN_MS;
    /* ease out: fast at the throw, slowing to rest */
    int64_t q = DECIDE_COIN_MS - (int64_t)e, all = (int64_t)DECIDE_COIN_MS * DECIDE_COIN_MS;
    return (int)((int64_t)d->coin_half * 180 * (all - q * q) / all);
}

int decide_coin_face_at(const decide_t *d, int deg)
{
    if (d->phase != DECIDE_THROWING || d->kind != DECIDE_COIN) return d->coin_face;
    return d->coin_from ^ (((deg + 90) / 180) & 1);
}

int decide_die_shown(const decide_t *d, uint32_t now)
{
    if (d->phase == DECIDE_THROWING && d->kind == DECIDE_DIE) {
        uint32_t e = elapsed(d, now);
        if (e < rattle_at(DECIDE_RATTLES)) return d->rattle[rattles_by(e) - 1];
        return d->die_value;
    }
    return d->shown[DECIDE_DIE] ? d->die_value : 0;
}

int decide_hold_permille(const decide_t *d, uint32_t now)
{
    if (d->phase != DECIDE_HOLDING) return 0;
    uint32_t e = elapsed(d, now);
    return e >= DECIDE_HOLD_MS ? 1000 : (int)(e * 1000u / DECIDE_HOLD_MS);
}

int decide_rise_px(const decide_t *d, uint32_t now)
{
    if (d->kind != DECIDE_ORACLE || d->phase != DECIDE_SHOWN || d->settled) return 0;
    uint32_t e = elapsed(d, now);
    if (e >= DECIDE_RISE_MS) return 0;
    uint32_t q = DECIDE_RISE_MS - e;
    return (int)(DECIDE_RISE_PX * q * q / (DECIDE_RISE_MS * DECIDE_RISE_MS));
}
