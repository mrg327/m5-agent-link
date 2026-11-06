// USB Serial/JTAG link (PROTOCOL §1: 0x00 ‖ COBS(payload ‖ crc16_be) ‖ 0x00) and the '#' debug
// channel, which share the same USB CDC stream with the ESP-IDF console log.
//
// Interleaving: usb_serial_jtag_write_bytes() queues its whole buffer as one ring-buffer item,
// so a frame (one call) is never split by console characters. hal_debug_line() uses the same
// call; a mutex serialises both so the shared encode buffer is safe as well.
//
// Link state (reported through on_state):
//   up   = USB host attached (usb_serial_jtag_is_connected(), debounced). Core then sends hello
//          every 3 s until the daemon, once it opens the port, answers with welcome.
//   down = host detached, or no valid frame for SILENCE_MS after the daemon had been talking
//          (daemon gone/restarted). The state is then re-announced up, which re-triggers hello.
// The daemon pings every ~5 s, so a live link never trips the silence timeout.
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board_priv.h"
#include "hal.h"

#define FRAME_MAX 4096
#define CHUNK_MAX (FRAME_MAX + 2 + (FRAME_MAX + 2) / 254 + 2)
#define TX_RING 8192
#define RX_RING 8192
#define DETACH_DEBOUNCE_MS 1000
#define SILENCE_MS 12000
#define SEND_TIMEOUT_MS 200
/* Core's frame queue holds 8 frames and logs drops; a short gap keeps bursts from piling up. */
#define DELIVER_GAP_MS 2

static hal_link_rx_cb_t s_on_rx;
static hal_link_state_cb_t s_on_state;
static SemaphoreHandle_t s_tx_mux;
static volatile bool s_driver_ready;
static volatile bool s_up;
static uint8_t s_tx_buf[CHUNK_MAX + 2];

static uint16_t crc16_ccitt(const uint8_t *d, size_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*d++) << 8;
        for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* COBS-encode in[0..n) into out; returns encoded length (worst case n + n/254 + 1). */
static size_t cobs_encode(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t o = 1, code_at = 0;
    uint8_t code = 1;
    for (size_t i = 0; i < n; i++) {
        if (in[i] == 0) {
            out[code_at] = code;
            code_at = o++;
            code = 1;
        } else {
            out[o++] = in[i];
            if (++code == 0xFF) {
                out[code_at] = code;
                code_at = o++;
                code = 1;
            }
        }
    }
    out[code_at] = code;
    return o;
}

/* In-place COBS decode of buf[0..n); returns decoded length or -1 if malformed. */
static int cobs_decode(uint8_t *buf, size_t n)
{
    size_t r = 0, w = 0;
    while (r < n) {
        uint8_t code = buf[r];
        if (code == 0 || r + code > n) return -1;
        r++;
        for (uint8_t i = 1; i < code; i++) buf[w++] = buf[r++];
        if (code != 0xFF && r < n) buf[w++] = 0;
    }
    return (int)w;
}

/* Frame = 0x00 ‖ COBS(payload ‖ crc) ‖ 0x00. s_tx_mux must be held. */
static size_t build_frame(const uint8_t *payload, size_t len)
{
    static uint8_t body[FRAME_MAX + 2];
    memcpy(body, payload, len);
    uint16_t crc = crc16_ccitt(payload, len);
    body[len] = (uint8_t)(crc >> 8);
    body[len + 1] = (uint8_t)crc;
    s_tx_buf[0] = 0;
    size_t n = cobs_encode(body, len + 2, s_tx_buf + 1);
    s_tx_buf[1 + n] = 0;
    return n + 2;
}

static bool is_hello(const uint8_t *f, size_t len)
{
    /* Envelope map {0: mt_hello(1), ...}: map header, then key 0, value 1. */
    return len >= 3 && (f[0] & 0xE0) == 0xA0 && f[1] == 0x00 && f[2] == 0x01;
}

bool sw_usb_link_send(const uint8_t *frame, size_t len)
{
    if (!s_up || !s_driver_ready || len == 0 || len > FRAME_MAX) return false;
    bool ok = false;
    if (xSemaphoreTake(s_tx_mux, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) != pdTRUE) return false;
    /* With nobody reading the port the TX queue never drains; don't pile hello retries into it,
     * they would all be replayed to the daemon as soon as it opens the port. */
    if (!is_hello(frame, len) || usb_serial_jtag_wait_tx_done(0) == ESP_OK) {
        size_t n = build_frame(frame, len);
        ok = usb_serial_jtag_write_bytes(s_tx_buf, n, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) == (int)n;
    }
    xSemaphoreGive(s_tx_mux);
    return ok;
}

void hal_debug_line(const char *json)
{
    if (!s_driver_ready) {
        printf("#%s\n", json);
        return;
    }
    char line[512];
    int n = snprintf(line, sizeof line, "#%s\n", json);
    if (n <= 0) return;
    if (n >= (int)sizeof line) {
        n = sizeof line - 1;
        line[n - 1] = '\n';
    }
    if (xSemaphoreTake(s_tx_mux, pdMS_TO_TICKS(20)) != pdTRUE) return;
    usb_serial_jtag_write_bytes(line, (size_t)n, 0);
    xSemaphoreGive(s_tx_mux);
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

typedef struct {
    uint8_t buf[CHUNK_MAX];
    size_t len;
    bool discarding;
} rx_state_t;

/* Feed one received byte; returns true when `st->buf` holds a verified payload in [0, *plen). */
static bool rx_byte(rx_state_t *st, uint8_t b, size_t *plen)
{
    if (b != 0) {
        if (st->discarding) return false;
        if (st->len >= CHUNK_MAX) {
            st->len = 0;
            st->discarding = true;
            return false;
        }
        st->buf[st->len++] = b;
        return false;
    }
    size_t n = st->len;
    bool was_discarding = st->discarding;
    st->len = 0;
    st->discarding = false;
    if (was_discarding || n == 0) return false;
    int d = cobs_decode(st->buf, n);
    if (d < 3 || d - 2 > FRAME_MAX) return false;
    size_t pl = (size_t)d - 2;
    uint16_t crc = (uint16_t)((st->buf[pl] << 8) | st->buf[pl + 1]);
    if (crc16_ccitt(st->buf, pl) != crc) return false;
    *plen = pl;
    return true;
}

static void link_task(void *arg)
{
    (void)arg;
    static rx_state_t st;
    static uint8_t rx[256];
    bool up = false, heard = false;
    uint32_t detach_at = 0, last_rx = 0;

    for (;;) {
        int n = usb_serial_jtag_read_bytes(rx, sizeof rx, pdMS_TO_TICKS(20));
        uint32_t now = now_ms();
        for (int i = 0; i < n; i++) {
            size_t plen;
            if (!rx_byte(&st, rx[i], &plen)) continue;
            last_rx = now;
            heard = true;
            if (s_on_rx) s_on_rx(HAL_LINK_USB, st.buf, plen);
            vTaskDelay(pdMS_TO_TICKS(DELIVER_GAP_MS));
        }

        bool host = usb_serial_jtag_is_connected();
        if (!up && host) {
            up = s_up = true;
            heard = false;
            detach_at = 0;
            if (s_on_state) s_on_state(HAL_LINK_USB, true);
        } else if (up && !host) {
            if (detach_at == 0) detach_at = now ? now : 1;
            if (now - detach_at >= DETACH_DEBOUNCE_MS) {
                up = s_up = false;
                if (s_on_state) s_on_state(HAL_LINK_USB, false);
            }
        } else if (up) {
            detach_at = 0;
            if (heard && now - last_rx >= SILENCE_MS) {
                heard = false;
                if (s_on_state) {
                    s_on_state(HAL_LINK_USB, false);
                    s_on_state(HAL_LINK_USB, true);
                }
            }
        }
    }
}

void sw_usb_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state)
{
    s_on_rx = on_rx;
    s_on_state = on_state;
    s_tx_mux = xSemaphoreCreateMutex();

    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.tx_buffer_size = TX_RING;
    cfg.rx_buffer_size = RX_RING;
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) return;
    usb_serial_jtag_vfs_use_driver(); /* printf/ESP_LOG keep working through the driver */
    s_driver_ready = true;
    xTaskCreate(link_task, "usb_link", 6144, NULL, 5, NULL);
}
