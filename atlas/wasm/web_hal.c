/* hal.h for the browser: framebuffer for JS to blit, input and link driven from JS, haptics and debug
 * lines delivered through Module.hooks. Mirrors simulator/src/sim_*.c. */
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hal.h"
#include "web_hal.h"

#define W 466
#define H 466
#define BUF_ROWS 40

static const hal_board_info_t s_info = {
    .board = "web", .fw_version = "0.1.0-web", .uid = {0x77, 0x65, 0x62, 0, 0, 0, 0, 1},
    .has_display = true, .shape = HAL_SHAPE_ROUND, .width = W, .height = H, .color = true,
    .has_touch = true, .has_imu = false, .has_haptic = true, .has_speaker = false, .has_mic = false,
    .has_battery = true, .num_buttons = 2,
};

static uint16_t s_fb[W * H];
static uint32_t s_rgba[W * H];
static uint16_t s_buf1[W * BUF_ROWS], s_buf2[W * BUF_ROWS];
static int s_dirty[4];
static bool s_has_dirty, s_power = true;
static uint8_t s_brightness = 255;
static struct { bool down; int x, y; } s_ptr;
static hal_button_cb_t s_button_cb;
static hal_motion_cb_t s_motion_cb;
static hal_link_rx_cb_t s_rx;
static hal_link_state_cb_t s_state;
static bool s_up[3];
static int64_t s_rtc_offset;

EM_JS(void, js_haptic, (const uint16_t *p, int n), {
    const h = Module.hooks && Module.hooks.haptic;
    if (h) h(Array.from(HEAPU16.subarray(p >> 1, (p >> 1) + n)));
});
EM_JS(void, js_tx, (int kind, const uint8_t *p, int n), {
    const h = Module.hooks && Module.hooks.tx;
    if (h) h(kind, HEAPU8.slice(p, p + n));
});
EM_JS(void, js_debug, (const char *s), {
    const h = Module.hooks && Module.hooks.debug;
    if (h) h(UTF8ToString(s));
});

void hal_board_init(void) {}
const hal_board_info_t *hal_board_info(void) { return &s_info; }
uint64_t hal_time_us(void) { return (uint64_t)(emscripten_get_now() * 1000.0); }
uint32_t hal_rtc_get_unix(void) { return (uint32_t)((int64_t)time(NULL) + s_rtc_offset); }
void hal_rtc_set_unix(uint32_t unix_s) { s_rtc_offset = (int64_t)unix_s - (int64_t)time(NULL); }
void web_set_wallclock(uint32_t unix_s) { hal_rtc_set_unix(unix_s); }

/* ── display ── */

static inline uint32_t to_rgba(uint16_t c)
{
    uint32_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
    r = r << 3 | r >> 2;
    g = g << 2 | g >> 4;
    b = b << 3 | b >> 2;
    if (!s_power) r = g = b = 0;
    else if (s_brightness < 255) {
        r = r * s_brightness / 255;
        g = g * s_brightness / 255;
        b = b * s_brightness / 255;
    }
    return 0xff000000u | b << 16 | g << 8 | r;
}

static void mark_dirty(int x1, int y1, int x2, int y2)
{
    if (!s_has_dirty) {
        s_dirty[0] = x1, s_dirty[1] = y1, s_dirty[2] = x2, s_dirty[3] = y2;
        s_has_dirty = true;
        return;
    }
    if (x1 < s_dirty[0]) s_dirty[0] = x1;
    if (y1 < s_dirty[1]) s_dirty[1] = y1;
    if (x2 > s_dirty[2]) s_dirty[2] = x2;
    if (y2 > s_dirty[3]) s_dirty[3] = y2;
}

static void convert(int x1, int y1, int x2, int y2)
{
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++) s_rgba[y * W + x] = to_rgba(s_fb[y * W + x]);
    mark_dirty(x1, y1, x2, y2);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++, src += w) memcpy(&s_fb[y * W + a->x1], src, (size_t)w * 2);
    convert(a->x1, a->y1, a->x2, a->y2);
    lv_display_flush_ready(disp);
}

static void pointer_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = s_ptr.x;
    data->point.y = s_ptr.y;
    data->state = s_ptr.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

lv_display_t *hal_display_create(void)
{
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, s_buf1, s_buf2, sizeof s_buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    return d;
}

lv_indev_t *hal_touch_create(lv_display_t *disp)
{
    lv_indev_t *in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, pointer_read_cb);
    lv_indev_set_display(in, disp);
    return in;
}

void hal_display_set_power(bool on)
{
    s_power = on;
    convert(0, 0, W - 1, H - 1);
}
void hal_display_set_brightness(uint8_t level)
{
    s_brightness = level;
    convert(0, 0, W - 1, H - 1);
}
void hal_display_set_rotation_180(bool flipped) { (void)flipped; }

size_t hal_display_snapshot(uint32_t offset, uint8_t *dst, size_t max_len)
{
    if (offset >= sizeof s_fb) return 0;
    size_t n = sizeof s_fb - offset < max_len ? sizeof s_fb - offset : max_len;
    memcpy(dst, (const uint8_t *)s_fb + offset, n);
    return n;
}

int web_take_dirty(void)
{
    int had = s_has_dirty;
    s_has_dirty = false;
    return had;
}
const int *web_dirty_rect(void) { return s_dirty; }
const uint32_t *web_framebuffer(void) { return s_rgba; }

/* ── input ── */

void web_pointer(bool down, int x, int y)
{
    s_ptr.down = down;
    s_ptr.x = x;
    s_ptr.y = y;
}
void hal_buttons_set_callback(hal_button_cb_t cb) { s_button_cb = cb; }
void web_button(int btn, bool down)
{
    if (s_button_cb) s_button_cb(btn ? HAL_BTN_B : HAL_BTN_A, down ? HAL_BTN_DOWN : HAL_BTN_UP, hal_time_us());
}
void hal_motion_set_callback(hal_motion_cb_t cb) { s_motion_cb = cb; }
void hal_motion_stream_tilt(bool enable) { (void)enable; }
void web_motion(int kind)
{
    hal_motion_evt_t e = {.kind = (hal_motion_kind_t)kind, .z_mg = 1000, .t_us = hal_time_us()};
    if (s_motion_cb) s_motion_cb(&e);
}

/* ── haptics, power, battery, debug ── */

void hal_haptic_play(const uint16_t *on_off_ms, size_t count) { js_haptic(on_off_ms, (int)count); }
void hal_audio_play(const int16_t *pcm, size_t n) { (void)pcm; (void)n; }
void hal_audio_set_volume(uint8_t percent) { (void)percent; }
bool hal_touch_covered(void) { return false; }
uint32_t hal_pref_get(const char *key, uint32_t fallback) { (void)key; return fallback; }
void hal_pref_set(const char *key, uint32_t value) { (void)key; (void)value; }
void *hal_audio_alloc(size_t bytes) { (void)bytes; return NULL; }
hal_power_state_t hal_power_enter(hal_power_state_t state, uint32_t wake_after_s)
{
    (void)wake_after_s;
    return state;
}
hal_wake_t hal_wake_reason(void) { return HAL_WAKE_POWER_ON; }
uint32_t hal_retained_get(void) { return 0; }
void hal_retained_set(uint32_t v) { (void)v; }
const char *hal_reset_reason(void) { return "poweron"; }

/* No microphone in the browser (has_mic is false): voice dictation reports "No microphone". */
bool hal_mic_start(void) { return false; }
const char *hal_mic_error(void) { return "no microphone in the browser"; }
size_t hal_mic_read(int16_t *pcm, size_t max)
{
    (void)pcm;
    (void)max;
    return 0;
}
void hal_mic_stop(void) {}
hal_battery_t hal_battery_read(void) { return (hal_battery_t){.percent = 80, .charging = false, .usb_present = true}; }
void hal_debug_line(const char *json) { js_debug(json); }

/* ── link: frames are raw CBOR envelopes, one per call (same as the simulator's WebSocket) ── */

void hal_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state)
{
    s_rx = on_rx;
    s_state = on_state;
}

bool hal_link_send(hal_link_kind_t kind, const uint8_t *frame, size_t len)
{
    if ((unsigned)kind > 2 || !s_up[kind]) return false;
    js_tx((int)kind, frame, (int)len);
    return true;
}

void hal_link_interactive(bool on) { (void)on; }

void web_link_up(int kind, bool up)
{
    if ((unsigned)kind > 2) return;
    s_up[kind] = up;
    if (s_state) s_state((hal_link_kind_t)kind, up);
}

void web_deliver(int kind, const uint8_t *frame, size_t len)
{
    if (s_rx && (unsigned)kind <= 2) s_rx((hal_link_kind_t)kind, frame, len);
}
