#include "power.h"

#include "debug.h"
#include "hal.h"
#include "link.h"
#include "ui.h"

#define GLOW_MS 2000
/* Display-off timeout with no input. 0 would mean "never", so the device default must be set here:
 * the AMOLED and the battery both suffer from a panel left lit (the ring dims to ambient at 30 s).
 * The simulator overrides it (`--idle-off`, default never) so scripts see a stable screen. */
#define IDLE_OFF_DEFAULT_MS (60u * 1000u)
/* Display-off time after which an unattended, battery-powered, unlinked device goes DEEP. */
#define DEEP_AFTER_MS (10u * 60u * 1000u)

static bool s_on = true;
static bool s_glowing, s_deep;
static uint32_t s_idle_ms, s_last_activity, s_glow_until, s_off_since;

static void set_display(bool on)
{
    if (s_on == on) return;
    s_on = on;
    s_deep = false;
    s_off_since = s_last_activity;
    hal_display_set_power(on);
    hal_power_enter(on ? HAL_POWER_ACTIVE : HAL_POWER_STANDBY, 0);
    dbg_event("power", on ? "display_on" : "display_off", NULL);
}

/* DEEP only saves battery: never while a host/charger supplies power or any link is up, so
 * USB Serial/JTAG (flashing, debug, daemon) stays reachable. The board repeats the USB guard. */
static bool may_deep(void)
{
    hal_battery_t b = hal_battery_read();
    return !b.usb_present && !link_connected();
}

void power_init(void)
{
    s_on = true;
    s_idle_ms = IDLE_OFF_DEFAULT_MS;
}
void power_set_idle_timeout_ms(uint32_t ms) { s_idle_ms = ms; }
bool power_display_on(void) { return s_on; }
const char *power_name(void) { return s_on ? "active" : s_deep ? "deep" : "standby"; }

bool power_activity(uint32_t now_ms)
{
    s_last_activity = now_ms;
    if (s_glowing) {
        /* The display is already on for the alert glow, but nothing has been read yet: this touch
         * only reveals the card, so a thumb grabbing the watch cannot start an approval. */
        s_glowing = false;
        ui_glow_hide();
        return true;
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
        s_off_since = now_ms;
        return;
    }
    if (!s_on && !s_glowing && !s_deep && now_ms - s_off_since >= DEEP_AFTER_MS && may_deep()) {
        s_deep = true;
        hal_power_enter(HAL_POWER_DEEP, 0);
        dbg_event("power", "deep", NULL);
    }
    if (s_on && !s_glowing && s_idle_ms && now_ms - s_last_activity >= s_idle_ms) set_display(false);
}
