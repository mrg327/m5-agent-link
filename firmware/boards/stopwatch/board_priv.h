// Internal glue shared by the StopWatch board sources. Not part of hal.h.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "sw_power.h"
#include "hal.h"

/* ───────────── pins (docs/StopWatch.md) ───────────── */
#define SW_PIN_I2C_SDA 47
#define SW_PIN_I2C_SCL 48
#define SW_PIN_TOUCH_INT 13
#define SW_PIN_BTN_A 2 /* KEYA, yellow */
#define SW_PIN_BTN_B 1 /* KEYB, blue */

#define SW_PANEL_W 466
#define SW_PANEL_H 466

#ifdef __cplusplus
extern "C" {
#endif

i2c_master_bus_handle_t sw_i2c_bus(void);

/* Subsystem init, called from hal_board_init in this order. */
void sw_touch_reset(void);
bool sw_touch_init(void);
bool sw_touch_read(uint16_t *x, uint16_t *y);
void sw_touch_sleep(bool sleep);
bool sw_rtc_init(void);
void sw_input_init(void);

/* Link transports, dispatched by link.cpp. Each calls the hal callbacks with its own kind. */
void sw_usb_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state); /* link_usb.cpp */
bool sw_usb_link_send(const uint8_t *frame, size_t len);
void sw_ble_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state); /* link_ble.cpp */
bool sw_ble_link_send(const uint8_t *frame, size_t len);

#ifdef __cplusplus
}
#endif
