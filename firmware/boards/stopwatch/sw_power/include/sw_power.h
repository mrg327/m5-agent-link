// StopWatch power management (M5PM1) and IO expander (M5IOE1). C API so that callers do not
// include the vendor headers; see sw_power.cpp for why this is a separate component.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"

#define SW_PIN_SPK_PA 14

/* M5IOE1 pin indices. The library API is 0-based: label IO<n> == M5IOE1_PIN_<n> == n-1.
 * sw_power.cpp static_asserts these against the library enum. */
#define SW_IOE_MOTOR_PWM 8  /* IO9  */
#define SW_IOE_L3B_AMOLED 7 /* IO8  */
#define SW_IOE_SPK_EN 9     /* IO10 */
#define SW_IOE_TP_RST 3     /* IO4  */
#define SW_IOE_OLED_RST 4   /* IO5  */
#define SW_IOE_MUX 0        /* IO1  */
#define SW_IOE_AUDIO_EN 2   /* IO3  */
#define SW_IOE_PWM_CH_MOTOR 0

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up PM1 then IOE1 (rails, resets, motor PWM) in the factory firmware's order. */
void sw_power_init(i2c_master_bus_handle_t bus);

/* IO expander helpers (serialised internally, safe from any task). */
void sw_ioe_write(uint8_t pin, bool level);
bool sw_ioe_ok(void);
/* False if the IOE1 is missing or the I2C write failed. */
bool sw_ioe_motor_duty(uint8_t duty_percent);
/* Reads the motor PWM channel back from the chip (duty in %). */
bool sw_ioe_motor_readback(uint8_t *duty, bool *enabled);

/* PM1 green power LED (kept off while the AMOLED is lit). */
void sw_pm1_led(bool on);
/* Battery and USB input voltages, and charger state. False if the PM1 is unavailable. */
bool sw_pm1_read(uint16_t *vbat_mv, uint16_t *vin_mv, bool *charging);

#ifdef __cplusplus
}
#endif
