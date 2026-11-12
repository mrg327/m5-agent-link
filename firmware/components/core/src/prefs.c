#include "prefs.h"

#include "debug.h"
#include "hal.h"

/* The orientation default is a build choice: automatic needs the sensor's up axis checked on a wrist first. */
#ifndef PREFS_ORIENT_DEFAULT
#define PREFS_ORIENT_DEFAULT ORIENT_NORMAL
#endif

static uint8_t s_vol = PREFS_VOL_DEFAULT;
static orient_mode_t s_orient = PREFS_ORIENT_DEFAULT;

void prefs_init(void)
{
    uint32_t v = hal_pref_get("volume", PREFS_VOL_DEFAULT);
    s_vol = v > PREFS_VOL_MAX ? PREFS_VOL_DEFAULT : (uint8_t)v;
    uint32_t o = hal_pref_get("orient", PREFS_ORIENT_DEFAULT);
    s_orient = o > ORIENT_AUTO ? PREFS_ORIENT_DEFAULT : (orient_mode_t)o;
}

uint8_t prefs_volume(void) { return s_vol; }

uint8_t prefs_volume_percent(void)
{
    static const uint8_t pct[PREFS_VOL_MAX + 1] = {0, 10, 25, 45, 70, 100};
    return pct[s_vol];
}

void prefs_set_volume(uint8_t step)
{
    if (step > PREFS_VOL_MAX) step = PREFS_VOL_MAX;
    if (step == s_vol) return;
    s_vol = step;
    hal_pref_set("volume", step);
    dbg_event("prefs", "volume", "\"step\":%u,\"percent\":%u", (unsigned)step, (unsigned)prefs_volume_percent());
}

orient_mode_t prefs_orient(void) { return s_orient; }

void prefs_set_orient(orient_mode_t m)
{
    if (m > ORIENT_AUTO || m == s_orient) return;
    s_orient = m;
    hal_pref_set("orient", (uint32_t)m);
    dbg_event("prefs", "orient", "\"mode\":%d", (int)m);
}
