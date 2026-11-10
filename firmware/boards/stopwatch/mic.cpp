// Microphone: ES8311 codec (I2C 0x18 on the system bus) + MEMS mic, I2S at 16 kHz mono (issue 0ea9).
// The ESP32-S3 is the I2S master and drives MCLK at 256 x fs (4.096 MHz); the codec is a slave and
// clocks its ADC from MCLK. The audio rail (IOE1 PYG3, PYB_AU_EN) powers both the codec and the mic.
// Register values follow Espressif's es8311 driver (esp_codec_dev) for an analog mic, ADC only.
// A capture task moves DMA blocks into a 1 s stream buffer that hal_mic_read() drains without
// blocking, so a slow UI frame never loses audio.

#include <string.h>

#include "board_priv.h"
#include "driver/i2s_std.h"
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

static bool wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_codec, b, 2, 50) == ESP_OK;
}

static bool codec_init(void)
{
    if (!s_codec) {
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = ES8311_ADDR;
        cfg.scl_speed_hz = SW_I2C_DEV_HZ;
        if (i2c_master_bus_add_device(sw_i2c_bus(), &cfg, &s_codec) != ESP_OK) return false;
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
            ESP_LOGW(TAG, "codec write 0x%02x failed", seq[i][0]);
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
    if (i2s_new_channel(&ch, NULL, &s_rx) != ESP_OK) return false;
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
    if (i2s_channel_init_std_mode(s_rx, &std) != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return false;
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

extern "C" bool hal_mic_start(void)
{
    if (s_running) return true;
    sw_ioe_write(SW_IOE_AUDIO_EN, true);
    vTaskDelay(pdMS_TO_TICKS(20)); /* rail and codec settle */
    if (!i2s_init()) {
        ESP_LOGW(TAG, "i2s init failed");
        sw_ioe_write(SW_IOE_AUDIO_EN, false);
        return false;
    }
    /* MCLK must run before the codec's clock manager is configured */
    if (i2s_channel_enable(s_rx) != ESP_OK || !codec_init()) {
        ESP_LOGW(TAG, "codec did not answer");
        i2s_channel_disable(s_rx);
        sw_ioe_write(SW_IOE_AUDIO_EN, false);
        return false;
    }
    if (!s_buf) s_buf = xStreamBufferCreate(BUFFER_BYTES, 2);
    xStreamBufferReset(s_buf);
    s_running = true;
    if (!s_task) xTaskCreatePinnedToCore(capture_task, "mic", 3072, NULL, 6, &s_task, 1);
    else xTaskNotifyGive(s_task);
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
