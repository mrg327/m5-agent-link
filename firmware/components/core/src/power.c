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
/* After a timer wake the display stays off; this long to reconnect, then DEEP again. */
#define RECONNECT_WINDOW_MS (90u * 1000u)
/* Timer wakes to look for the host (FIRMWARE.md "DEEP"): 5 → 60 min, ~4 h in all, then buttons only.
 * Off by default: a woken watch can only reach the host over BLE, and the Linux host has no working
 * BLE (USB only), so a timer wake just burns battery. Build with POWER_TIMER_WAKES=1 to turn back on. */
#ifndef POWER_TIMER_WAKES
#define POWER_TIMER_WAKES 0
#endif
static const uint16_t kRetryMin[] = {5, 10, 20, 40, 60, 60, 60};
#define N_RETRY (sizeof kRetryMin / sizeof kRetryMin[0])

static bool s_on = true;
static bool s_glowing, s_deep, s_timer_wake;
static uint32_t s_deep_after_ms = DEEP_AFTER_MS, s_tries;
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
    s_timer_wake = hal_wake_reason() == HAL_WAKE_TIMER;
    s_tries = s_timer_wake ? hal_retained_get() : 0;
    hal_retained_set(s_tries);
}

/* After the display exists: a timer wake is only a reconnect attempt, so nothing lights up. */
void power_start(uint32_t now_ms)
{
    dbg_event("power", "wake", "\"timer\":%s,\"tries\":%u", s_timer_wake ? "true" : "false", (unsigned)s_tries);
    s_last_activity = now_ms; /* idle and DEEP timers count from boot, not from clock zero */
    s_off_since = now_ms;
    if (!s_timer_wake) return;
    set_display(false);
    s_off_since = now_ms;
}

bool power_woke_by_timer(void) { return s_timer_wake; }
void power_set_deep_after_ms(uint32_t ms) { s_deep_after_ms = ms; }

/* The user or the host is back: the next DEEP starts the retry ladder from the top. */
static void reset_retries(void)
{
    s_timer_wake = false;
    s_tries = 0;
}
void power_set_idle_timeout_ms(uint32_t ms) { s_idle_ms = ms; }
bool power_display_on(void) { return s_on; }
const char *power_name(void) { return s_on ? "active" : s_deep ? "deep" : "standby"; }

bool power_activity(uint32_t now_ms)
{
    s_last_activity = now_ms;
    reset_retries();
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
    if (link_connected()) reset_retries();
    uint32_t deep_after = s_timer_wake && RECONNECT_WINDOW_MS < s_deep_after_ms ? RECONNECT_WINDOW_MS : s_deep_after_ms;
    if (!s_on && !s_glowing && !s_deep && now_ms - s_off_since >= deep_after && may_deep()) {
        uint32_t wake_after_s = POWER_TIMER_WAKES && s_tries < N_RETRY ? kRetryMin[s_tries] * 60u : 0;
        hal_retained_set(s_tries + 1);
        s_deep = true;
        dbg_event("power", "deep", "\"wake_after_s\":%u,\"tries\":%u", (unsigned)wake_after_s, (unsigned)s_tries);
        hal_power_enter(HAL_POWER_DEEP, wake_after_s); /* on the device this does not return */
    }
    if (s_on && !s_glowing && s_idle_ms && now_ms - s_last_activity >= s_idle_ms) {
        set_display(false);
        s_off_since = now_ms; /* DEEP counts from display-off, not from the last input */
    }
}
