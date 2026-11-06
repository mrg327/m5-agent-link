// CO5300 466x466 round AMOLED over QSPI via the M5GFX Panel_AMOLED (as in the factory firmware),
// plus the LVGL display / touch glue.
#include <stdlib.h>

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

#define LV_BUF_LINES 48 /* two 466x48 RGB565 partial buffers (~44 KB each) */
#define FLUSH_CHUNK_PX 8192 /* the factory firmware chunks large pushes to dodge an M5GFX SIMD copy issue */

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

    // Re-run the init commands after the AMOLED rail (L3B) was power-cycled.
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
        // Full-screen PSRAM framebuffer: partial LVGL flushes become 2-px aligned QSPI pushes.
        if (panel.initPanelFb()) {
            auto fb = panel.getPanelFb();
            if (fb) {
                fb->setBus(&bus);
                fb->setAutoDisplay(true);
                setPanel(fb);
            }
        }
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

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    uint32_t pixels = w * h;

    xSemaphoreTake(s_gfx_mtx, portMAX_DELAY);
    if (s_powered) {
        s_gfx->startWrite();
        s_gfx->setAddrWindow(area->x1, area->y1, w, h);
        const lgfx::rgb565_t *src = (const lgfx::rgb565_t *)px_map;
        while (pixels) {
            uint32_t n = pixels > FLUSH_CHUNK_PX ? FLUSH_CHUNK_PX : pixels;
            s_gfx->writePixels(src, n);
            src += n;
            pixels -= n;
        }
        s_gfx->endWrite();
    }
    xSemaphoreGive(s_gfx_mtx);
    lv_display_flush_ready(disp);
}

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
    if (s_powered && sw_touch_read(&x, &y)) {
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
    if (s_powered) {
        s_gfx->panel.setBrightness(level);
    }
    xSemaphoreGive(s_gfx_mtx);
}

void hal_display_set_power(bool on)
{
    if (!s_gfx || on == s_powered) {
        return;
    }
    xSemaphoreTake(s_gfx_mtx, portMAX_DELAY);
    if (on) {
        sw_ioe_write(SW_IOE_L3B_AMOLED, true);
        vTaskDelay(pdMS_TO_TICKS(20));
        sw_ioe_write(SW_IOE_OLED_RST, false);
        vTaskDelay(pdMS_TO_TICKS(10));
        sw_ioe_write(SW_IOE_OLED_RST, true);
        vTaskDelay(pdMS_TO_TICKS(20));
        s_gfx->panel.reinitAfterRail();
        s_powered = true;
        s_gfx->display(0, 0, SW_PANEL_W, SW_PANEL_H); /* repaint from the PSRAM framebuffer */
        s_gfx->waitDisplay();
        s_gfx->panel.setBrightness(s_brightness);
        sw_pm1_led(false);
    } else {
        s_powered = false;
        s_gfx->panel.setBrightness(0);
        s_gfx->panel.setSleep(true);
        sw_ioe_write(SW_IOE_L3B_AMOLED, false);
        sw_pm1_led(true);
    }
    xSemaphoreGive(s_gfx_mtx);
}

void hal_display_set_rotation_180(bool flipped)
{
    if (s_disp) {
        lv_display_set_rotation(s_disp, flipped ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0);
    }
}

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
