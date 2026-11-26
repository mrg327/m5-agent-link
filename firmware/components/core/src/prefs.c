#include "prefs.h"

#include "debug.h"
#include "hal.h"
#include "persona.h"

/* The orientation default is a build choice: automatic needs the sensor's up axis checked on a
 * wrist first. */
#ifndef PREFS_ORIENT_DEFAULT
#define PREFS_ORIENT_DEFAULT ORIENT_NORMAL
#endif

static uint8_t s_vol = PREFS_VOL_DEFAULT;
static orient_mode_t s_orient = PREFS_ORIENT_DEFAULT;
static uint32_t s_fun = FUN_DEFAULT;
static uint32_t s_mote_days;

void prefs_init(void)
{
    uint32_t v = hal_pref_get("volume", PREFS_VOL_DEFAULT);
    s_vol = v > PREFS_VOL_MAX ? PREFS_VOL_DEFAULT : (uint8_t)v;
    uint32_t o = hal_pref_get("orient", PREFS_ORIENT_DEFAULT);
    s_orient = o > ORIENT_AUTO ? PREFS_ORIENT_DEFAULT : (orient_mode_t)o;
    s_fun = hal_pref_get("fun", FUN_DEFAULT) & FUN_KNOWN;
    s_mote_days = hal_pref_get("mote_days", 0);
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
    dbg_event("prefs", "volume", "\"step\":%u,\"percent\":%u", (unsigned)step,
              (unsigned)prefs_volume_percent());
}

orient_mode_t prefs_orient(void) { return s_orient; }

void prefs_set_orient(orient_mode_t m)
{
    if (m > ORIENT_AUTO || m == s_orient) return;
    s_orient = m;
    hal_pref_set("orient", (uint32_t)m);
    dbg_event("prefs", "orient", "\"mode\":%d", (int)m);
}

uint32_t prefs_fun(void) { return s_fun; }

void prefs_set_fun(uint32_t word)
{
    word &= FUN_KNOWN;
    if (word == s_fun) return;
    s_fun = word;
    hal_pref_set("fun", word);
    dbg_event("prefs", "fun", "\"word\":%u", (unsigned)word);
}

uint32_t prefs_mote_days(void) { return s_mote_days; }

void prefs_set_mote_days(uint32_t word)
{
    if (word == s_mote_days) return;
    s_mote_days = word;
    hal_pref_set("mote_days", word);
}
