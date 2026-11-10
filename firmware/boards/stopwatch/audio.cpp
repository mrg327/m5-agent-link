// Audio: the ES8311 codec (I2C 0x18 on the system bus) and I2S0, shared by the microphone
// (mic.cpp) and the speaker (the AW8737A amp, driven by the task below).
// The ESP32-S3 is the I2S master in full duplex at 16 kHz mono and drives MCLK at 256 x fs
// (4.096 MHz); the codec is a slave and clocks its ADC and DAC from MCLK. The audio rail (IOE1
// PYG3, PYB_AU_EN) powers the codec and the mic; IOE1 PYG10 (PYB_SPK_EN, and G14 per the
// schematic) enables the amp. Register values follow Espressif's es8311 driver (esp_codec_dev).
// sw_audio_on()/sw_audio_off() are reference counted: the first user powers everything up, the
// last powers it down and frees the DMA buffers.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board_priv.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "audio";

#define ES8311_ADDR 0x18
#define PIN_MCLK GPIO_NUM_18
#define PIN_BCLK GPIO_NUM_17
#define PIN_DIN GPIO_NUM_16  /* codec ASDOUT -> ESP (mic) */
#define PIN_WS GPIO_NUM_15
#define PIN_DOUT GPIO_NUM_21 /* ESP -> codec DSDIN (speaker) */
#define DAC_VOLUME 0xBF      /* 0xBF = 0 dB, 0.5 dB per step */
#define AMP_SETTLE_MS 10     /* amp enable to first sample */
#define SPEAKER_IDLE_MS 2500 /* keep the amp up this long after a sound, so taps play at once */
#define CHUNK_SAMPLES 256

static SemaphoreHandle_t s_lock;
static int s_users;
static i2c_master_dev_handle_t s_codec;
static i2s_chan_handle_t s_tx, s_rx;
static char s_err[80];

__attribute__((format(printf, 1, 2))) static bool FAIL(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_err, sizeof s_err, fmt, ap);
    va_end(ap);
    ESP_LOGW(TAG, "%s", s_err);
    return false;
}

static bool wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    esp_err_t e = i2c_master_transmit(s_codec, b, 2, 50);
    if (e != ESP_OK) return FAIL("codec reg 0x%02x: %s", reg, esp_err_to_name(e));
    return true;
}

static bool codec_init(void)
{
    if (!s_codec) {
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = ES8311_ADDR;
        cfg.scl_speed_hz = SW_I2C_DEV_HZ;
        esp_err_t e = i2c_master_bus_add_device(sw_i2c_bus(), &cfg, &s_codec);
        if (e != ESP_OK) return FAIL("codec add: %s", esp_err_to_name(e));
    }
    /* reg, value: reset and clocks for MCLK = 256 fs at 16 kHz (pre_div 1, adc_osr 0x10) */
    static const uint8_t seq[][2] = {
        {0x45, 0x00}, /* GP: normal */
        {0x01, 0x30}, /* clock manager: MCLK from the pin, clocks off while configuring */
        {0x02, 0x00}, /* pre-divider 1, multiplier 1 */
        {0x03, 0x10}, /* ADC fs mode single speed, OSR 16 */
        {0x16, 0x24}, /* ADC: mic gain / sync defaults */
        {0x04, 0x10}, /* DAC OSR 16 */
        {0x05, 0x00}, /* ADC and DAC clock dividers 1 */
        {0x0B, 0x00}, {0x0C, 0x00},
        {0x10, 0x1F}, {0x11, 0x7F},
        {0x00, 0x80}, /* reset off, slave mode, power on the state machine */
        {0x01, 0x3F}, /* all clocks on, MCLK from the pin */
        {0x13, 0x10}, /* output drive for the amp input */
        {0x1B, 0x0A}, {0x1C, 0x6A}, /* ADC HPF and EQ bypass */
        {0x0A, 0x0C}, /* serial data out (ADC): I2S, 16-bit */
        {0x09, 0x0C}, /* serial data in (DAC): I2S, 16-bit, left slot */
        {0x17, 0xBF}, /* ADC volume 0 dB */
        {0x0E, 0x02}, /* power up the PGA and ADC modulator */
        {0x12, 0x00}, /* power up the DAC */
        {0x14, 0x1A}, /* analog mic on MIC1P/N, PGA +30 dB */
        {0x0D, 0x01}, /* power up the analog circuits */
        {0x15, 0x40}, /* ADC ramp rate */
        {0x37, 0x08}, /* DAC EQ bypass */
        {0x32, DAC_VOLUME},
        {0x31, 0x00}, /* DAC unmuted */
    };
    for (size_t i = 0; i < sizeof seq / sizeof *seq; i++)
        if (!wr(seq[i][0], seq[i][1])) return false;
    return true;
}

static void i2s_free(void)
{
    if (s_tx) i2s_del_channel(s_tx);
    if (s_rx) i2s_del_channel(s_rx);
    s_tx = s_rx = NULL;
}

static bool i2s_init(void)
{
    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ch.dma_desc_num = 6;
    ch.dma_frame_num = 320; /* 20 ms */
    ch.auto_clear = true;   /* the DAC gets silence, not a looping buffer, when nothing is written */
    esp_err_t e = i2s_new_channel(&ch, &s_tx, &s_rx);
    if (e != ESP_OK) return FAIL("i2s channel: %s", esp_err_to_name(e));
    i2s_std_config_t std = {};
    std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(HAL_AUDIO_RATE);
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    std.gpio_cfg.mclk = PIN_MCLK;
    std.gpio_cfg.bclk = PIN_BCLK;
    std.gpio_cfg.ws = PIN_WS;
    std.gpio_cfg.dout = PIN_DOUT;
    std.gpio_cfg.din = PIN_DIN;
    if ((e = i2s_channel_init_std_mode(s_tx, &std)) != ESP_OK || (e = i2s_channel_init_std_mode(s_rx, &std)) != ESP_OK) {
        i2s_free();
        return FAIL("i2s std mode: %s", esp_err_to_name(e));
    }
    return true;
}

static void power_down(void)
{
    if (s_tx) i2s_channel_disable(s_tx);
    if (s_rx) i2s_channel_disable(s_rx);
    if (s_codec) {
        wr(0x31, 0x60); /* DAC muted */
        wr(0x12, 0x02); /* DAC off */
        wr(0x0D, 0xFC); /* analog circuits off */
        wr(0x0E, 0x6A); /* PGA and ADC off */
    }
    i2s_free();
    sw_ioe_write(SW_IOE_AUDIO_EN, false);
}

static bool power_up(void)
{
    s_err[0] = 0;
    sw_ioe_write(SW_IOE_AUDIO_EN, true);
    vTaskDelay(pdMS_TO_TICKS(20)); /* rail and codec settle */
    if (!i2s_init()) {
        sw_ioe_write(SW_IOE_AUDIO_EN, false);
        return false;
    }
    /* MCLK must run before the codec's clock manager is configured */
    esp_err_t e = i2s_channel_enable(s_tx);
    if (e == ESP_OK) e = i2s_channel_enable(s_rx);
    if (e != ESP_OK) {
        FAIL("i2s enable: %s", esp_err_to_name(e));
        power_down();
        return false;
    }
    if (!codec_init()) {
        power_down();
        return false;
    }
    ESP_LOGI(TAG, "codec on");
    return true;
}

/* Created on the core task (the first mic start or sound), before the speaker task exists. */
static void lock_create(void)
{
    static StaticSemaphore_t buf;
    if (!s_lock) s_lock = xSemaphoreCreateMutexStatic(&buf);
}

static void lock(void)
{
    lock_create();
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

extern "C" bool sw_audio_on(void)
{
    lock();
    bool ok = s_users > 0 || power_up();
    if (ok) s_users++;
    xSemaphoreGive(s_lock);
    return ok;
}

extern "C" void sw_audio_off(void)
{
    lock();
    if (s_users > 0 && --s_users == 0) {
        power_down();
        ESP_LOGI(TAG, "codec off");
    }
    xSemaphoreGive(s_lock);
}

extern "C" i2s_chan_handle_t sw_audio_rx(void) { return s_rx; }
extern "C" const char *sw_audio_error(void) { return s_err; }

/* ───────────── speaker ───────────── */

typedef struct {
    const int16_t *pcm;
    size_t n;
} play_t;

static QueueHandle_t s_q;

static void amp(bool on)
{
    sw_ioe_write(SW_IOE_SPK_EN, on);
    gpio_set_level((gpio_num_t)SW_PIN_SPK_PA, on);
}

static void speaker_task(void *)
{
    bool on = false;
    play_t p;
    while (true) {
        if (xQueueReceive(s_q, &p, on ? pdMS_TO_TICKS(SPEAKER_IDLE_MS) : portMAX_DELAY) != pdTRUE) {
            amp(false);
            sw_audio_off();
            on = false;
            continue;
        }
        if (sw_mic_active()) continue; /* the speaker would be heard in the dictation */
        if (!on) {
            if (!sw_audio_on()) continue;
            amp(true);
            vTaskDelay(pdMS_TO_TICKS(AMP_SETTLE_MS));
            on = true;
        }
        /* Stop early when a newer sound is waiting: it replaces this one. */
        for (size_t i = 0; i < p.n && !uxQueueMessagesWaiting(s_q);) {
            size_t k = p.n - i < CHUNK_SAMPLES ? p.n - i : CHUNK_SAMPLES, w = 0;
            if (i2s_channel_write(s_tx, p.pcm + i, k * sizeof *p.pcm, &w, pdMS_TO_TICKS(200)) != ESP_OK) break;
            i += w / sizeof *p.pcm;
        }
    }
}

extern "C" void hal_audio_play(const int16_t *pcm, size_t n)
{
    if (!pcm || !n) return;
    if (!s_q) {
        lock_create();
        s_q = xQueueCreate(1, sizeof(play_t));
        if (!s_q || xTaskCreatePinnedToCore(speaker_task, "spk", 3072, NULL, 5, NULL, 1) != pdPASS) {
            ESP_LOGW(TAG, "no memory for the speaker task");
            if (s_q) vQueueDelete(s_q);
            s_q = NULL;
            return;
        }
    }
    play_t p = {pcm, n};
    xQueueOverwrite(s_q, &p);
}

extern "C" void *hal_audio_alloc(size_t bytes)
{
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
