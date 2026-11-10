/*
 * hal.h — the only interface between portable firmware code (components/core)
 * and a specific board (boards/<name>) or the desktop simulator (simulator/).
 *
 * Rules:
 *   - core/ includes only this header (plus LVGL and the protocol component).
 *   - Every board and the simulator implement every function here; features a
 *     board lacks are reported via hal_board_info()->caps and become no-ops.
 *   - Callbacks may run in ISR or driver-task context: keep them short and post
 *     to a queue. Never call LVGL from a callback.
 *
 * See docs/design/FIRMWARE.md for the bigger picture.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────── board identity & capabilities (sent in protocol `hello`) ───────────── */

typedef enum { HAL_SHAPE_ROUND = 0, HAL_SHAPE_RECT = 1 } hal_shape_t;

typedef struct {
    const char *board;        /* "m5stopwatch", "sim", ... */
    const char *fw_version;   /* semver */
    uint8_t uid[8];           /* stable device id */
    bool has_display;
    hal_shape_t shape;
    uint16_t width, height;
    bool color;
    bool has_touch, has_imu, has_haptic, has_speaker, has_mic, has_battery;
    uint8_t num_buttons;
} hal_board_info_t;

/* Bring up power rails, buses and peripherals. Called once, first thing. */
void hal_board_init(void);
const hal_board_info_t *hal_board_info(void);

/* ───────────── time ───────────── */

uint64_t hal_time_us(void);               /* monotonic since boot */
uint32_t hal_rtc_get_unix(void);          /* wall clock (RTC), 0 if unknown */
void hal_rtc_set_unix(uint32_t unix_s);

/* ───────────── display & touch (LVGL) ───────────── */

/* Create the LVGL display (buffers, flush, rounder) and return it. lv_init() must
 * already have been called. */
lv_display_t *hal_display_create(void);
/* Create the LVGL pointer input device for touch; NULL if the board has no touch. */
lv_indev_t *hal_touch_create(lv_display_t *disp);
void hal_display_set_power(bool on);       /* panel sleep + rail; off = no pixels lit */
void hal_display_set_brightness(uint8_t level); /* 0..255 */
void hal_display_set_rotation_180(bool flipped);

/* ───────────── buttons ───────────── */

typedef enum { HAL_BTN_A = 0, HAL_BTN_B = 1 } hal_button_t;
typedef enum { HAL_BTN_DOWN = 0, HAL_BTN_UP = 1 } hal_button_edge_t;
typedef void (*hal_button_cb_t)(hal_button_t btn, hal_button_edge_t edge, uint64_t t_us);
/* Raw edges only; debouncing is done by the board, gesture logic by core/. */
void hal_buttons_set_callback(hal_button_cb_t cb);

/* ───────────── motion (IMU) ───────────── */

typedef enum {
    HAL_MOTION_LIFT = 0,      /* device raised to read (lanyard → face) */
    HAL_MOTION_DROP = 1,      /* device let go / hanging again */
    HAL_MOTION_SHAKE = 2,
    HAL_MOTION_TILT = 3,      /* continuous tilt sample, see x_mg/y_mg */
} hal_motion_kind_t;
typedef struct {
    hal_motion_kind_t kind;
    int16_t x_mg, y_mg, z_mg; /* gravity vector in milli-g, screen coordinates */
    uint64_t t_us;
} hal_motion_evt_t;
typedef void (*hal_motion_cb_t)(const hal_motion_evt_t *evt);
void hal_motion_set_callback(hal_motion_cb_t cb);
/* Enable TILT samples (~30 Hz) for easter eggs; off by default to save power. */
void hal_motion_stream_tilt(bool enable);

/* ───────────── haptics & sound ───────────── */

/* Play a vibration pattern: alternating on/off durations in ms, starting with on.
 * Non-blocking; a new pattern replaces the current one. */
void hal_haptic_play(const uint16_t *on_off_ms, size_t count);

/* Speaker: 16-bit signed mono PCM at HAL_AUDIO_RATE. Non-blocking; `pcm` must stay valid until
 * it has played (the core keeps rendered sounds for the life of the app). A new sound replaces one
 * in progress. No-op without a speaker, and while the microphone records. */
#define HAL_AUDIO_RATE 16000
void hal_audio_play(const int16_t *pcm, size_t n);
/* Long-lived memory for rendered sounds (PSRAM on the watch); NULL when there is none. */
void *hal_audio_alloc(size_t bytes);

/* ───────────── microphone (voice dictation, issue 0ea9) ───────────── */

#define HAL_MIC_RATE 16000 /* mono, 16-bit signed PCM */
/* Powers the codec and starts capture; false without a mic (or when it did not answer). */
bool hal_mic_start(void);
/* Why the last hal_mic_start() failed (short text for the daemon's log), "" after a success. */
const char *hal_mic_error(void);
/* Non-blocking: copies up to `max` samples captured since the last call (about 1 s is buffered). */
size_t hal_mic_read(int16_t *pcm, size_t max);
/* Stops capture and powers the codec down. Safe to call when not started. */
void hal_mic_stop(void);

/* ───────────── power ───────────── */

typedef enum {
    HAL_POWER_ACTIVE = 0,     /* display on, full speed */
    HAL_POWER_STANDBY = 1,    /* display off, light sleep, link kept alive */
    HAL_POWER_DEEP = 2,       /* link off, only wake sources powered */
} hal_power_state_t;
/* Apply rails/clock policy for a state. DEEP may not return (board reboots on wake);
 * wake_after_s = 0 means wake only on lift/button. Returns the state actually entered: a board
 * may refuse DEEP (USB present, button held) and fall back to STANDBY. */
hal_power_state_t hal_power_enter(hal_power_state_t state, uint32_t wake_after_s);
typedef enum { HAL_WAKE_POWER_ON = 0, HAL_WAKE_BUTTON = 1, HAL_WAKE_TIMER = 2 } hal_wake_t;
/* Why this boot happened: power-on or reset, or which source ended a DEEP. */
hal_wake_t hal_wake_reason(void);
/* One word that survives DEEP (RTC memory); 0 after a power-on or reset. */
uint32_t hal_retained_get(void);
void hal_retained_set(uint32_t v);
/* Short name of the cause of the last chip reset ("poweron", "panic", "brownout", ...), for crash reports. */
const char *hal_reset_reason(void);
typedef struct { uint8_t percent; bool charging; bool usb_present; } hal_battery_t;
hal_battery_t hal_battery_read(void);

/* ───────────── link transports ───────────── */

/* A transport carries opaque protocol frames (already CBOR+envelope encoded).
 * Framing (COBS, BLE fragmentation, WebSocket frames) is the transport's job. */
typedef enum { HAL_LINK_USB = 0, HAL_LINK_BLE = 1, HAL_LINK_WS = 2 } hal_link_kind_t;
typedef void (*hal_link_rx_cb_t)(hal_link_kind_t kind, const uint8_t *frame, size_t len);
typedef void (*hal_link_state_cb_t)(hal_link_kind_t kind, bool connected);

void hal_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state);
/* Returns false if that transport is not connected or the frame is too large. */
bool hal_link_send(hal_link_kind_t kind, const uint8_t *frame, size_t len);
/* While the watch drives the PC live (SCROLL), ask the links for low latency: BLE requests a
 * 15–30 ms connection interval; false returns to the interval the central chose. No-op elsewhere. */
void hal_link_interactive(bool on);

/* ───────────── debug / observability (DESIGN §0) ───────────── */

/* Write one JSON object line to the debug channel (USB CDC `#`-prefixed lines on
 * hardware, stdout on the simulator). `json` must not contain newlines. */
void hal_debug_line(const char *json);

/* Copy the current framebuffer region as RGB565 little-endian into `dst`.
 * Returns bytes written (0 if unsupported). Used for `dbg_screenshot_req`. */
size_t hal_display_snapshot(uint32_t offset, uint8_t *dst, size_t max_len);

#ifdef __cplusplus
}
#endif
