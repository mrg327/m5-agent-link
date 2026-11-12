/* hal_link_* over a WebSocket client to the daemon's simulator transport (one binary frame = one
 * envelope). Reconnects every 2 s; runs on the main thread from sim_link_poll(). */
#include "sim.h"
#include "sim_ws.h"

#define WS_HOST "127.0.0.1"
#define WS_PORT 7878
#define RETRY_MS 2000

static hal_link_rx_cb_t s_rx;
static hal_link_state_cb_t s_state;
static bool s_ws_enabled = true, s_fake_up[3]; /* per hal_link_kind_t, for --no-ws scripts */
static uint32_t s_next_try_ms, s_attempts;

static void on_open(void)
{
    s_attempts = 0;
    sim_log("ws_open", "\"url\":\"ws://" WS_HOST ":7878/\"");
    if (s_state) s_state(HAL_LINK_WS, true);
}

static void on_close(void)
{
    sim_log("ws_close", NULL);
    if (s_state) s_state(HAL_LINK_WS, false);
}

static void on_message(const uint8_t *data, size_t len)
{
    if (s_rx) s_rx(HAL_LINK_WS, data, len);
}

void hal_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state)
{
    s_rx = on_rx;
    s_state = on_state;
}

static sim_tx_tap_t s_tap;
void sim_link_set_tx_tap(sim_tx_tap_t tap) { s_tap = tap; }

bool hal_link_send(hal_link_kind_t kind, const uint8_t *frame, size_t len)
{
    if ((unsigned)kind > HAL_LINK_WS) return false;
    if (s_tap && !(kind == HAL_LINK_WS && s_ws_enabled) && s_fake_up[kind]) s_tap(frame, len);
    if (kind == HAL_LINK_WS && s_ws_enabled) return ws_send_binary(frame, len);
    /* fake transports swallow frames; USB and BLE ones are logged so scripts can see where hellos go */
    if (kind != HAL_LINK_WS && s_fake_up[kind]) sim_log("fake_tx", "\"transport\":%d,\"len\":%u", (int)kind, (unsigned)len);
    return s_fake_up[kind];
}

void hal_link_interactive(bool on) { (void)on; }

void sim_link_enable_ws(bool enable) { s_ws_enabled = enable; }

void sim_link_set_connected_kind(int kind, bool connected)
{
    if (kind < 0 || kind > HAL_LINK_WS) return;
    s_fake_up[kind] = connected;
    if (s_state) s_state((hal_link_kind_t)kind, connected);
}

void sim_link_set_connected(bool connected) { sim_link_set_connected_kind(HAL_LINK_WS, connected); }

void sim_link_inject_rx_kind(int kind, const uint8_t *frame, size_t len)
{
    if (s_rx) s_rx((hal_link_kind_t)kind, frame, len);
}

void sim_link_inject_rx(const uint8_t *frame, size_t len) { sim_link_inject_rx_kind(HAL_LINK_WS, frame, len); }

void sim_link_poll(uint32_t now_ms)
{
    if (!s_ws_enabled) return;
    static const ws_callbacks_t cb = {on_open, on_close, on_message};
    if (ws_is_open()) {
        ws_poll(&cb);
        return;
    }
    if ((int32_t)(now_ms - s_next_try_ms) < 0) return;
    s_next_try_ms = now_ms + RETRY_MS;
    s_attempts++;
    if (ws_connect(WS_HOST, WS_PORT, "/")) on_open();
    else sim_log("ws_retry", "\"attempt\":%u,\"in_ms\":%d,\"reason\":\"no daemon at ws://" WS_HOST ":7878\"",
                 (unsigned)s_attempts, RETRY_MS);
}
