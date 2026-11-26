#include "mote.h"

#include <math.h>
#include <stdlib.h>

#define FULL 360000 /* millidegrees in a turn */

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

static uint16_t whole_deg(int32_t mdeg) { return (uint16_t)(wrap(mdeg + 500) / 1000 % 360); }

static int ang(int a, int b) { return abs((int)short_way(a * 1000, b * 1000)) / 1000; }

/* Eased in and out: 0..1 to 0..1, at most 1.5 times the mean speed. */
static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

static float frac(uint32_t t, uint32_t len) { return t >= len ? 1.0f : (float)t / (float)len; }

uint16_t mote_breath(uint32_t t_ms, uint32_t period_ms)
{
    if (!period_ms) return 0;
    float ph = (float)(t_ms % period_ms) / (float)period_ms;
    float b = 0.5f - 0.5f * cosf(ph * 6.2831853f);
    long k = lroundf(b * (float)(MOTE_BREATH_STEPS - 1));
    return (uint16_t)(k * 1000 / (MOTE_BREATH_STEPS - 1));
}

mote_look_t mote_look(uint16_t scale_pct, uint8_t lum, uint16_t breath_pm)
{
    if (breath_pm > 1000) breath_pm = 1000;
    int32_t core = MOTE_CORE_R * (int32_t)scale_pct / 100;
    int32_t halo_m = MOTE_HALO_R * 1000 + MOTE_BREATH_PX * breath_pm;
    int32_t halo = (halo_m * (int32_t)scale_pct / 100 + 500) / 1000;
    int32_t pct =
        MOTE_HALO_PCT * (MOTE_BREATH_LO * 1000 + (100 - MOTE_BREATH_LO) * breath_pm) / 100000;
    if (core < 1) core = 1;
    if (core > 120) core = 120;
    if (halo <= core) halo = core + 1;
    if (halo > 255) halo = 255;
    return (mote_look_t){(uint8_t)core, (uint8_t)halo, lum > 100 ? 100 : lum,
                         (uint8_t)(pct > 100 ? 100 : pct)};
}

/* Where it goes among the working sessions: the nearest at first (or when the one it is by stops or
 * moves), then after MOTE_VISIT_MS by each, the next one clockwise (ring order). */
static int visit(const mote_t *m, const mote_in_t *in, uint32_t now)
{
    int n = in->n_work > MOTE_VISIT_MAX ? MOTE_VISIT_MAX : in->n_work;
    if (in->reduced) return in->work_deg[0]; /* still, by the first */
    int cur = m->target, at = -1;
    for (int i = 0; i < n; i++) {
        if (in->work_deg[i] == cur) at = i;
    }
    if (at < 0) {
        int from = cur < 0 ? whole_deg(m->pos) : cur, best = 0;
        for (int i = 1; i < n; i++) {
            if (ang(from, in->work_deg[i]) < ang(from, in->work_deg[best])) best = i;
        }
        return in->work_deg[best];
    }
    if (n < 2 || !m->arrived || now - m->arrive_ms < MOTE_VISIT_MS) return cur;
    int best = -1;
    int32_t bd = FULL;
    for (int i = 0; i < n; i++) {
        int32_t d = wrap((in->work_deg[i] - cur) * 1000);
        if (d > 0 && d < bd) {
            bd = d;
            best = i;
        }
    }
    return best < 0 ? cur : in->work_deg[best];
}

/* Heads for deg: a new target starts an eased trip from where it is, the short way. */
static void go(mote_t *m, int deg, uint32_t now, bool jump)
{
    int32_t to = wrap(deg * 1000);
    if (jump || deg != m->target) {
        bool same = deg == m->target;
        m->target = (int16_t)deg;
        m->from = m->pos;
        m->to = to;
        m->t0 = now;
        int32_t d = jump ? 0 : short_way(m->pos, to);
        m->dur = MOTE_TRAVEL_MS + (uint32_t)(abs((int)d) / 1000) * MOTE_TRAVEL_MS_DEG;
        m->travelling = d != 0;
        if (!m->travelling) {
            m->pos = to;
            if (!same || !m->arrived) m->arrive_ms = now;
            m->arrived = true;
        } else {
            m->arrived = false;
        }
    }
    if (!m->travelling) return;
    uint32_t t = now - m->t0;
    if (t >= m->dur) {
        m->pos = m->to;
        m->travelling = false;
        m->arrived = true;
        m->arrive_ms = now;
        return;
    }
    float k = smooth(frac(t, m->dur));
    m->pos = wrap(m->from + (int32_t)lroundf((float)short_way(m->from, m->to) * k));
}

static uint8_t base_lum(const mote_out_t *o) { return o->night ? MOTE_LUM_NIGHT : MOTE_LUM; }

/* The halo's brightness for the night and a tired plan. */
static void dim_halo(const mote_in_t *in, mote_out_t *o)
{
    if (o->night) o->look.halo_pct = (uint8_t)(o->look.halo_pct * MOTE_NIGHT_HALO / 100);
    if (in->usage != 255 && in->usage >= MOTE_TIRED_PCT) {
        o->look.halo_pct = (uint8_t)(o->look.halo_pct * MOTE_TIRED_HALO / 100);
    }
}

/* The cheer's loop and flare, or the loop off the track; false once it is over. */
static bool effect(mote_t *m, const mote_in_t *in, uint32_t now, mote_out_t *o)
{
    uint32_t t = now - m->fx_ms;
    uint8_t lum = base_lum(o);
    if (m->fx == MOTE_CHEER && t < MOTE_CHEER_MS + MOTE_FLARE_MS) {
        o->mode = MOTE_CHEER;
        if (t < MOTE_CHEER_MS) {
            o->deg = whole_deg(m->pos + (int32_t)lroundf(smooth(frac(t, MOTE_CHEER_MS)) * FULL));
            o->look = mote_look(100, lum, 1000);
        } else { /* the flare: bright and wide, falling back over MOTE_FLARE_MS */
            uint32_t f = MOTE_FLARE_MS - (t - MOTE_CHEER_MS);
            o->deg = whole_deg(m->pos);
            o->look =
                mote_look(100, (uint8_t)(lum + (MOTE_LUM_FLARE - lum) * f / MOTE_FLARE_MS), 1000);
            o->look.halo_r = (uint8_t)(o->look.halo_r + MOTE_FLARE_PX * f / MOTE_FLARE_MS);
            o->look.halo_pct = 100;
        }
        return true;
    }
    if (m->fx == MOTE_LOOP && t < MOTE_LOOP_MS) {
        o->mode = MOTE_LOOP;
        o->deg = whole_deg(m->pos);
        o->look = mote_look(100, lum, 1000);
        dim_halo(in, o);
        float a = (float)o->deg * 0.017453292f, ph = (float)t / (float)MOTE_LOOP_MS * 6.2831853f;
        float inw = 1.0f - cosf(ph), along = sinf(ph);
        o->dx = (int8_t)lroundf((float)MOTE_LOOP_R * (along * cosf(a) - inw * sinf(a)));
        o->dy = (int8_t)lroundf((float)MOTE_LOOP_R * (along * sinf(a) + inw * cosf(a)));
        return true;
    }
    m->fx = MOTE_GONE;
    return false;
}

/* Its look in its base mode, and the sway beside an error. */
static void settle_look(const mote_t *m, const mote_in_t *in, uint32_t now, mote_out_t *o)
{
    uint8_t lum = base_lum(o);
    int32_t at = m->pos;
    uint16_t breath = 0;
    if (m->base == MOTE_HOVER) {
        if (m->arrived && !in->reduced) {
            uint32_t t = now - m->arrive_ms; /* the sway starts from the error, not mid-swing */
            float s = sinf((float)(t % 1000u) * 0.0062831853f);
            at += (int32_t)lroundf(s * (float)(MOTE_HOVER_SWAY * 1000));
            if (t / MOTE_FLICKER_MS % 2) lum = (uint8_t)(lum * 60 / 100);
        }
    } else if (m->base == MOTE_REST && now - m->idle_ms >= MOTE_EMBER_MS) {
        o->mode = MOTE_EMBER;
        o->look = mote_look(MOTE_EMBER_SCALE, MOTE_LUM_EMBER, 0);
        o->look.halo_pct /= 2;
        o->deg = whole_deg(at);
        return;
    } else if (!in->reduced) {
        bool tired = in->usage != 255 && in->usage >= MOTE_TIRED_PCT;
        breath = mote_breath(now, tired ? MOTE_BREATH_TIRED_MS : MOTE_BREATH_MS);
    }
    o->look = mote_look(100, lum, breath);
    dim_halo(in, o);
    o->deg = whole_deg(at);
}

static int target_of(const mote_t *m, const mote_in_t *in, uint32_t now)
{
    if (m->base == MOTE_HOVER) return ((int)in->error_deg % 360 + 360) % 360;
    if (m->base == MOTE_VISIT) return visit(m, in, now);
    return MOTE_REST_DEG;
}

mote_out_t mote_step(mote_t *m, const mote_in_t *in, uint32_t now_ms)
{
    mote_out_t o = {0};
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
    mote_mode_t base = in->errors ? MOTE_HOVER : in->n_work ? MOTE_VISIT : MOTE_REST;
    bool appear = !m->shown;
    if (appear || base != m->base) {
        m->base = base;
        m->target = -1;
        if (base == MOTE_REST) m->idle_ms = now_ms;
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
    /* it arrives with the face, at its place; with reduced motion it never travels */
    go(m, target_of(m, in, now_ms), now_ms, appear || in->reduced);

    o.night = in->hour != 255 && (in->hour >= 23 || in->hour < 6);
    o.rgb = MOTE_RGB;
    o.mode = base;
    if (!effect(m, in, now_ms, &o)) settle_look(m, in, now_ms, &o);
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
