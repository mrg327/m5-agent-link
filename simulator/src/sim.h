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
/* `--slept S`: the DEEP this boot ended lasted S seconds (the power report's deep time). */
void sim_set_deep_slept_ms(uint32_t ms);
void sim_input_motion(hal_motion_kind_t kind);
/* The pose the simulated IMU reports (milli-g, screen axes) and the palm-cover flag of the touch controller. */
void sim_set_pose(int x, int y, int z);
void sim_set_cover(bool on);
void sim_motion_poll(uint32_t now_ms); /* streams one sample per 100 ms, as the watch's IMU task does */

/* sim_display.c */
void sim_display_set_headless(bool headless);
void sim_display_pump(void);          /* SDL events (mouse/keys), then present if dirty */
bool sim_display_quit_requested(void);
void sim_input_pointer(bool down, int x, int y);
bool sim_save_png(const char *path);
void sim_fb_mark(void);              /* remember the current framebuffer */
int sim_fb_flipped_match_pct(void); /* % of pixels equal to the marked frame turned 180 degrees */  /* current framebuffer, round mask applied */

/* sim_link.c */
void sim_link_enable_ws(bool enable);
void sim_link_poll(uint32_t now_ms);
void sim_link_set_connected(bool connected); /* for --no-ws scripts */
/* `mic FILE.wav` (16 kHz mono 16-bit): the simulated microphone plays it from the next start. */
bool sim_mic_load(const char *path);
/* `connect usb|ble|ble2`, `rx usb|ble|ble2 HEX`: fake transports beside the default WS one (kind = hal_link_kind_t). */
void sim_link_set_connected_kind(int kind, bool connected);
void sim_link_inject_rx_kind(int kind, const uint8_t *frame, size_t len);
void sim_link_inject_rx(const uint8_t *frame, size_t len);
/* Called with every frame the device sends on a fake (--no-ws) transport: the script's stand-in daemon. */
typedef void (*sim_tx_tap_t)(const uint8_t *frame, size_t len);
void sim_link_set_tx_tap(sim_tx_tap_t tap);

/* sim_png.c: writes an RGB (3 bytes/pixel) image; returns false on I/O error. */
bool sim_png_write(const char *path, const uint8_t *rgb, int w, int h);

/* sim_script.c: timed scripted input. */
bool sim_script_load(const char *path);
/* Runs due actions; returns false once the script has finished (or failed: see sim_script_failed). */
bool sim_script_poll(uint32_t now_ms);
bool sim_script_failed(void);

/* main.c: CPU time spent in core_step() so far (thread CPU clock), in microseconds. */
uint64_t sim_core_cpu_us(void);

/* sim_ota.c: the script's synthetic firmware image (mode: "", "unsigned", "badhdr", "badsha"). */
bool sim_fw_image(uint32_t size, const char *mode);
uint32_t sim_fw_size(void);
const uint8_t *sim_fw_sha(void);
const uint8_t *sim_fw_data(void);
/* A delta-OTA fixture (mode: "", "otherbase", "corrupt"): `path` is a patch next to base.bin and new.bin. The
 * image is new.bin, the running slot base.bin; fw_send then sends the patch. */
bool sim_fw_patch(const char *path, const char *mode);
bool sim_fw_is_patch(void);
const uint8_t *sim_fw_base_sha(void);
uint32_t sim_fw_base_size(void);
/* What fw_send sends: the patch in patch mode, else the image. */
uint32_t sim_fw_xfer_size(void);
const uint8_t *sim_fw_xfer_data(void);
/* The spare slot holds exactly the image. */
bool sim_fw_slot_matches(void);
/* sim_ota.c: the running slot a patch applies to; raw = no header or signature checks (fixtures);
 * the spare slot's bytes so far. */
void sim_ota_set_running(const uint8_t *img, uint32_t size);
void sim_ota_set_raw(bool raw);
const uint8_t *sim_ota_slot(uint32_t *size);
/* sim_board.c: the battery reading (`battery PCT [chg]`) and a jump of the monotonic clock (`skip MS`). */
void sim_set_battery_pct(unsigned pct, bool charging);
void sim_skip_time(uint32_t ms);
