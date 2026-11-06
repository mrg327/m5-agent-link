// NimBLE GATT peripheral link (PROTOCOL §1: rx/tx characteristics, 1-byte fragment header).
#include "board_priv.h"
#include "hal.h"

void sw_ble_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state)
{
    (void)on_rx;
    (void)on_state; /* TODO(ble) */
}

bool sw_ble_link_send(const uint8_t *frame, size_t len)
{
    (void)frame;
    (void)len;
    return false;
}
