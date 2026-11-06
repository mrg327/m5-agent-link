// USB Serial/JTAG link (PROTOCOL §1: COBS(payload ‖ crc16) + 0x00) and the '#' debug channel,
// which share the same USB CDC stream.
#include <stdio.h>

#include "board_priv.h"
#include "hal.h"

void sw_usb_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state)
{
    (void)on_rx;
    (void)on_state; /* TODO(usb) */
}

bool sw_usb_link_send(const uint8_t *frame, size_t len)
{
    (void)frame;
    (void)len;
    return false;
}

void hal_debug_line(const char *json)
{
    /* Console is USB Serial/JTAG; a single printf keeps the line atomic. */
    printf("#%s\n", json);
}
