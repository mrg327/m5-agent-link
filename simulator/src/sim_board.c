/* Board-level hal.h functions for the simulator: info, time, buttons, motion, haptics, power, debug. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

uint64_t hal_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static bool s_rtc_unknown; /* set to 0: the clock stays unknown, as on a board whose RTC was never set */
uint32_t hal_rtc_get_unix(void) { return s_rtc_unknown ? 0 : (uint32_t)((int64_t)time(NULL) + s_rtc_offset); }
void hal_rtc_set_unix(uint32_t unix_s)
{
    s_rtc_unknown = unix_s == 0;
    s_rtc_offset = (int64_t)unix_s - (int64_t)time(NULL);
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
    hal_motion_evt_t e = {.kind = HAL_MOTION_TILT, .x_mg = (int16_t)s_pose[0], .y_mg = (int16_t)s_pose[1],
                          .z_mg = (int16_t)s_pose[2], .t_us = hal_time_us()};
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
    if (s_button_cb) s_button_cb(btn ? HAL_BTN_B : HAL_BTN_A, down ? HAL_BTN_DOWN : HAL_BTN_UP, hal_time_us());
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

hal_battery_t hal_battery_read(void)
{
    return (hal_battery_t){.percent = 80, .charging = false, .usb_present = !s_on_battery};
}

void hal_debug_line(const char *json)
{
    fputs(json, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

void sim_log(const char *ev, const char *fmt, ...)
{
    char body[200] = "";
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(body, sizeof body, fmt, ap);
        va_end(ap);
    }
    char line[300];
    snprintf(line, sizeof line, "{\"ts_us\":%llu,\"cat\":\"sim\",\"ev\":\"%s\"%s%s}", (unsigned long long)hal_time_us(),
             ev, body[0] ? "," : "", body);
    hal_debug_line(line);
}
