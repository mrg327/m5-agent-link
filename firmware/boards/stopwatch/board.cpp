// M5Stack StopWatch (C152) board support: power rails, I2C bus, PM1/IOE1, battery,
// power states and identity. Links + debug channel live in link*.cpp.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "sw_power.h"

#include "board_priv.h"
#include "hal.h"

static const char *TAG = "sw-board";

static i2c_master_bus_handle_t s_bus;
static hal_board_info_t s_info;
static char s_fw_version[32];

static hal_power_state_t s_power_state = HAL_POWER_ACTIVE;

/* Kept in RTC slow memory across deep sleep; the bootloader zeroes it on a power-on or reset. */
static RTC_DATA_ATTR uint32_t s_retained;
uint32_t hal_retained_get(void) { return s_retained; }
void hal_retained_set(uint32_t v) { s_retained = v; }

hal_wake_t hal_wake_reason(void)
{
    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT1: return HAL_WAKE_BUTTON;
    case ESP_SLEEP_WAKEUP_TIMER: return HAL_WAKE_TIMER;
    default: return HAL_WAKE_POWER_ON;
    }
}

/* ESP32-S3 deep sleep: only the RTC domain, PM1, IOE1 and the (sleeping) panel/touch rail stay up.
 * Either button (G1/G2, active low, both RTC IOs) or a touch (CST820 INT on G13) wakes it through
 * EXT1, as does the optional timer; a wake is a reboot. The PM1 power
 * button (single click = reset) is the recovery path if anything here goes wrong. */
static bool deep_sleep(uint32_t wake_after_s)
{
    const gpio_num_t pins[2] = {(gpio_num_t)SW_PIN_BTN_A, (gpio_num_t)SW_PIN_BTN_B};
    for (gpio_num_t p : pins) {
        if (gpio_get_level(p) == 0) {
            return false; /* a held button would wake us at once and its release would be lost */
        }
    }
    /* Tap to wake: the CST820 pulls INT (G13, an RTC IO) low while touched. If it is low now (a
     * finger, or a line that idles low), leave it out rather than wake straight back up. */
    uint64_t mask = (1ULL << SW_PIN_BTN_A) | (1ULL << SW_PIN_BTN_B);
    if (gpio_get_level((gpio_num_t)SW_PIN_TOUCH_INT) == 1) {
        mask |= 1ULL << SW_PIN_TOUCH_INT;
    }
    hal_haptic_play(NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    sw_ioe_motor_duty(0); /* IOE1 stays powered: a motor left on would run all night */
    sw_pm1_led(false);
    for (gpio_num_t p : pins) {
        rtc_gpio_pullup_en(p);
        rtc_gpio_pulldown_dis(p);
    }
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON); /* keeps the pull-ups */
    if (esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK) {
        return false;
    }
    if (wake_after_s) {
        esp_sleep_enable_timer_wakeup((uint64_t)wake_after_s * 1000000ULL);
    }
    char line[80];
    snprintf(line, sizeof(line), "{\"cat\":\"power\",\"ev\":\"deep_sleep\",\"wake_after_s\":%u}",
             (unsigned)wake_after_s);
    hal_debug_line(line);
    vTaskDelay(pdMS_TO_TICKS(50)); /* let the line drain */
    esp_deep_sleep_start();
    return true; /* not reached */
}

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
    s_info.has_mic = true; /* ES8311 + MEMS mic, mic.cpp */
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
    sw_power_init(s_bus);
    vTaskDelay(pdMS_TO_TICKS(50));
    sw_rtc_init();
    sw_input_init();
    /* Display and touch are created by hal_display_create / hal_touch_create. */
    ESP_LOGI(TAG, "board init done (ioe=%d)", sw_ioe_ok());
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

hal_power_state_t hal_power_enter(hal_power_state_t state, uint32_t wake_after_s)
{
    (void)wake_after_s;
    /* Guard: nothing below may take USB Serial/JTAG away while a host is attached. */
    if (state == HAL_POWER_DEEP && (usb_serial_jtag_is_connected() || hal_battery_read().usb_present)) {
        state = HAL_POWER_STANDBY;
    }
    switch (state) {
    case HAL_POWER_ACTIVE:
        hal_display_set_power(true);
        sw_touch_sleep(false);
        break;
    case HAL_POWER_STANDBY:
        /* TODO(power): auto light-sleep with BLE kept connected; wake on buttons / G12.
         * Touch stays in its normal (self-dozing) mode so a tap wakes the display. */
        hal_display_set_power(false);
        break;
    case HAL_POWER_DEEP:
        /* Only reached on battery with no host (guard above), so losing USB Serial/JTAG is fine.
         * Touch stays awake: its INT line (G13) is a deep-sleep wake source, so a tap wakes too. */
        hal_display_set_power(false);
        if (!deep_sleep(wake_after_s)) {
            state = HAL_POWER_STANDBY; /* a button is held: its release is input and wakes the display */
        }
        break;
    }
    s_power_state = state;
    char line[64];
    snprintf(line, sizeof(line), "{\"cat\":\"power\",\"ev\":\"state\",\"v\":%d}", (int)state);
    hal_debug_line(line);
    return state;
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

    int64_t now = esp_timer_get_time();
    if (last_us == 0 || now - last_us > 2000000) {
        uint16_t vbat = 0, vin = 0;
        bool chg_active = false;
        if (sw_pm1_read(&vbat, &vin, &chg_active)) {
            if (vbat > 0) {
                filtered_mv = filtered_mv ? (uint16_t)((filtered_mv * 7U + vbat + 4U) / 8U) : vbat;
                cached.percent = mv_to_percent(filtered_mv);
            }
            cached.usb_present = vin > 4000;
            cached.charging = cached.usb_present && chg_active;
        }
        last_us = now;
    }
    return cached;
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
