/* Simulator-internal API shared between the HAL implementation files, main and the script runner. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal.h"

/* sim_board.c: input injection (same path as real keys / IMU) and logging helper. */
void sim_log(const char *ev, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sim_input_button(int btn, bool down);
/* Power tests: run on battery (no USB), and pretend this boot ended a DEEP. */
void sim_set_battery(bool on_battery);
void sim_set_wake(hal_wake_t why, uint32_t retained);
void sim_input_motion(hal_motion_kind_t kind);

/* sim_display.c */
void sim_display_set_headless(bool headless);
void sim_display_pump(void);          /* SDL events (mouse/keys), then present if dirty */
bool sim_display_quit_requested(void);
void sim_input_pointer(bool down, int x, int y);
bool sim_save_png(const char *path);  /* current framebuffer, round mask applied */

/* sim_link.c */
void sim_link_enable_ws(bool enable);
void sim_link_poll(uint32_t now_ms);
void sim_link_set_connected(bool connected); /* for --no-ws scripts */
/* `connect usb|ble`, `rx usb|ble HEX`: fake transports beside the default WS one (kind = hal_link_kind_t). */
void sim_link_set_connected_kind(int kind, bool connected);
void sim_link_inject_rx_kind(int kind, const uint8_t *frame, size_t len);
void sim_link_inject_rx(const uint8_t *frame, size_t len);

/* sim_png.c: writes an RGB (3 bytes/pixel) image; returns false on I/O error. */
bool sim_png_write(const char *path, const uint8_t *rgb, int w, int h);

/* sim_script.c: timed scripted input. */
bool sim_script_load(const char *path);
/* Runs due actions; returns false once the script has finished (or failed: see sim_script_failed). */
bool sim_script_poll(uint32_t now_ms);
bool sim_script_failed(void);

/* main.c: CPU time spent in core_step() so far (thread CPU clock), in microseconds. */
uint64_t sim_core_cpu_us(void);
