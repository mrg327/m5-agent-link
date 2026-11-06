#include "power.h"

#include "debug.h"
#include "hal.h"
#include "ui.h"

#define GLOW_MS 2000

static bool s_on = true;
static bool s_glowing;
static uint32_t s_idle_ms, s_last_activity, s_glow_until;

static void set_display(bool on)
{
    if (s_on == on) return;
    s_on = on;
    hal_display_set_power(on);
    hal_power_enter(on ? HAL_POWER_ACTIVE : HAL_POWER_STANDBY, 0);
    dbg_event("power", on ? "display_on" : "display_off", NULL);
}

void power_init(void) { s_on = true; }
void power_set_idle_timeout_ms(uint32_t ms) { s_idle_ms = ms; }
bool power_display_on(void) { return s_on; }
const char *power_name(void) { return s_on ? "active" : "standby"; }

bool power_activity(uint32_t now_ms)
{
    s_last_activity = now_ms;
    if (s_glowing) {
        s_glowing = false;
        ui_glow_hide();
    }
    if (s_on) return false;
    set_display(true);
    return true;
}

void power_attention(uint32_t rgb, uint32_t now_ms)
{
    if (s_on) {
        s_last_activity = now_ms;
        return;
    }
    set_display(true);
    ui_glow(rgb);
    s_glowing = true;
    s_glow_until = now_ms + GLOW_MS;
}

void power_tick(uint32_t now_ms)
{
    if (s_glowing && (int32_t)(now_ms - s_glow_until) >= 0) {
        s_glowing = false;
        ui_glow_hide();
        set_display(false);
        return;
    }
    if (s_on && !s_glowing && s_idle_ms && now_ms - s_last_activity >= s_idle_ms) set_display(false);
}
