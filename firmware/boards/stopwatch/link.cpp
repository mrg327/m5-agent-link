// hal_link_* dispatcher: fans out to the USB and BLE transports.
#include "board_priv.h"
#include "hal.h"

void hal_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state)
{
    sw_usb_link_start(on_rx, on_state);
    sw_ble_link_start(on_rx, on_state);
}

void hal_link_interactive(bool on) { sw_ble_link_interactive(on); }
void hal_link_bulk(bool on) { sw_ble_link_bulk(on); }
uint32_t hal_link_ble_interval_us(void) { return sw_ble_link_interval_us(); }

bool hal_link_send(hal_link_kind_t kind, const uint8_t *frame, size_t len)
{
    switch (kind) {
    case HAL_LINK_USB: return sw_usb_link_send(frame, len);
    case HAL_LINK_BLE: return sw_ble_link_send(frame, len);
    default: return false;
    }
}
