/* Board-level hal.h functions for the simulator: info, time, buttons, motion, haptics, power,
 * debug. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sim.h"

static const hal_board_info_t s_info = {
    .board = "sim",
    .fw_version = "0.1.0-sim",
    .uid = {1, 2, 3, 4, 5, 6, 7, 8},
    .has_display = true,
    .shape = HAL_SHAPE_ROUND,
    .width = 466,
    .height = 466,
    .color = true,
    .has_touch = true,
    .has_imu = true,
    .has_haptic = true,
    .has_speaker = true,
    .has_mic = true,
    .has_battery = true,
    .num_buttons = 2,
};

static hal_button_cb_t s_button_cb;
static hal_motion_cb_t s_motion_cb;
static int64_t s_rtc_offset;

void hal_board_init(void) {}
const hal_board_info_t *hal_board_info(void) { return &s_info; }

static uint64_t s_skip_us; /* `skip`: the clock jumps, to test long timeouts */

/* A scripted headless run uses a virtual clock (sim_clock_virtual): it moves only when the main
 * loop sleeps, so a starved process cannot stretch a press into a hold (issue 6eb4). */
static bool s_virtual;
static uint64_t s_virtual_us;

/* The clock reads as if the process started an hour after boot, so its phase does not depend on
 * how long the host has been up. A periodic animation keyed to the clock (a breath, a pulse) then
 * starts at the same phase in every run, and so do the frames a script's redraw budget counts:
 * with the host's uptime as the origin, sim_fun_temper's 6.5 s window on the pulsing ring varied
 * from 3.76 to 4.15 M px between runs (BENCH.md §9). */
#define SIM_CLOCK_BASE_US 3600000000ull

static uint64_t real_us(void)
{
    static uint64_t s_start;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
    if (!s_start) s_start = now;
    return SIM_CLOCK_BASE_US + (now - s_start);
}

uint64_t hal_time_us(void) { return (s_virtual ? s_virtual_us : real_us()) + s_skip_us; }

void sim_clock_virtual(void)
{
    s_virtual_us = real_us();
    s_virtual = true;
}

void sim_sleep_us(uint64_t us)
{
    if (s_virtual) {
        s_virtual_us += us;
    } else if (us) {
        usleep((useconds_t)us);
    }
}

void sim_skip_time(uint32_t ms) { s_skip_us += (uint64_t)ms * 1000u; }

/* A scripted run starts its RTC here, not at the host's wall clock, so a test reads the same time
 * of day whenever it runs (the Mote's night look and the eggs depend on it); a script sets another
 * with `rtc`. Unix 1800000000 is 08:00 UTC, a weekday morning. */
#define SIM_SCRIPT_EPOCH 1800000000

/* The RTC runs on hal_time_us() (the virtual clock in a headless script, `skip` included), from the
 * wall clock at the first read (the fixed epoch in a scripted run): a script's seconds pass as its
 * milliseconds do. */
static int64_t rtc_now(void)
{
    static bool init;
    static int64_t base;
    int64_t up = (int64_t)(hal_time_us() / 1000000u);
    if (!init) {
        init = true;
        base = (s_virtual ? (int64_t)SIM_SCRIPT_EPOCH : (int64_t)time(NULL)) - up;
    }
    return base + up;
}

/* set to 0: the clock stays unknown, as on a board whose RTC was never set */
static bool s_rtc_unknown;
uint32_t hal_rtc_get_unix(void) { return s_rtc_unknown ? 0 : (uint32_t)(rtc_now() + s_rtc_offset); }
void hal_rtc_set_unix(uint32_t unix_s)
{
    s_rtc_unknown = unix_s == 0;
    s_rtc_offset = (int64_t)unix_s - rtc_now();
}

void hal_buttons_set_callback(hal_button_cb_t cb) { s_button_cb = cb; }
void hal_motion_set_callback(hal_motion_cb_t cb) { s_motion_cb = cb; }
static bool s_tilt_stream;
static int s_pose[3] = {0, 0, 1000}; /* lying face up, still */
static uint32_t s_pose_next_ms;
static bool s_cover_on;
void hal_motion_stream_tilt(bool enable) { s_tilt_stream = enable; }
bool hal_touch_covered(void) { return s_cover_on; }
void sim_set_cover(bool on) { s_cover_on = on; }
void sim_set_pose(int x, int y, int z)
{
    s_pose[0] = x;
    s_pose[1] = y;
    s_pose[2] = z;
}

/* The IMU task of the watch: one gravity sample every 100 ms while streaming. */
void sim_motion_poll(uint32_t now_ms)
{
    if (!s_tilt_stream || !s_motion_cb || (int32_t)(now_ms - s_pose_next_ms) < 0) return;
    s_pose_next_ms = now_ms + 100;
    hal_motion_evt_t e = {.kind = HAL_MOTION_TILT,
                          .x_mg = (int16_t)s_pose[0],
                          .y_mg = (int16_t)s_pose[1],
                          .z_mg = (int16_t)s_pose[2],
                          .t_us = hal_time_us()};
    s_motion_cb(&e);
}

static uint32_t s_prefs[8][2]; /* key hash, value; RAM only */
static const char *s_pref_keys[8];
uint32_t hal_pref_get(const char *key, uint32_t fallback)
{
    for (int i = 0; i < 8; i++)
        if (s_pref_keys[i] && !strcmp(s_pref_keys[i], key)) return s_prefs[i][1];
    return fallback;
}

void hal_pref_set(const char *key, uint32_t value)
{
    int free_i = -1;
    for (int i = 0; i < 8; i++) {
        if (s_pref_keys[i] && !strcmp(s_pref_keys[i], key)) return (void)(s_prefs[i][1] = value);
        if (!s_pref_keys[i] && free_i < 0) free_i = i;
    }
    if (free_i >= 0) {
        s_pref_keys[free_i] = key; /* callers pass string literals */
        s_prefs[free_i][1] = value;
    }
}

void sim_input_button(int btn, bool down)
{
    if (s_button_cb)
        s_button_cb(btn ? HAL_BTN_B : HAL_BTN_A, down ? HAL_BTN_DOWN : HAL_BTN_UP, hal_time_us());
}

void sim_input_motion(hal_motion_kind_t kind)
{
    hal_motion_evt_t e = {.kind = kind, .z_mg = 1000, .t_us = hal_time_us()};
    if (s_motion_cb) s_motion_cb(&e);
}

void hal_haptic_play(const uint16_t *on_off_ms, size_t count)
{
    char list[96] = "";
    size_t n = 0;
    for (size_t i = 0; i < count && n < sizeof list - 8; i++)
        n += (size_t)snprintf(list + n, sizeof list - n, "%s%u", i ? "," : "", on_off_ms[i]);
    sim_log("haptic", "\"pattern_ms\":[%s]", list);
}

void hal_audio_play(const int16_t *pcm, size_t n)
{
    if (!pcm || !n) sim_log("sound_stop", NULL);
    else sim_log("sound", "\"samples\":%u", (unsigned)n);
}
static uint8_t s_volume = 100;
void hal_audio_set_volume(uint8_t percent)
{
    if (percent == s_volume) return;
    s_volume = percent;
    sim_log("volume", "\"percent\":%u", (unsigned)percent);
}
void *hal_audio_alloc(size_t bytes) { return malloc(bytes); }

hal_power_state_t hal_power_enter(hal_power_state_t state, uint32_t wake_after_s)
{
    sim_log("power_enter", "\"state\":%d,\"wake_after_s\":%u", (int)state, (unsigned)wake_after_s);
    return state;
}

static bool s_on_battery;
static hal_wake_t s_wake = HAL_WAKE_POWER_ON;
static uint32_t s_retained;
void sim_set_battery(bool on_battery) { s_on_battery = on_battery; }
void sim_set_wake(hal_wake_t why, uint32_t retained) { s_wake = why, s_retained = retained; }
hal_wake_t hal_wake_reason(void) { return s_wake; }
uint32_t hal_retained_get(void) { return s_retained; }
const char *hal_reset_reason(void) { return "poweron"; }
void hal_retained_set(uint32_t v) { s_retained = v; }

static int s_batt_pct = 80, s_batt_chg = -1; /* -1: the default, plugged in unless on_battery */
void sim_set_battery_pct(unsigned pct, bool charging)
{
    s_batt_pct = (int)pct;
    s_batt_chg = charging;
}

hal_battery_t hal_battery_read(void)
{
    if (s_batt_chg >= 0)
        return (hal_battery_t){
            .percent = (uint8_t)s_batt_pct, .charging = s_batt_chg, .usb_present = s_batt_chg};
    return (hal_battery_t){
        .percent = (uint8_t)s_batt_pct, .charging = false, .usb_present = !s_on_battery};
}

/* The power report's voltages, derived from the scripted percent (the device's own linear 3.3-4.2 V
 * map). */
hal_battery_mv_t hal_battery_mv(void)
{
    hal_battery_t b = hal_battery_read();
    uint16_t mv = (uint16_t)(3300 + 9 * (b.percent > 100 ? 100 : b.percent));
    return (hal_battery_mv_t){.mv = mv, .mv_filt = mv, .vin_mv = b.usb_present ? 5000 : 0};
}

static uint32_t s_slept_ms;
void sim_set_deep_slept_ms(uint32_t ms) { s_slept_ms = ms; }
uint32_t hal_deep_slept_ms(void) { return s_wake == HAL_WAKE_POWER_ON ? 0 : s_slept_ms; }
static uint32_t s_retained_words[HAL_RETAINED_WORDS];
uint32_t *hal_retained_words(void) { return s_retained_words; }

void hal_debug_line(const char *json)
{
    fputs(json, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

void sim_log(const char *ev, const char *fmt, ...)
{
    char body[320] = ""; /* the frames report is about 250 */
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(body, sizeof body, fmt, ap);
        va_end(ap);
    }
    char line[420];
    snprintf(line, sizeof line, "{\"ts_us\":%llu,\"cat\":\"sim\",\"ev\":\"%s\"%s%s}",
             (unsigned long long)hal_time_us(), ev, body[0] ? "," : "", body);
    hal_debug_line(line);
}
