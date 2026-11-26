#include "eggs.h"

#include <math.h>

#define DAY 86400u

bool eggs_slip_time(uint32_t local_unix)
{
    uint32_t hm = local_unix % DAY / 60;
    return hm == 13 * 60 + 37 || hm == 4 * 60 + 4;
}

bool eggs_slip_due(eggs_t *e, uint32_t local_unix, bool may, uint32_t now_ms)
{
    if (!may || local_unix < DAY || !eggs_slip_time(local_unix)) return false;
    uint32_t minute = local_unix / 60;
    if (minute == e->slip_minute) return false; /* once per such minute */
    e->slip_minute = minute;
    e->slip_ms = now_ms;
    e->slipping = true;
    return true;
}

int eggs_slip_dx(eggs_t *e, uint32_t now_ms)
{
    if (!e->slipping) return 0;
    uint32_t t = now_ms - e->slip_ms;
    if (t >= EGGS_SLIP_MS) {
        e->slipping = false;
        return 0;
    }
    return t / EGGS_SLIP_FRAME_MS % 2 ? 0 : EGGS_SLIP_PX;
}

uint32_t eggs_count(uint32_t n) { return n == UINT32_MAX ? n : n + 1; }

bool eggs_is_century(uint32_t n) { return n == 100 || n == 1000; }

bool eggs_save(uint32_t n) { return n % EGGS_SAVE_EVERY == 0 || eggs_is_century(n); }

void eggs_finished(eggs_t *e, uint32_t n, uint32_t now_ms)
{
    if (!eggs_is_century(n)) return;
    e->century = true;
    e->century_ms = now_ms;
}

bool eggs_century_due(eggs_t *e, bool may, uint32_t now_ms)
{
    if (!e->century) return false;
    if (now_ms - e->century_ms > EGGS_CENTURY_WAIT_MS) {
        e->century = false; /* lapsed: nothing is saved up for later */
        return false;
    }
    if (!may) return false;
    e->century = false;
    return true;
}

int eggs_spin_deg(uint32_t t_ms)
{
    if (t_ms >= EGGS_SPIN_MS) return 0;
    float t = (float)t_ms / (float)EGGS_SPIN_MS, u = -2.0f * t + 2.0f;
    float e = t < 0.5f ? 4.0f * t * t * t : 1.0f - u * u * u / 2.0f; /* inout */
    return (int)lroundf(360.0f * e);
}
