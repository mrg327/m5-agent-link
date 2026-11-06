// Internal glue shared by the StopWatch board sources. Not part of hal.h.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"

/* ───────────── pins (docs/StopWatch.md) ───────────── */
#define SW_PIN_I2C_SDA 47
#define SW_PIN_I2C_SCL 48
#define SW_PIN_TOUCH_INT 13
#define SW_PIN_BTN_A 2 /* KEYA, yellow */
#define SW_PIN_BTN_B 1 /* KEYB, blue */
#define SW_PIN_SPK_PA 14

#define SW_PANEL_W 466
#define SW_PANEL_H 466

/* M5IOE1 pin numbers (PYGn) */
#define SW_IOE_MOTOR_PWM 9
#define SW_IOE_L3B_AMOLED 8
#define SW_IOE_SPK_EN 10
#define SW_IOE_TP_RST 4
#define SW_IOE_OLED_RST 5
#define SW_IOE_MUX 1
#define SW_IOE_AUDIO_EN 3
#define SW_IOE_PWM_CH_MOTOR 0

#ifdef __cplusplus
extern "C" {
#endif

i2c_master_bus_handle_t sw_i2c_bus(void);

/* IO expander helpers (serialised internally, safe from any task). */
void sw_ioe_write(uint8_t pin, bool level);
bool sw_ioe_ok(void);
void sw_ioe_motor_duty(uint8_t duty_percent);

/* PM1 green power LED (kept off while the AMOLED is lit). */
void sw_pm1_led(bool on);

/* Subsystem init, called from hal_board_init in this order. */
void sw_touch_reset(void);
bool sw_touch_init(void);
bool sw_touch_read(uint16_t *x, uint16_t *y);
void sw_touch_sleep(bool sleep);
bool sw_rtc_init(void);
void sw_input_init(void);

#ifdef __cplusplus
}
#endif
