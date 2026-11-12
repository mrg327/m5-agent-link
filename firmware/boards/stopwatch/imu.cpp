// BMI270 six-axis IMU (I2C 0x68 on the system bus). Only the accelerometer runs, in the sensor's
// low-power mode at 25 Hz; a small task reads it 10 times a second and hands gravity samples to core
// (core/motion.c decides what they mean). The sensor's interrupt is not on an ESP32 pin: it goes
// to PM1 GPIO0 (shared with the RTC) and PM1 raises G12, so reading is by polling instead. The feature
// engine (wrist gestures, any-motion) is loaded but not used: its config file is uploaded so a later
// change can switch a feature on without touching the bring-up.
//
// Power (BMI270 datasheet, not measured here): accelerometer-only low-power mode at 25 Hz is in the
// region of 14 uA; the gyroscope stays off. The ESP32 side costs one 6-byte I2C read per 100 ms.
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_priv.h"

extern "C" {
extern const uint8_t bmi270_config_file[];
extern const size_t bmi270_config_file_len;
}

static const char *TAG = "sw-imu";

#define BMI_ADDR 0x68
#define BMI_CHIP_ID 0x24
#define REG_CHIP_ID 0x00
#define REG_ERR 0x02
#define REG_ACC_X 0x0C
#define REG_INTERNAL_STATUS 0x21
#define REG_ACC_CONF 0x40
#define REG_ACC_RANGE 0x41
#define REG_INIT_CTRL 0x59
#define REG_INIT_ADDR_0 0x5B
#define REG_INIT_DATA 0x5E
#define REG_PWR_CONF 0x7C
#define REG_PWR_CTRL 0x7D
#define REG_CMD 0x7E
#define CMD_SOFT_RESET 0xB6
#define UPLOAD_CHUNK 32
#define POLL_MS 100
#define ACC_LSB_PER_G 8192 /* +-4 g */
#define MAX_FAILS 50

static i2c_master_dev_handle_t s_dev;
static volatile hal_motion_cb_t s_cb;
static volatile bool s_stream;
static TaskHandle_t s_task;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, sizeof b, 50);
}

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 50);
}

static esp_err_t wr_burst(uint8_t reg, const uint8_t *data, size_t n)
{
    uint8_t b[1 + UPLOAD_CHUNK];
    b[0] = reg;
    memcpy(b + 1, data, n);
    return i2c_master_transmit(s_dev, b, n + 1, 100);
}

/* Datasheet 4.4: power save off, INIT_CTRL 0, burst the file to INIT_DATA (the word address first), INIT_CTRL 1,
 * then INTERNAL_STATUS reports init_ok (message 1) within 20 ms. */
static bool upload_config(void)
{
    if (wr(REG_PWR_CONF, 0x00) != ESP_OK) return false;
    esp_rom_delay_us(450);
    if (wr(REG_INIT_CTRL, 0x00) != ESP_OK) return false;
    for (size_t off = 0; off < bmi270_config_file_len; off += UPLOAD_CHUNK) {
        uint16_t word = (uint16_t)(off / 2);
        uint8_t addr[2] = {(uint8_t)(word & 0x0F), (uint8_t)(word >> 4)};
        size_t n = bmi270_config_file_len - off < UPLOAD_CHUNK ? bmi270_config_file_len - off : UPLOAD_CHUNK;
        if (wr_burst(REG_INIT_ADDR_0, addr, 2) != ESP_OK || wr_burst(REG_INIT_DATA, bmi270_config_file + off, n) != ESP_OK) {
            return false;
        }
    }
    if (wr(REG_INIT_CTRL, 0x01) != ESP_OK) return false;
    uint8_t st = 0;
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        if (rd(REG_INTERNAL_STATUS, &st, 1) == ESP_OK && (st & 0x0F) == 0x01) return true;
    }
    ESP_LOGE(TAG, "config upload: INTERNAL_STATUS 0x%02x, not init_ok", st);
    return false;
}

/* The chip sits rotated on the board: its X and Y are the screen's Y and X (the factory firmware swaps them
 * too). Signs: z is checked on the bench (face up reads +1 g); the x and y signs are the datasheet's and not
 * yet checked against a moved watch, so core keeps a sign switch for the orientation axis. */
static bool read_mg(int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t b[6];
    if (rd(REG_ACC_X, b, sizeof b) != ESP_OK) return false;
    int16_t raw[3];
    for (int i = 0; i < 3; i++) raw[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
    *x = (int16_t)((int32_t)raw[1] * 1000 / ACC_LSB_PER_G);
    *y = (int16_t)((int32_t)raw[0] * 1000 / ACC_LSB_PER_G);
    *z = (int16_t)((int32_t)raw[2] * 1000 / ACC_LSB_PER_G);
    return true;
}

static bool bring_up(void)
{
    uint8_t id = 0;
    if (wr(REG_CMD, CMD_SOFT_RESET) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    /* the first transaction after a reset may be NACKed and leaves the master "busy": reset the bus and retry */
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 5 && err != ESP_OK; i++) {
        err = rd(REG_CHIP_ID, &id, 1);
        if (err != ESP_OK) {
            i2c_master_bus_reset(sw_i2c_bus());
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    if (err != ESP_OK) return false;
    int64_t t0 = esp_timer_get_time();
    if (!upload_config()) return false;
    ESP_LOGI(TAG, "config file %u bytes uploaded in %d ms", (unsigned)bmi270_config_file_len,
             (int)((esp_timer_get_time() - t0) / 1000));
    /* accelerometer only; low-power filter, average of 4, 25 Hz; +-4 g; then the sensor may save power between samples */
    if (wr(REG_ACC_CONF, 0x26) != ESP_OK || wr(REG_ACC_RANGE, 0x01) != ESP_OK || wr(REG_PWR_CTRL, 0x04) != ESP_OK) {
        return false;
    }
    if (wr(REG_PWR_CONF, 0x03) != ESP_OK) return false; /* adv_power_save + fifo_self_wakeup */
    vTaskDelay(pdMS_TO_TICKS(200));                    /* a few samples at 25 Hz */
    uint8_t conf = 0, e = 0xFF;
    rd(REG_ACC_CONF, &conf, 1);
    rd(REG_ERR, &e, 1);
    ESP_LOGI(TAG, "acc_conf 0x%02x err_reg 0x%02x", conf, e);
    return conf == 0x26 && e == 0;
}

static void imu_task(void *)
{
    bool ok = bring_up();
    int16_t x = 0, y = 0, z = 0;
    if (ok && read_mg(&x, &y, &z)) {
        int32_t g2 = (int32_t)x * x + (int32_t)y * y + (int32_t)z * z;
        int g = 0;
        while ((g + 1) * (g + 1) <= g2) g++;
        ESP_LOGI(TAG, "gravity x %d y %d z %d mg, |g| %d mg", x, y, z, g);
    } else {
        ESP_LOGE(TAG, "bring-up failed: no motion events");
        vTaskDelete(NULL);
    }
    int fails = 0;
    TickType_t t = xTaskGetTickCount();
    for (;;) {
        if (!s_stream) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            t = xTaskGetTickCount();
        }
        vTaskDelayUntil(&t, pdMS_TO_TICKS(POLL_MS));
        if (!s_stream) continue;
        if (!read_mg(&x, &y, &z)) {
            if (++fails == MAX_FAILS) {
                ESP_LOGW(TAG, "I2C reads keep failing: motion samples stop");
                s_stream = false;
            }
            continue;
        }
        fails = 0;
        hal_motion_cb_t cb = s_cb;
        if (cb) {
            hal_motion_evt_t e = {};
            e.kind = HAL_MOTION_TILT;
            e.x_mg = x;
            e.y_mg = y;
            e.z_mg = z;
            e.t_us = (uint64_t)esp_timer_get_time();
            cb(&e);
        }
    }
}

// Adds the device and checks the chip ID (one short read); the rest runs in the task. Returns whether a BMI270 answered.
bool sw_imu_init(void)
{
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = BMI_ADDR;
    cfg.scl_speed_hz = SW_I2C_DEV_HZ;
    if (!s_dev && i2c_master_bus_add_device(sw_i2c_bus(), &cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "add device failed");
        s_dev = nullptr;
        return false;
    }
    uint8_t id = 0;
    if (rd(REG_CHIP_ID, &id, 1) != ESP_OK || id != BMI_CHIP_ID) {
        ESP_LOGW(TAG, "no BMI270 at 0x%02x (id 0x%02x)", BMI_ADDR, id);
        return false;
    }
    ESP_LOGI(TAG, "BMI270 chip id 0x%02x", id);
    if (xTaskCreate(imu_task, "imu", 3072, nullptr, 2, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the IMU task");
        return false;
    }
    return true;
}

void hal_motion_set_callback(hal_motion_cb_t cb)
{
    s_cb = cb;
}

void hal_motion_stream_tilt(bool enable)
{
    s_stream = enable;
    if (enable && s_task) xTaskNotifyGive(s_task);
}
