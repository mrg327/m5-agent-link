/* Board-level hal.h functions for the simulator: info, time, buttons, motion, haptics, power, debug. */
#include <stdarg.h>
#include <stdio.h>
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

uint32_t hal_rtc_get_unix(void) { return (uint32_t)((int64_t)time(NULL) + s_rtc_offset); }
void hal_rtc_set_unix(uint32_t unix_s) { s_rtc_offset = (int64_t)unix_s - (int64_t)time(NULL); }

void hal_buttons_set_callback(hal_button_cb_t cb) { s_button_cb = cb; }
void hal_motion_set_callback(hal_motion_cb_t cb) { s_motion_cb = cb; }
void hal_motion_stream_tilt(bool enable) { (void)enable; }

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

void hal_sound_play(hal_sound_t sound) { sim_log("sound", "\"id\":%d", (int)sound); }

void hal_power_enter(hal_power_state_t state, uint32_t wake_after_s)
{
    sim_log("power_enter", "\"state\":%d,\"wake_after_s\":%u", (int)state, (unsigned)wake_after_s);
}

hal_battery_t hal_battery_read(void) { return (hal_battery_t){.percent = 80, .charging = false, .usb_present = true}; }

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
