// CST820B capacitive touch (I2C 0x15, INT on G13, reset via IOE1 PYG4).
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_priv.h"

static const char *TAG = "sw-touch";

#define CST820_ADDR 0x15
#define REG_STATUS 0x00
#define REG_CHIP_ID 0xA7
#define REG_SOFT_VER 0xA9
#define REG_SLEEP 0xE5

static i2c_master_dev_handle_t s_dev;
static bool s_asleep;

static bool rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 50) == ESP_OK;
}

void sw_touch_reset(void)
{
    sw_ioe_write(SW_IOE_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    sw_ioe_write(SW_IOE_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(50));
}

bool sw_touch_init(void)
{
    sw_touch_reset();
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = CST820_ADDR;
    cfg.scl_speed_hz = 100000;
    if (!s_dev && i2c_master_bus_add_device(sw_i2c_bus(), &cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "add device failed");
        s_dev = nullptr;
        return false;
    }
    uint8_t id = 0, ver = 0;
    if (!rd(REG_CHIP_ID, &id, 1) || !rd(REG_SOFT_VER, &ver, 1) || id == 0 || ver == 0) {
        ESP_LOGE(TAG, "CST820 not responding");
        return false;
    }
    ESP_LOGI(TAG, "CST820 chip 0x%02x ver 0x%02x", id, ver);
    return true;
}

bool sw_touch_read(uint16_t *x, uint16_t *y)
{
    uint8_t b[7];
    if (!s_dev || !rd(REG_STATUS, b, sizeof(b))) {
        return false;
    }
    uint8_t event = (b[3] & 0xC0) >> 6; /* 0 down, 1 up, 2 contact */
    *x = (uint16_t)(((b[3] & 0x0F) << 8) | b[4]);
    *y = (uint16_t)(((b[5] & 0x0F) << 8) | b[6]);
    return b[2] > 0 && (event == 0 || event == 2);
}

void sw_touch_sleep(bool sleep)
{
    if (!s_dev) {
        return;
    }
    if (sleep == s_asleep) {
        return;
    }
    s_asleep = sleep;
    if (sleep) {
        uint8_t cmd[2] = {REG_SLEEP, 0x03}; /* deep sleep: no touch reports until a reset */
        i2c_master_transmit(s_dev, cmd, sizeof(cmd), 50);
    } else {
        sw_touch_reset(); /* the controller wakes on a reset pulse */
    }
}
