// M5Stack StopWatch (C152) board support: power rails, I2C bus, PM1/IOE1, battery,
// power states, identity, debug channel and the not-yet-implemented transports.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "M5IOE1.h"
#include "M5PM1.h"

#include "board_priv.h"
#include "hal.h"

static const char *TAG = "sw-board";

static i2c_master_bus_handle_t s_bus;
static M5PM1 *s_pm1;
static M5IOE1 *s_ioe;
static SemaphoreHandle_t s_pm1_mtx;
static SemaphoreHandle_t s_ioe_mtx;
static hal_board_info_t s_info;
static char s_fw_version[32];

static hal_power_state_t s_power_state = HAL_POWER_ACTIVE;

i2c_master_bus_handle_t sw_i2c_bus(void)
{
    return s_bus;
}

/* ───────────── I2C ───────────── */

static void i2c_init(void)
{
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = (gpio_num_t)SW_PIN_I2C_SDA;
    cfg.scl_io_num = (gpio_num_t)SW_PIN_I2C_SCL;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
}

/* ───────────── M5PM1 power management IC (L0) ───────────── */

#define PMG2_CHG_STAT M5PM1_GPIO_NUM_2
#define PMG3_CHG_PROG M5PM1_GPIO_NUM_3

static void pm1_init(void)
{
    s_pm1_mtx = xSemaphoreCreateMutex();
    s_pm1 = new M5PM1();
    if (s_pm1->begin(s_bus) != M5PM1_OK) {
        ESP_LOGE(TAG, "M5PM1 not found");
        delete s_pm1;
        s_pm1 = nullptr;
        return;
    }
    s_pm1->setI2cSleepTime(0);
    s_pm1->setI2cSleepTime(0);
    s_pm1->btnSetConfig(M5PM1_BTN_TYPE_CLICK, M5PM1_BTN_CLICK_DELAY_1000MS);
    s_pm1->wdtSet(0);
    s_pm1->ldoSetPowerHold(true); /* keep RTC powered after power off */
    s_pm1->setChargeEnable(true);
    s_pm1->gpioSet(PMG3_CHG_PROG, M5PM1_GPIO_MODE_OUTPUT, 0, M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);
    s_pm1->gpioSetFunc(PMG2_CHG_STAT, M5PM1_GPIO_FUNC_GPIO);
    s_pm1->gpioSetMode(PMG2_CHG_STAT, M5PM1_GPIO_MODE_INPUT);
    s_pm1->gpioSetPull(PMG2_CHG_STAT, M5PM1_GPIO_PULL_NONE);
    s_pm1->setSingleResetDisable(true);
    /* TODO(power): PMG1 -> G12 IRQ (IMU lift / RTC wake) once lift detection exists. */
}

void sw_pm1_led(bool on)
{
    if (!s_pm1) {
        return;
    }
    xSemaphoreTake(s_pm1_mtx, portMAX_DELAY);
    s_pm1->setLedEnLevel(on);
    xSemaphoreGive(s_pm1_mtx);
}

/* ───────────── M5IOE1 IO expander (L3B rails, resets, motor PWM) ───────────── */

static void ioe_init(void)
{
    s_ioe_mtx = xSemaphoreCreateMutex();
    s_ioe = new M5IOE1();
    m5ioe1_err_t ret = s_ioe->begin(s_bus, 0x4F, M5IOE1_I2C_FREQ_400K);
    if (ret != M5IOE1_OK) {
        ret = s_ioe->begin(s_bus, 0x6F, M5IOE1_I2C_FREQ_400K);
    }
    if (ret != M5IOE1_OK) {
        ESP_LOGE(TAG, "M5IOE1 not found");
        delete s_ioe;
        s_ioe = nullptr;
        return;
    }
    s_ioe->setI2cSleepTime(0);
    s_ioe->setI2cSleepTime(0);

    const uint8_t outs[] = {SW_IOE_MOTOR_PWM, SW_IOE_L3B_AMOLED, SW_IOE_SPK_EN, SW_IOE_TP_RST,
                            SW_IOE_OLED_RST,  SW_IOE_MUX,        SW_IOE_AUDIO_EN};
    for (uint8_t pin : outs) {
        s_ioe->pinMode(pin, OUTPUT);
    }
    /* Same order as the factory firmware: AMOLED rail, resets released, amp off, UART mux. */
    s_ioe->digitalWrite(SW_IOE_L3B_AMOLED, 1);
    s_ioe->digitalWrite(SW_IOE_TP_RST, 1);
    s_ioe->digitalWrite(SW_IOE_SPK_EN, 0);
    s_ioe->digitalWrite(SW_IOE_OLED_RST, 1);
    s_ioe->digitalWrite(SW_IOE_MUX, 0);
    s_ioe->digitalWrite(SW_IOE_AUDIO_EN, 0); /* TODO(audio): enable together with the ES8311 driver */
    s_ioe->setPwmFrequency(5000);
    s_ioe->setPwmDuty(SW_IOE_PWM_CH_MOTOR, 0, false, true);

    gpio_set_direction((gpio_num_t)SW_PIN_SPK_PA, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)SW_PIN_SPK_PA, 0);

    /* The factory firmware verifies the AMOLED rail really came up. */
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(80));
        if (s_ioe->digitalRead(SW_IOE_L3B_AMOLED) == 1) {
            break;
        }
        s_ioe->digitalWrite(SW_IOE_L3B_AMOLED, 1);
    }
}

void sw_ioe_write(uint8_t pin, bool level)
{
    if (!s_ioe) {
        return;
    }
    xSemaphoreTake(s_ioe_mtx, portMAX_DELAY);
    s_ioe->digitalWrite(pin, level ? 1 : 0);
    xSemaphoreGive(s_ioe_mtx);
}

bool sw_ioe_ok(void)
{
    return s_ioe != nullptr;
}

void sw_ioe_motor_duty(uint8_t duty_percent)
{
    if (!s_ioe) {
        return;
    }
    xSemaphoreTake(s_ioe_mtx, portMAX_DELAY);
    s_ioe->setPwmDuty(SW_IOE_PWM_CH_MOTOR, duty_percent, false, true);
    xSemaphoreGive(s_ioe_mtx);
}

/* ───────────── identity ───────────── */

static void info_init(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    snprintf(s_fw_version, sizeof(s_fw_version), "%s", desc->version[0] ? desc->version : "0.0.0");

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    memcpy(s_info.uid, mac, 6);
    s_info.uid[6] = 0x53; /* 'S' */
    s_info.uid[7] = 0x57; /* 'W' */

    s_info.board = "m5stopwatch";
    s_info.fw_version = s_fw_version;
    s_info.has_display = true;
    s_info.shape = HAL_SHAPE_ROUND;
    s_info.width = SW_PANEL_W;
    s_info.height = SW_PANEL_H;
    s_info.color = true;
    s_info.has_touch = true;
    s_info.has_imu = false;     /* TODO(imu): BMI270 driver */
    s_info.has_haptic = true;
    s_info.has_speaker = false; /* TODO(audio): ES8311 + AW8737A */
    s_info.has_mic = false;
    s_info.has_battery = true;
    s_info.num_buttons = 2;
}

void hal_board_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    info_init();
    /* Rail bring-up order follows M5StopWatch-UserDemo: I2C -> PM1 -> IOE1 -> (50 ms) -> display -> touch. */
    i2c_init();
    pm1_init();
    ioe_init();
    vTaskDelay(pdMS_TO_TICKS(50));
    sw_rtc_init();
    sw_input_init();
    /* Display and touch are created by hal_display_create / hal_touch_create. */
    ESP_LOGI(TAG, "board init done (pm1=%d ioe=%d)", s_pm1 != nullptr, s_ioe != nullptr);
}

const hal_board_info_t *hal_board_info(void)
{
    return &s_info;
}

/* ───────────── time ───────────── */

uint64_t hal_time_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

/* ───────────── power & battery ───────────── */

void hal_power_enter(hal_power_state_t state, uint32_t wake_after_s)
{
    (void)wake_after_s;
    switch (state) {
    case HAL_POWER_ACTIVE:
        hal_display_set_power(true);
        sw_touch_sleep(false);
        break;
    case HAL_POWER_STANDBY:
        /* TODO(power): auto light-sleep with BLE kept connected; wake on buttons / G12. */
        sw_touch_sleep(true);
        hal_display_set_power(false);
        break;
    case HAL_POWER_DEEP:
        /* TODO(power): L1-only deep state with RTC timer wake via PM1; behaves as STANDBY for now. */
        sw_touch_sleep(true);
        hal_display_set_power(false);
        break;
    }
    s_power_state = state;
    char line[64];
    snprintf(line, sizeof(line), "{\"cat\":\"power\",\"ev\":\"state\",\"v\":%d}", (int)state);
    hal_debug_line(line);
}

static uint8_t mv_to_percent(uint16_t mv)
{
    if (mv <= 3300) {
        return 0;
    }
    if (mv >= 4200) {
        return 100;
    }
    return (uint8_t)(((uint32_t)(mv - 3300) * 100U) / (4200 - 3300));
}

hal_battery_t hal_battery_read(void)
{
    static hal_battery_t cached = {0, false, false};
    static uint16_t filtered_mv;
    static int64_t last_us;

    hal_battery_t out = cached;
    if (!s_pm1) {
        return out;
    }
    xSemaphoreTake(s_pm1_mtx, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (last_us == 0 || now - last_us > 2000000) {
        uint16_t vbat = 0, vin = 0;
        uint8_t chg = 1;
        if (s_pm1->readVbat(&vbat) == M5PM1_OK && vbat > 0) {
            filtered_mv = filtered_mv ? (uint16_t)((filtered_mv * 7U + vbat + 4U) / 8U) : vbat;
            cached.percent = mv_to_percent(filtered_mv);
        }
        if (s_pm1->readVin(&vin) == M5PM1_OK) {
            cached.usb_present = vin > 4000;
            if (s_pm1->gpioGetInput(PMG2_CHG_STAT, &chg) == M5PM1_OK) {
                cached.charging = cached.usb_present && chg == 0;
            }
        }
        last_us = now;
    }
    out = cached;
    xSemaphoreGive(s_pm1_mtx);
    return out;
}

/* ───────────── link transports (TODO) ───────────── */

static hal_link_rx_cb_t s_link_rx;
static hal_link_state_cb_t s_link_state;

void hal_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state)
{
    /* TODO(link): NimBLE peripheral (HAL_LINK_BLE) and USB Serial/JTAG CDC with COBS (HAL_LINK_USB). */
    s_link_rx = on_rx;
    s_link_state = on_state;
}

bool hal_link_send(hal_link_kind_t kind, const uint8_t *frame, size_t len)
{
    (void)kind;
    (void)frame;
    (void)len;
    return false;
}

/* ───────────── debug channel ───────────── */

void hal_debug_line(const char *json)
{
    /* Console is USB Serial/JTAG; a single printf keeps the line atomic. */
    printf("#%s\n", json);
}

/* ───────────── sound & motion (TODO) ───────────── */

static hal_motion_cb_t s_motion_cb;

void hal_motion_set_callback(hal_motion_cb_t cb)
{
    /* TODO(imu): BMI270 lift/drop/shake via PM1 PMG0 -> G12; see FIRMWARE.md §4. */
    s_motion_cb = cb;
}

void hal_motion_stream_tilt(bool enable)
{
    (void)enable;
}

void hal_sound_play(hal_sound_t sound)
{
    /* TODO(audio): ES8311 over I2S + PYG3 audio rail + PYG10 amp. */
    (void)sound;
}
