// CO5300 466x466 round AMOLED over QSPI via the M5GFX Panel_AMOLED (as in the factory firmware),
// plus the LVGL display / touch glue.
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_AMOLED.hpp>

#include "board_priv.h"
#include "hal.h"

static const char *TAG = "sw-display";

#define PIN_SCLK GPIO_NUM_40
#define PIN_IO0 GPIO_NUM_41
#define PIN_IO1 GPIO_NUM_42
#define PIN_IO2 GPIO_NUM_46
#define PIN_IO3 GPIO_NUM_45
#define PIN_CS GPIO_NUM_39
#define PIN_TE GPIO_NUM_38

#ifdef CONFIG_AGENTLINK_DISPLAY_DIRECT_DMA
#define SW_DIRECT 1
/* 466*34*2 = 31688 B: a full-width band fits one ESP32-S3 SPI transaction (<= 32 KB), so M5GFX
 * starts it with a single DMA and returns at once (larger pushes busy-wait on all but the last 32 KB). */
#define LV_BUF_LINES 34
#else
#define SW_DIRECT 0
#define LV_BUF_LINES 48 /* two 466x48 RGB565 partial buffers (~44 KB each) */
#define FLUSH_CHUNK_PX 8192 /* the factory firmware chunks large pushes to dodge an M5GFX SIMD copy issue */
#endif

class Panel_CO5300 : public lgfx::Panel_AMOLED {
public:
    Panel_CO5300()
    {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 480;
        _write_depth = lgfx::color_depth_t::rgb565_2Byte;
        _read_depth = lgfx::color_depth_t::rgb565_2Byte;
    }

    const uint8_t *getInitCommands(uint8_t listno) const override
    {
        static constexpr uint8_t list0[] = {
            0x11, 0 + CMD_INIT_DELAY, 150, /* sleep out */
            0xC4, 1, 0x80,
            0x35, 1, 0x80,
            0x44, 2, 0x01, 0xD2, /* TE line at 466 */
            0x53, 1, 0x20,
            0x36, 1, 0x00,
            0x51, 1, 0xA0,
            0x29, 0, /* display on */
            0xFF, 0xFF,
        };
        return listno == 0 ? list0 : nullptr;
    }

    // Re-run the init commands (sleep-out, TE, brightness mode, display on) after a panel sleep.
    void reinitAfterRail()
    {
        startWrite(true);
        for (uint8_t i = 0; auto cmds = getInitCommands(i); i++) {
            command_list(cmds);
        }
        endWrite();
    }
};

class M5StopWatchGfx : public M5GFX {
public:
    lgfx::Bus_SPI bus;
    Panel_CO5300 panel;

    bool init_impl(bool use_reset, bool use_clear) override
    {
        {
            auto cfg = bus.config();
            cfg.freq_write = 80000000;
            cfg.freq_read = 10000000;
            cfg.pin_sclk = PIN_SCLK;
            cfg.pin_io0 = PIN_IO0;
            cfg.pin_io1 = PIN_IO1;
            cfg.pin_io2 = PIN_IO2;
            cfg.pin_io3 = PIN_IO3;
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.spi_3wire = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            bus.config(cfg);
            panel.setBus(&bus);
        }
        {
            auto cfg = panel.config();
            cfg.pin_rst = GPIO_NUM_NC; /* reset is PYG5 on the IO expander */
            cfg.pin_cs = PIN_CS;
            cfg.panel_width = 468;
            cfg.panel_height = 466;
            cfg.offset_x = 6;
            cfg.offset_y = 0;
            cfg.readable = false;
            panel.config(cfg);
        }
        setPanel(&panel);
        lgfx::pinMode(PIN_TE, lgfx::pin_mode_t::input_pullup);

        if (!LGFX_Device::init_impl(use_reset, use_clear)) {
            return false;
        }
#if !SW_DIRECT
        // Full-screen PSRAM framebuffer: partial LVGL flushes become 2-px aligned QSPI pushes.
        if (panel.initPanelFb()) {
            auto fb = panel.getPanelFb();
            if (fb) {
                fb->setBus(&bus);
                fb->setAutoDisplay(true);
                setPanel(fb);
            }
        }
#endif
        panel.setBrightness(128);
        return true;
    }
};

static M5StopWatchGfx *s_gfx;
static SemaphoreHandle_t s_gfx_mtx;
static lv_display_t *s_disp;
static bool s_powered = true;
static uint8_t s_brightness = 160;

static uint32_t tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

#ifdef CONFIG_AGENTLINK_DISPLAY_STATS
static struct { uint32_t n, px; int64_t cpu_us, wait_us, t0; } s_st;
static void stats_tick(void)
{
    int64_t now = esp_timer_get_time();
    if (!s_st.t0) s_st.t0 = now;
    if (now - s_st.t0 < 5000000) return;
    ESP_LOGI(TAG, "flush %u x, %u px, cpu %lld us, dma wait %lld us in %lld ms", (unsigned)s_st.n,
             (unsigned)s_st.px, s_st.cpu_us, s_st.wait_us, (now - s_st.t0) / 1000);
    s_st = {};
    s_st.t0 = now;
}
#define ST_T0() int64_t st_t0 = esp_timer_get_time()
#define ST_ADD(field) (s_st.field += esp_timer_get_time() - st_t0)
#else
#define ST_T0() do {} while (0)
#define ST_ADD(field) do {} while (0)
#endif

#if SW_DIRECT
static bool s_dma_ok;  /* draw buffers are internal DMA-capable RAM (a PSRAM fallback would need cache writeback) */
static bool s_pending; /* a band is still on the wire and its bus transaction is open */
static uint16_t *s_shot; /* hal_display_snapshot capture (PSRAM), filled by flush_cb while s_capture */
static bool s_capture;

/* Caller holds s_gfx_mtx. endWrite waits for the DMA, raises CS and releases the SPI bus. */
static void dma_finish_locked(void)
{
    if (s_pending) {
        s_gfx->endWrite();
        s_pending = false;
    }
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    ST_T0();
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);
    if (s_capture && s_shot) {
        for (int32_t y = 0; y < h; y++) {
            memcpy(&s_shot[(area->y1 + y) * SW_PANEL_W + area->x1], px_map + y * w * 2, (size_t)w * 2);
        }
    }
    lv_draw_sw_rgb565_swap(px_map, (uint32_t)(w * h)); /* the CO5300 takes big-endian RGB565 */

    xSemaphoreTake(s_gfx_mtx, portMAX_DELAY);
    dma_finish_locked(); /* LVGL already waited via flush_wait_cb; this only guards a direct call */
    s_gfx->startWrite(); /* held open past pushImageDMA so its endWrite does not wait for the bus */
    if (s_dma_ok) {
        s_gfx->pushImageDMA(area->x1, area->y1, w, h, (const lgfx::swap565_t *)px_map);
        s_pending = true;
    } else {
        s_gfx->pushImage(area->x1, area->y1, w, h, (const lgfx::swap565_t *)px_map);
        s_gfx->endWrite();
    }
    xSemaphoreGive(s_gfx_mtx);
#ifdef CONFIG_AGENTLINK_DISPLAY_STATS
    s_st.n++;
    s_st.px += (uint32_t)(w * h);
#endif
    ST_ADD(cpu_us);
    if (!s_dma_ok) {
        lv_display_flush_ready(disp);
    }
}

/* LVGL calls this before reusing a buffer that is still being flushed (and treats it as flushed after). */
static void flush_wait_cb(lv_display_t *disp)
{
    (void)disp;
    ST_T0();
    xSemaphoreTake(s_gfx_mtx, portMAX_DELAY);
    dma_finish_locked();
    xSemaphoreGive(s_gfx_mtx);
    ST_ADD(wait_us);
#ifdef CONFIG_AGENTLINK_DISPLAY_STATS
    stats_tick();
#endif
}
#else
static inline void dma_finish_locked(void) {}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    ST_T0();
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    uint32_t pixels = w * h;

    xSemaphoreTake(s_gfx_mtx, portMAX_DELAY);
    { /* always draw, even while dark: the panel never sleeps, so it never falls behind */
        s_gfx->startWrite();
        s_gfx->setAddrWindow(area->x1, area->y1, w, h);
        const lgfx::rgb565_t *src = (const lgfx::rgb565_t *)px_map;
        while (pixels) {
            uint32_t n = pixels > FLUSH_CHUNK_PX ? FLUSH_CHUNK_PX : pixels;
            s_gfx->writePixels(src, n);
            src += n;
            pixels -= n;
        }
        s_gfx->endWrite(); /* autoDisplay: pushes the dirty rect from the PSRAM framebuffer */
    }
    xSemaphoreGive(s_gfx_mtx);
#ifdef CONFIG_AGENTLINK_DISPLAY_STATS
    s_st.n++;
    s_st.px += w * h;
    ST_ADD(cpu_us);
    stats_tick();
#endif
    lv_display_flush_ready(disp);
}
#endif

// CO5300 needs even start/odd end coordinates.
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
    if (a->x2 > SW_PANEL_W - 1) a->x2 = SW_PANEL_W - 1;
    if (a->y2 > SW_PANEL_H - 1) a->y2 = SW_PANEL_H - 1;
}

lv_display_t *hal_display_create(void)
{
    sw_pm1_led(false); /* the AMOLED is the power indicator; keep the green LED dark */

    s_gfx_mtx = xSemaphoreCreateMutex();
    s_gfx = new M5StopWatchGfx();
    if (!s_gfx->init()) {
        ESP_LOGE(TAG, "M5GFX init failed");
        delete s_gfx;
        s_gfx = nullptr;
        return nullptr;
    }

    size_t buf_bytes = (size_t)SW_PANEL_W * LV_BUF_LINES * 2;
    void *b1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
#if SW_DIRECT
    s_dma_ok = b1 && b2;
#endif
    if (!b1 || !b2) {
        ESP_LOGW(TAG, "internal DMA buffers unavailable, falling back to PSRAM");
        heap_caps_free(b1);
        heap_caps_free(b2);
        b1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
        b2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    }
    if (!b1 || !b2) {
        ESP_LOGE(TAG, "no LVGL draw buffers");
        return nullptr;
    }

    lv_tick_set_cb(tick_cb);
    s_disp = lv_display_create(SW_PANEL_W, SW_PANEL_H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, flush_cb);
#if SW_DIRECT
    lv_display_set_flush_wait_cb(s_disp, flush_wait_cb);
#endif
    lv_display_set_buffers(s_disp, b1, b2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(s_disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, nullptr);

    hal_display_set_brightness(s_brightness);
    return s_disp;
}

/* ───────────── touch (LVGL pointer) ───────────── */

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static lv_point_t last = {0, 0};
    uint16_t x, y;
    if (sw_touch_read(&x, &y)) { /* read while dark too: a tap wakes the display (the core spends it) */
        last.x = x >= SW_PANEL_W ? SW_PANEL_W - 1 : x;
        last.y = y >= SW_PANEL_H ? SW_PANEL_H - 1 : y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point = last; /* LVGL rotates pointer coordinates when the display is rotated */
}

lv_indev_t *hal_touch_create(lv_display_t *disp)
{
    if (!sw_touch_init()) {
        return nullptr;
    }
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    if (disp) {
        lv_indev_set_display(indev, disp);
    }
    return indev;
}

/* ───────────── power / brightness / rotation ───────────── */

void hal_display_set_brightness(uint8_t level)
{
    s_brightness = level;
    if (!s_gfx) {
        return;
    }
    xSemaphoreTake(s_gfx_mtx, portMAX_DELAY);
    dma_finish_locked();
    if (s_powered) {
        s_gfx->panel.setBrightness(level);
    }
    xSemaphoreGive(s_gfx_mtx);
}

/* Display off = brightness 0, nothing else. The panel keeps running and LVGL keeps drawing into it,
 * so on wake only the brightness changes. Two attempts to sleep the CO5300 (rail cut + re-init, then
 * sleep-in/out) both woke with a scrambled picture (mixed frames, shifted copy), so the panel's own
 * sleep stays unused until it can be debugged on the bench. An AMOLED at brightness 0 emits nothing;
 * the driver IC still draws a few mA, and deep sleep covers long idle on battery. */
void hal_display_set_power(bool on)
{
    if (!s_gfx || on == s_powered) {
        return;
    }
    xSemaphoreTake(s_gfx_mtx, portMAX_DELAY);
    dma_finish_locked();
    s_powered = on;
    s_gfx->panel.setBrightness(on ? s_brightness : 0);
    sw_pm1_led(!on);
    xSemaphoreGive(s_gfx_mtx);
}

void hal_display_set_rotation_180(bool flipped)
{
    if (s_disp) {
        lv_display_set_rotation(s_disp, flipped ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0);
    }
}

#if SW_DIRECT
/* No framebuffer to read back: offset 0 forces a full redraw that flush_cb copies (pre-swap) into a
 * PSRAM buffer, which then serves the rest of the transfer and is freed after the last byte. */
size_t hal_display_snapshot(uint32_t offset, uint8_t *dst, size_t max_len)
{
    const size_t total = (size_t)SW_PANEL_W * SW_PANEL_H * 2;
    if (!s_disp || offset >= total || !dst) {
        return 0;
    }
    if (offset == 0 || !s_shot) {
        if (!s_shot) {
            s_shot = (uint16_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
        }
        if (!s_shot) {
            return 0;
        }
        s_capture = true;
        lv_obj_invalidate(lv_display_get_screen_active(s_disp));
        lv_refr_now(s_disp);
        s_capture = false;
    }
    size_t n = total - offset < max_len ? total - offset : max_len;
    memcpy(dst, (const uint8_t *)s_shot + offset, n);
    if (offset + n >= total) {
        heap_caps_free(s_shot);
        s_shot = nullptr;
    }
    return n;
}
#else
size_t hal_display_snapshot(uint32_t offset, uint8_t *dst, size_t max_len)
{
    const size_t row_bytes = (size_t)SW_PANEL_W * 2;
    const size_t total = row_bytes * SW_PANEL_H;
    if (!s_gfx || offset >= total || !dst) {
        return 0;
    }
    static uint16_t row[SW_PANEL_W];
    size_t written = 0;
    xSemaphoreTake(s_gfx_mtx, portMAX_DELAY);
    while (written < max_len && offset + written < total) {
        size_t pos = offset + written;
        int y = (int)(pos / row_bytes);
        size_t in_row = pos % row_bytes;
        // TODO(verify on device): read-back goes through the PSRAM framebuffer panel.
        s_gfx->readRect(0, y, SW_PANEL_W, 1, (lgfx::rgb565_t *)row);
        size_t n = row_bytes - in_row;
        if (n > max_len - written) n = max_len - written;
        memcpy(dst + written, (const uint8_t *)row + in_row, n);
        written += n;
    }
    xSemaphoreGive(s_gfx_mtx);
    return written;
}
#endif
