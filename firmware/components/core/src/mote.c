#include "mote.h"

#include <math.h>

#define FULL 360000    /* millidegrees in a turn */
#define CHEER_MDPS 600 /* millidegrees a millisecond: 18° a 30 ms tick, three laps in 1.8 s */

static int32_t wrap(int32_t a)
{
    a %= FULL;
    return a < 0 ? a + FULL : a;
}

/* The short way from a to b, in (-180°, 180°]. */
static int32_t short_way(int32_t a, int32_t b)
{
    int32_t d = wrap(b - a);
    return d > FULL / 2 ? d - FULL : d;
}

/* Moves pos toward target by at most step; true once it is there. */
static bool approach(int32_t *pos, int32_t target, int32_t step)
{
    int32_t d = short_way(*pos, target);
    if (d <= step && d >= -step) {
        *pos = wrap(target);
        return true;
    }
    *pos = wrap(*pos + (d > 0 ? step : -step));
    return false;
}

/* 0..levels-1 and back over one period. */
static int tri(uint32_t t, uint32_t period, int levels)
{
    uint32_t p = t % period * 2;
    if (p > period) p = 2 * period - p;
    int k = (int)(p * (uint32_t)levels / period);
    return k >= levels ? levels - 1 : k;
}

static uint8_t tail_of(const mote_in_t *in)
{
    int lvl = in->level > MOTE_LEVEL_MAX ? MOTE_LEVEL_MAX : in->level;
    int t = MOTE_TAIL_DEG + MOTE_TAIL_STEP * lvl;
    if (in->usage != 255 && in->usage >= MOTE_TIRED_PCT) {
        t /= 2;
        if (t < MOTE_TAIL_MIN) t = MOTE_TAIL_MIN;
    }
    return (uint8_t)t;
}

static uint16_t whole_deg(int32_t mdeg) { return (uint16_t)(wrap(mdeg + 500) / 1000 % 360); }

/* The cheer's laps and its one large frame, or the loop off the track; false once it is over. */
static bool effect(mote_t *m, uint32_t now, mote_out_t *o)
{
    uint32_t t = now - m->fx_ms;
    if (m->fx == MOTE_CHEER && t < MOTE_CHEER_MS + MOTE_CHEER_HOLD_MS) {
        o->mode = MOTE_CHEER;
        if (t < MOTE_CHEER_MS) {
            o->deg = whole_deg(m->pos + (int32_t)(t * CHEER_MDPS));
            o->tail_deg = MOTE_CHEER_TAIL;
        } else {
            o->deg = whole_deg(m->pos);
            o->size = MOTE_CHEER_PX;
        }
        return true;
    }
    if (m->fx == MOTE_LOOP && t < MOTE_LOOP_MS) {
        o->mode = MOTE_LOOP;
        o->deg = whole_deg(m->pos);
        o->tail_deg = 0;
        float a = (float)o->deg * 0.017453292f, ph = (float)t / (float)MOTE_LOOP_MS * 6.2831853f;
        float in = 1.0f - cosf(ph), along = sinf(ph);
        o->dx = (int8_t)lroundf((float)MOTE_LOOP_R * (along * cosf(a) - in * sinf(a)));
        o->dy = (int8_t)lroundf((float)MOTE_LOOP_R * (along * sinf(a) + in * cosf(a)));
        return true;
    }
    if (m->fx != MOTE_GONE) { /* over: catch up from where it left off */
        m->fx = MOTE_GONE;
        m->locked = false;
        m->since = now;
    }
    return false;
}

/* Where the light goes in its base mode, and how bright it is there. */
static void move(mote_t *m, const mote_in_t *in, uint32_t now, uint32_t dt, mote_out_t *o)
{
    switch (m->base) {
    case MOTE_HOVER: {
        int32_t target = wrap((int32_t)in->error_deg * 1000);
        int32_t at = target;
        if (!m->locked && approach(&m->pos, target, MOTE_CATCH_DPS * (int32_t)dt)) {
            m->locked = true;
            m->since = now; /* the sway starts from the error, not mid-swing */
        }
        if (!m->locked) {
            at = m->pos;
        } else if (!in->reduced) {
            float s = sinf((float)((now - m->since) % 1000u) * 0.0062831853f);
            at = target + (int32_t)lroundf(s * (float)(MOTE_HOVER_SWAY * 1000));
            if ((now - m->since) / MOTE_FLICKER_MS % 2) o->lum = (uint8_t)(o->lum * 60 / 100);
        }
        o->deg = whole_deg(at);
        return;
    }
    case MOTE_LAP: {
        int32_t target = in->reduced ? 0 : (int32_t)(in->minute_ms % 60000u) * 6;
        if (m->locked && short_way(m->pos, target) <= 2000 && short_way(m->pos, target) >= -2000) {
            m->pos = target;
        } else {
            if (m->locked) { /* the clock was set: catch up rather than jump */
                m->locked = false;
                m->since = now;
            }
            uint32_t ramp = now - m->since > 1000 ? 1000 : now - m->since;
            int32_t dps = MOTE_CALM_DPS + (MOTE_CATCH_DPS - MOTE_CALM_DPS) * (int32_t)ramp / 1000;
            m->locked = approach(&m->pos, target, dps * (int32_t)dt);
        }
        o->deg = whole_deg(m->pos);
        return;
    }
    default: /* SETTLE, EMBER */
        if (!m->locked) {
            m->locked = approach(&m->pos, MOTE_SETTLE_DEG * 1000, MOTE_CALM_DPS * (int32_t)dt);
        }
        o->deg = whole_deg(m->pos);
        if (now - m->idle_ms >= MOTE_EMBER_MS) {
            o->mode = MOTE_EMBER;
            o->lum = MOTE_LUM_EMBER;
            o->size = MOTE_EMBER_PX;
            o->tail_deg = 0;
        } else if (m->locked && !in->reduced) {
            uint32_t period = o->amber ? MOTE_PULSE_NIGHT_MS : MOTE_PULSE_MS;
            o->lum = (uint8_t)(o->lum * (70 + 10 * tri(now - m->idle_ms, period, 4)) / 100);
        }
        return;
    }
}

mote_out_t mote_step(mote_t *m, const mote_in_t *in, uint32_t now_ms)
{
    mote_out_t o = {0};
    uint32_t dt = m->shown ? now_ms - m->last_ms : 0;
    if (dt > 1000) dt = 1000;
    m->last_ms = now_ms;
    if (!m->synced || !in->face || !in->may) {
        /* gone: no exit animation (the request is the picture), and nothing saved for later */
        m->synced = true;
        m->cheers = in->cheers;
        m->loops = in->loops;
        m->fx = MOTE_GONE;
        if (!in->face || !in->may) {
            m->shown = false;
            m->mode = MOTE_GONE;
            return o;
        }
    }
    mote_mode_t base = in->errors ? MOTE_HOVER : in->working ? MOTE_LAP : MOTE_SETTLE;
    bool appear = !m->shown;
    if (appear || base != m->base) {
        m->base = base;
        m->since = now_ms;
        m->locked = false;
        if (base == MOTE_SETTLE) m->idle_ms = now_ms;
    }
    if (in->cheers != m->cheers) {
        m->cheers = in->cheers;
        if (!in->reduced && !appear && base != MOTE_HOVER) {
            m->fx = MOTE_CHEER;
            m->fx_ms = now_ms;
        }
    }
    if (in->loops != m->loops) {
        m->loops = in->loops;
        if (!in->reduced) {
            m->fx = MOTE_LOOP;
            m->fx_ms = now_ms;
        }
    }
    m->shown = true;

    o.amber = in->hour != 255 && (in->hour >= 23 || in->hour < 6);
    o.lum = o.amber ? MOTE_LUM_NIGHT : MOTE_LUM;
    o.size = MOTE_HEAD_PX;
    o.tail_deg = tail_of(in);
    o.mode = base;
    if (appear || in->reduced) { /* it arrives with the face, at its place: nothing to catch up */
        m->pos = base == MOTE_HOVER ? wrap((int32_t)in->error_deg * 1000)
                 : base == MOTE_LAP ? (in->reduced ? 0 : (int32_t)(in->minute_ms % 60000u) * 6)
                                    : MOTE_SETTLE_DEG * 1000;
        m->locked = true;
    }
    if (!effect(m, now_ms, &o)) move(m, in, now_ms, dt, &o);
    m->mode = o.mode;
    return o;
}

uint8_t mote_level(uint32_t days)
{
    static const uint16_t s_at[MOTE_LEVEL_MAX] = {1, 3, 7, 14, 30, 60, 120};
    uint8_t l = 0;
    while (l < MOTE_LEVEL_MAX && days >= s_at[l]) l++;
    return l;
}

uint32_t mote_days(uint32_t word) { return word & 0xFFFFu; }

uint32_t mote_note_day(uint32_t word, uint32_t day)
{
    uint32_t count = word & 0xFFFFu, last = word >> 16;
    if (day == 0 || day > 0xFFFFu || day <= last || count == 0xFFFFu) return word;
    return day << 16 | (count + 1);
}

bool mote_finish_green(const mote_finish_t *f) { return !f->error && f->tests == 1; }

bool mote_finish_big(const mote_finish_t *f)
{
    if (f->error || f->tests == 0) return false;
    return f->secs >= 600 || f->files >= 5 || f->lines >= 200;
}
