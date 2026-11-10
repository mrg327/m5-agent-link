// CST820B capacitive touch (I2C 0x15, INT on G13, reset via IOE1 PYG4).
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
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

#ifdef CONFIG_AGENTLINK_TOUCH_INT
/* INT gating: with no finger down, a poll reads only after an INT edge or once per FORCE_US. */
#define TOUCH_FORCE_US 250000
#define TOUCH_INT_STRIKES 2
static volatile uint32_t s_int_edges;
static uint32_t s_int_seen;
static bool s_gate, s_down;
static uint8_t s_strikes;
static int64_t s_last_rd_us;

static void IRAM_ATTR int_isr(void *arg)
{
    (void)arg;
    s_int_edges = s_int_edges + 1;
}

static void int_init(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << SW_PIN_TOUCH_INT;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.intr_type = GPIO_INTR_ANYEDGE; /* the pulse polarity is configurable on the CST820: count both */
    esp_err_t err = gpio_config(&io);
    if (err == ESP_OK) {
        err = gpio_install_isr_service(0);
        err = err == ESP_ERR_INVALID_STATE ? ESP_OK : err; /* input.cpp installed it already */
    }
    if (err == ESP_OK) {
        err = gpio_isr_handler_add((gpio_num_t)SW_PIN_TOUCH_INT, int_isr, nullptr);
    }
    s_gate = err == ESP_OK;
    ESP_LOGI(TAG, "INT-gated polling %s", s_gate ? "on" : "off");
}
#endif

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
    cfg.scl_speed_hz = SW_I2C_DEV_HZ;
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
    ESP_LOGI(TAG, "CST820 chip 0x%02x ver 0x%02x (%d kHz)", id, ver, SW_I2C_DEV_HZ / 1000);
#ifdef CONFIG_AGENTLINK_TOUCH_INT
    int_init();
#endif
    return true;
}

bool sw_touch_read(uint16_t *x, uint16_t *y)
{
    uint8_t b[7];
    if (!s_dev) {
        return false;
    }
#ifdef CONFIG_AGENTLINK_TOUCH_INT
    uint32_t edges = s_int_edges;
    bool edge = edges != s_int_seen;
    int64_t now = esp_timer_get_time();
    if (s_gate && !s_down && !edge && now - s_last_rd_us < TOUCH_FORCE_US) {
        return false;
    }
    s_int_seen = edges;
    s_last_rd_us = now;
#endif
    bool down = false;
    if (rd(REG_STATUS, b, sizeof(b))) {
        uint8_t event = (b[3] & 0xC0) >> 6; /* 0 down, 1 up, 2 contact */
        *x = (uint16_t)(((b[3] & 0x0F) << 8) | b[4]);
        *y = (uint16_t)(((b[5] & 0x0F) << 8) | b[6]);
        down = b[2] > 0 && (event == 0 || event == 2);
    }
#ifdef CONFIG_AGENTLINK_TOUCH_INT
    /* A forced read that finds a new finger with no INT edge: the line is not doing its job. */
    if (s_gate && down && !s_down && !edge && ++s_strikes >= TOUCH_INT_STRIKES) {
        s_gate = false;
        ESP_LOGW(TAG, "touch without INT edge: INT-gated polling off");
    }
    s_down = down;
#endif
    return down;
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
