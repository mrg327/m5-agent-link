// Buttons (KEYA G2 / KEYB G1, active low) and the vibration motor (IOE1 PYG9 PWM).
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board_priv.h"
#include "hal.h"

#define BTN_DEBOUNCE_MS 12
#define HAPTIC_MAX_STEPS 32
#define HAPTIC_DUTY 85      /* % PWM hold level; the motor does not start below ~25 */
#define HAPTIC_KICK_DUTY 100 /* overdrive to spin the ERM up quickly */
#define HAPTIC_KICK_MS 15    /* kick length; pulses at least twice as long get it first, shorter ones run at kick duty */

static const char *TAG = "haptic";

/* ───────────── buttons ───────────── */

static const gpio_num_t kBtnPin[2] = {(gpio_num_t)SW_PIN_BTN_A, (gpio_num_t)SW_PIN_BTN_B};
static TaskHandle_t s_btn_task;
static volatile hal_button_cb_t s_btn_cb;

void hal_buttons_set_callback(hal_button_cb_t cb)
{
    s_btn_cb = cb;
}

static void IRAM_ATTR btn_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_btn_task, &woken);
    portYIELD_FROM_ISR(woken);
}

static void btn_task(void *arg)
{
    (void)arg;
    bool reported_down[2] = {false, false};
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* Let contact bounce settle, then report only real state changes. */
        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));
        ulTaskNotifyTake(pdTRUE, 0);
        for (int i = 0; i < 2; i++) {
            bool down = gpio_get_level(kBtnPin[i]) == 0;
            if (down != reported_down[i]) {
                reported_down[i] = down;
                hal_button_cb_t cb = s_btn_cb;
                if (cb) {
                    cb((hal_button_t)i, down ? HAL_BTN_DOWN : HAL_BTN_UP, hal_time_us());
                }
            }
        }
    }
}

/* ───────────── haptics ───────────── */

static SemaphoreHandle_t s_hap_mtx;
static TaskHandle_t s_hap_task;
static uint16_t s_hap_pattern[HAPTIC_MAX_STEPS];
static size_t s_hap_count;

void hal_haptic_play(const uint16_t *on_off_ms, size_t count)
{
    if (!s_hap_task) {
        return;
    }
    if (count > HAPTIC_MAX_STEPS) {
        count = HAPTIC_MAX_STEPS;
    }
    xSemaphoreTake(s_hap_mtx, portMAX_DELAY);
    if (on_off_ms && count) {
        memcpy(s_hap_pattern, on_off_ms, count * sizeof(uint16_t));
    }
    s_hap_count = on_off_ms ? count : 0;
    xSemaphoreGive(s_hap_mtx);
    xTaskNotifyGive(s_hap_task); /* a new pattern (or count 0) replaces the running one */
}

static void haptic_task(void *arg)
{
    (void)arg;
    uint16_t local[HAPTIC_MAX_STEPS];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            bool logged_readback = false; /* once per pattern start */
            xSemaphoreTake(s_hap_mtx, portMAX_DELAY);
            size_t n = s_hap_count;
            memcpy(local, s_hap_pattern, n * sizeof(uint16_t));
            xSemaphoreGive(s_hap_mtx);

            ESP_LOGI(TAG, "pattern steps=%u first=%ums duty=%d/%d", (unsigned)n, n ? (unsigned)local[0] : 0u,
                     HAPTIC_KICK_DUTY, HAPTIC_DUTY);
            bool interrupted = false;
            for (size_t i = 0; i < n && !interrupted; i++) {
                bool on = (i % 2) == 0;
                uint32_t ms = local[i] ? local[i] : 1;
                /* A notification during a wait means a new pattern arrived. */
                if (on && ms >= 2 * HAPTIC_KICK_MS) {
                    bool ok = sw_ioe_motor_duty(HAPTIC_KICK_DUTY);
                    if (!logged_readback) {
                        logged_readback = true;
                        uint8_t duty = 0;
                        bool en = false;
                        bool rb = sw_ioe_motor_readback(&duty, &en);
                        ESP_LOGI(TAG, "motor set_ok=%d readback_ok=%d duty=%u en=%d", ok, rb, duty, en);
                    }
                    interrupted = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HAPTIC_KICK_MS)) > 0;
                    ms -= HAPTIC_KICK_MS;
                    if (!interrupted) sw_ioe_motor_duty(HAPTIC_DUTY);
                } else {
                    /* A pulse no longer than the kick (the rim detent) is all kick: a crisp click. */
                    sw_ioe_motor_duty(on ? (ms <= HAPTIC_KICK_MS ? HAPTIC_KICK_DUTY : HAPTIC_DUTY) : 0);
                }
                if (!interrupted) interrupted = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms)) > 0;
            }
            sw_ioe_motor_duty(0);
            if (!interrupted) {
                break;
            }
        }
    }
}

void sw_input_init(void)
{
    for (int i = 0; i < 2; i++) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << kBtnPin[i];
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        cfg.intr_type = GPIO_INTR_ANYEDGE;
        gpio_config(&cfg);
    }
    xTaskCreate(btn_task, "btn", 3072, nullptr, 6, &s_btn_task);
    gpio_install_isr_service(0);
    for (int i = 0; i < 2; i++) {
        gpio_isr_handler_add(kBtnPin[i], btn_isr, nullptr);
    }

    s_hap_mtx = xSemaphoreCreateMutex();
    if (sw_ioe_ok()) {
        xTaskCreate(haptic_task, "haptic", 3072, nullptr, 5, &s_hap_task);
    }
}
