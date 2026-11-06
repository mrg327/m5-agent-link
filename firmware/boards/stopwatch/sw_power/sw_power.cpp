// M5PM1 (power management) and M5IOE1 (IO expander) for the StopWatch.
//
// This lives in its own component on purpose: the M5PM1/M5IOE1 headers change their class
// layout depending on whether <M5GFX.h> is visible (see M5IOE1_i2c_compat.h, M5IOE1_HAS_I2C_BUS).
// The libraries are compiled without M5GFX on the include path, so code that instantiates them
// must be too, or `new M5IOE1()` allocates a smaller object than the library writes to (heap
// corruption on boot). Keep m5gfx out of this component's requirements.
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "M5IOE1.h"
#include "M5PM1.h"

#include "sw_power.h"

static_assert(SW_IOE_MOTOR_PWM == M5IOE1_PIN_9, "IOE pin map");
static_assert(SW_IOE_L3B_AMOLED == M5IOE1_PIN_8, "IOE pin map");
static_assert(SW_IOE_SPK_EN == M5IOE1_PIN_10, "IOE pin map");
static_assert(SW_IOE_TP_RST == M5IOE1_PIN_4, "IOE pin map");
static_assert(SW_IOE_OLED_RST == M5IOE1_PIN_5, "IOE pin map");
static_assert(SW_IOE_MUX == M5IOE1_PIN_1, "IOE pin map");
static_assert(SW_IOE_AUDIO_EN == M5IOE1_PIN_3, "IOE pin map");

#if __has_include(<M5GFX.h>)
#error "sw_power must not see M5GFX headers (M5PM1/M5IOE1 class layout would differ from the libraries)"
#endif

static const char *TAG = "sw-power";

static M5PM1 *s_pm1;
static M5IOE1 *s_ioe;
static SemaphoreHandle_t s_pm1_mtx;
static SemaphoreHandle_t s_ioe_mtx;

/* ───────────── M5PM1 power management IC (L0) ───────────── */

#define PMG2_CHG_STAT M5PM1_GPIO_NUM_2
#define PMG3_CHG_PROG M5PM1_GPIO_NUM_3

static void pm1_init(i2c_master_bus_handle_t s_bus)
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

static void ioe_init(i2c_master_bus_handle_t s_bus)
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

bool sw_ioe_motor_duty(uint8_t duty_percent)
{
    if (!s_ioe) {
        return false;
    }
    xSemaphoreTake(s_ioe_mtx, portMAX_DELAY);
    bool ok = s_ioe->setPwmDuty(SW_IOE_PWM_CH_MOTOR, duty_percent, false, true) == M5IOE1_OK;
    xSemaphoreGive(s_ioe_mtx);
    return ok;
}

bool sw_ioe_motor_readback(uint8_t *duty, bool *enabled)
{
    if (!s_ioe) {
        return false;
    }
    bool polarity = false;
    xSemaphoreTake(s_ioe_mtx, portMAX_DELAY);
    bool ok = s_ioe->getPwmDuty(SW_IOE_PWM_CH_MOTOR, duty, &polarity, enabled) == M5IOE1_OK;
    xSemaphoreGive(s_ioe_mtx);
    return ok;
}

void sw_power_init(i2c_master_bus_handle_t bus)
{
    pm1_init(bus);
    ioe_init(bus);
}

bool sw_pm1_read(uint16_t *vbat_mv, uint16_t *vin_mv, bool *charging)
{
    if (!s_pm1) {
        return false;
    }
    xSemaphoreTake(s_pm1_mtx, portMAX_DELAY);
    bool ok = s_pm1->readVbat(vbat_mv) == M5PM1_OK;
    ok = ok && s_pm1->readVin(vin_mv) == M5PM1_OK;
    uint8_t chg = 1;
    if (ok && s_pm1->gpioGetInput(PMG2_CHG_STAT, &chg) == M5PM1_OK) {
        *charging = chg == 0; /* CHG_STAT is active low */
    }
    xSemaphoreGive(s_pm1_mtx);
    return ok;
}
