// Microphone: ES8311 codec (I2C 0x18 on the system bus) + MEMS mic, I2S at 16 kHz mono (issue 0ea9).
// The ESP32-S3 is the I2S master and drives MCLK at 256 x fs (4.096 MHz); the codec is a slave and
// clocks its ADC from MCLK. The audio rail (IOE1 PYG3, PYB_AU_EN) powers both the codec and the mic.
// Register values follow Espressif's es8311 driver (esp_codec_dev) for an analog mic, ADC only.
// A capture task moves DMA blocks into a 1 s stream buffer that hal_mic_read() drains without
// blocking, so a slow UI frame never loses audio.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board_priv.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

static const char *TAG = "mic";

#define ES8311_ADDR 0x18
#define PIN_MCLK GPIO_NUM_18
#define PIN_BCLK GPIO_NUM_17
#define PIN_DIN GPIO_NUM_16 /* codec ASDOUT -> ESP */
#define PIN_WS GPIO_NUM_15
#define PIN_DOUT GPIO_NUM_21 /* ESP -> codec DSDIN (speaker; unused here) */
#define BLOCK_SAMPLES 320    /* 20 ms */
#define BUFFER_BYTES (HAL_MIC_RATE * 2) /* 1 s */

static i2c_master_dev_handle_t s_codec;
static i2s_chan_handle_t s_rx;
static StreamBufferHandle_t s_buf;
static TaskHandle_t s_task;
static volatile bool s_running;
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

extern "C" const char *hal_mic_error(void) { return s_err; }

static bool wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    esp_err_t e = i2c_master_transmit(s_codec, b, 2, 50);
    if (e != ESP_OK) snprintf(s_err, sizeof s_err, "codec reg 0x%02x: %s", reg, esp_err_to_name(e));
    return e == ESP_OK;
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
        {0x13, 0x10},
        {0x1B, 0x0A}, {0x1C, 0x6A}, /* ADC HPF and EQ bypass */
        {0x0A, 0x0C}, /* serial data out (ADC): I2S, 16-bit */
        {0x09, 0x0C}, /* serial data in (DAC): I2S, 16-bit */
        {0x17, 0xBF}, /* ADC volume 0 dB */
        {0x0E, 0x02}, /* power up the PGA and ADC modulator */
        {0x12, 0x02}, /* DAC stays powered down */
        {0x14, 0x1A}, /* analog mic on MIC1P/N, PGA +30 dB */
        {0x0D, 0x01}, /* power up the analog circuits */
        {0x15, 0x40}, /* ADC ramp rate */
        {0x37, 0x08}, /* DAC EQ bypass */
    };
    for (size_t i = 0; i < sizeof seq / sizeof *seq; i++)
        if (!wr(seq[i][0], seq[i][1])) {
            ESP_LOGW(TAG, "%s", s_err);
            return false;
        }
    return true;
}

static bool i2s_init(void)
{
    if (s_rx) return true;
    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ch.dma_desc_num = 6;
    ch.dma_frame_num = BLOCK_SAMPLES;
    esp_err_t e = i2s_new_channel(&ch, NULL, &s_rx);
    if (e != ESP_OK) return FAIL("i2s channel: %s", esp_err_to_name(e));
    i2s_std_config_t std = {};
    std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(HAL_MIC_RATE);
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    std.gpio_cfg.mclk = PIN_MCLK;
    std.gpio_cfg.bclk = PIN_BCLK;
    std.gpio_cfg.ws = PIN_WS;
    std.gpio_cfg.dout = PIN_DOUT;
    std.gpio_cfg.din = PIN_DIN;
    e = i2s_channel_init_std_mode(s_rx, &std);
    if (e != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return FAIL("i2s std mode: %s", esp_err_to_name(e));
    }
    return true;
}

static void capture_task(void *)
{
    static int16_t block[BLOCK_SAMPLES];
    while (true) {
        if (!s_running) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        size_t got = 0;
        if (i2s_channel_read(s_rx, block, sizeof block, &got, pdMS_TO_TICKS(100)) == ESP_OK && got)
            xStreamBufferSend(s_buf, block, got, 0); /* full: drop the newest rather than block */
    }
}

static void hal_mic_stop_hw(void)
{
    i2s_channel_disable(s_rx);
    sw_ioe_write(SW_IOE_AUDIO_EN, false);
}

extern "C" bool hal_mic_start(void)
{
    if (s_running) return true;
    s_err[0] = 0;
    /* The 1 s buffer lives in PSRAM: xStreamBufferCreate() would take it from internal RAM,
       which BLE and LVGL leave too fragmented for 32 KB. */
    if (!s_buf) {
        static StaticStreamBuffer_t ctl;
        uint8_t *mem = (uint8_t *)heap_caps_malloc(BUFFER_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!mem) return FAIL("no PSRAM for the buffer");
        s_buf = xStreamBufferCreateStatic(BUFFER_BYTES, 2, mem, &ctl);
    }
    sw_ioe_write(SW_IOE_AUDIO_EN, true);
    vTaskDelay(pdMS_TO_TICKS(20)); /* rail and codec settle */
    if (!i2s_init()) {
        sw_ioe_write(SW_IOE_AUDIO_EN, false);
        return false;
    }
    /* MCLK must run before the codec's clock manager is configured */
    esp_err_t e = i2s_channel_enable(s_rx);
    if (e != ESP_OK) {
        sw_ioe_write(SW_IOE_AUDIO_EN, false);
        return FAIL("i2s enable: %s", esp_err_to_name(e));
    }
    if (!codec_init()) {
        hal_mic_stop_hw();
        return false;
    }
    xStreamBufferReset(s_buf);
    s_running = true;
    if (!s_task && xTaskCreatePinnedToCore(capture_task, "mic", 4096, NULL, 6, &s_task, 1) != pdPASS) {
        s_task = NULL;
        s_running = false;
        hal_mic_stop_hw();
        return FAIL("no memory for the capture task (internal free %u)",
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }
    xTaskNotifyGive(s_task);
    ESP_LOGI(TAG, "capture on");
    return true;
}

extern "C" size_t hal_mic_read(int16_t *pcm, size_t max)
{
    if (!s_buf) return 0;
    return xStreamBufferReceive(s_buf, pcm, max * sizeof *pcm, 0) / sizeof *pcm;
}

extern "C" void hal_mic_stop(void)
{
    if (!s_running) return;
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(110)); /* let a read in flight finish */
    i2s_channel_disable(s_rx);
    wr(0x0D, 0xFC); /* analog circuits off */
    wr(0x0E, 0x6A); /* PGA and ADC off */
    sw_ioe_write(SW_IOE_AUDIO_EN, false);
    ESP_LOGI(TAG, "capture off");
}
