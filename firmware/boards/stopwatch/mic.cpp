// Microphone: the MEMS mic through the ES8311 ADC, I2S at 16 kHz mono (issue 0ea9). The codec,
// the I2S channels and the audio rail are shared with the speaker (audio.cpp).
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

#define BLOCK_SAMPLES 320    /* 20 ms */
#define BUFFER_BYTES (HAL_MIC_RATE * 2) /* 1 s */

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

static void capture_task(void *)
{
    static int16_t block[BLOCK_SAMPLES];
    while (true) {
        if (!s_running) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        size_t got = 0;
        if (i2s_channel_read(sw_audio_rx(), block, sizeof block, &got, pdMS_TO_TICKS(100)) == ESP_OK && got)
            xStreamBufferSend(s_buf, block, got, 0); /* full: drop the newest rather than block */
    }
}

extern "C" bool sw_mic_active(void) { return s_running; }

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
    if (!sw_audio_on()) return FAIL("%s", sw_audio_error());
    xStreamBufferReset(s_buf);
    s_running = true;
    if (!s_task && xTaskCreatePinnedToCore(capture_task, "mic", 4096, NULL, 6, &s_task, 1) != pdPASS) {
        s_task = NULL;
        s_running = false;
        sw_audio_off();
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
    sw_audio_off();
    ESP_LOGI(TAG, "capture off");
}
