// Internal glue shared by the StopWatch board sources. Not part of hal.h.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "driver/i2s_types.h"
#include "sdkconfig.h"
#include "sw_power.h"
#include "hal.h"

/* ───────────── pins (docs/StopWatch.md) ───────────── */
#define SW_PIN_I2C_SDA 47
#define SW_PIN_I2C_SCL 48
#define SW_PIN_TOUCH_INT 13
#define SW_PIN_BTN_A 2 /* KEYA, yellow */
#define SW_PIN_BTN_B 1 /* KEYB, blue */

/* SCL for the CST820 and RX8130CE (both rated to 400 kHz; 2.2k pull-ups on SYS_SDA/SCL). */
#ifdef CONFIG_AGENTLINK_I2C_FAST
#define SW_I2C_DEV_HZ 400000
#else
#define SW_I2C_DEV_HZ 100000
#endif

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
bool sw_imu_init(void); /* imu.cpp: BMI270 chip ID check, then the sampling task starts */
void sw_input_init(void);

/* Link transports, dispatched by link.cpp. Each calls the hal callbacks with its own kind. */
void sw_usb_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state); /* link_usb.cpp */
bool sw_usb_link_send(const uint8_t *frame, size_t len);
void sw_ble_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state); /* link_ble.cpp */
/* link_ble.cpp: per port (HAL_LINK_BLE, HAL_LINK_BLE2: one per central); other ports are ignored. */
void sw_ble_link_interactive(hal_link_kind_t port, bool on);
void sw_ble_link_bulk(hal_link_kind_t port, bool on);
bool sw_ble_link_send(hal_link_kind_t port, const uint8_t *frame, size_t len);
/* link_ble.cpp: the shortest connection interval in force (us, 0 = no central), and the Battery Level (0x2A19) value;
 * a change is notified to a subscribed central. Safe to call from the core task. */
uint32_t sw_ble_link_interval_us(void);
void sw_ble_battery_level(uint8_t percent);

/* Codec and I2S0 shared by the mic and the speaker (audio.cpp); reference counted. */
bool sw_audio_on(void);
void sw_audio_off(void);
i2s_chan_handle_t sw_audio_rx(void);
const char *sw_audio_error(void);
bool sw_mic_active(void); /* mic.cpp: dictation is recording */

#ifdef __cplusplus
}
#endif
