#include <string.h>

#include "link_internal.h"

link_state_t g_link;
struct envelope_r g_rx_env, g_tx_env;

#define HELLO_RETRY_MS 3000
#define IDLE_PING_MS 60000

static uint32_t s_now_ms;

void link_init(const link_hooks_t *hooks)
{
    memset(&g_link, 0, sizeof g_link);
    g_link.hooks = *hooks;
}

bool link_connected(void) { return g_link.connected; }
bool link_welcomed(void) { return g_link.welcomed; }

void link_on_state(hal_link_kind_t kind, bool connected)
{
    dbg_event("link", connected ? "up" : "down", "\"transport\":%d", (int)kind);
    if (connected) {
        if (g_link.connected) return; /* first transport wins */
        g_link.kind = kind;
        g_link.connected = true;
        g_link.welcomed = false;
        g_link.tx_id = 0;
        g_link.last_rx_id = 0;
        memset(g_link.pending, 0, sizeof g_link.pending);
        link_tx_hello();
        g_link.hello_sent_ms = s_now_ms;
    } else if (g_link.connected && kind == g_link.kind) {
        g_link.connected = g_link.welcomed = false;
        if (g_link.hooks.on_link) g_link.hooks.on_link(false);
    }
}

void link_on_frame(const uint8_t *frame, size_t len)
{
    size_t used = 0;
    memset(&g_rx_env, 0, sizeof g_rx_env);
    int rc = cbor_decode_envelope(frame, len, &g_rx_env, &used);
    g_link.last_activity_ms = s_now_ms;
    if (rc != ZCBOR_SUCCESS) {
        /* Unknown message types land here too; the protocol says to ignore them. */
        dbg_event_at(DBG_DEBUG, "link", "rx_undecodable", "\"len\":%u,\"rc\":%d", (unsigned)len, rc);
        return;
    }
    link_rx_dispatch(&g_rx_env, s_now_ms);
}

void link_tick(uint32_t now_ms)
{
    s_now_ms = now_ms;
    if (!g_link.connected) return;
    if (!g_link.welcomed && now_ms - g_link.hello_sent_ms >= HELLO_RETRY_MS) {
        g_link.hello_sent_ms = now_ms;
        dbg_event("link", "hello_retry", NULL);
        link_tx_hello();
    }
    if (g_link.welcomed && now_ms - g_link.last_activity_ms >= IDLE_PING_MS) {
        g_link.last_activity_ms = now_ms;
        link_tx_ping();
    }
}

bool link_send_cmd(link_op_t op, uint32_t sid, uint32_t rid, uint32_t hold_ms)
{
    if (!g_link.connected) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    struct msg_cmd *m = &g_tx_env.envelope_msg_cmd_m;
    g_tx_env.envelope_choice = envelope_msg_cmd_m_c;
    m->msg_cmd_cmd_m.cmd_op_m.cmd_op_choice = (int)op;
    if (sid) {
        m->msg_cmd_cmd_m.cmd_uint1uint_present = true;
        m->msg_cmd_cmd_m.cmd_uint1uint.cmd_uint1uint = sid;
    }
    if (rid) {
        m->msg_cmd_cmd_m.cmd_uint2uint_present = true;
        m->msg_cmd_cmd_m.cmd_uint2uint.cmd_uint2uint = rid;
    }
    if (hold_ms) {
        m->msg_cmd_cmd_m.cmd_uint4uint_present = true;
        m->msg_cmd_cmd_m.cmd_uint4uint.cmd_uint4uint = hold_ms;
    }
    uint32_t id = ++g_link.tx_id;
    m->msg_cmd_hdr_m.hdr_uint1uint = id;
    /* Unanswered commands are simply overwritten by newer ones. */
    g_link.pending[id % LINK_MAX_PENDING] = (pending_cmd_t){.id = id, .op = (int)op, .sid = sid};
    uint8_t buf[64];
    size_t len = 0;
    if (cbor_encode_envelope(buf, sizeof buf, &g_tx_env, &len) != ZCBOR_SUCCESS) return false;
    dbg_event("link", "tx_cmd", "\"id\":%u,\"op\":%d,\"sid\":%u,\"rid\":%u,\"hold_ms\":%u", (unsigned)id,
              (int)op, (unsigned)sid, (unsigned)rid, (unsigned)hold_ms);
    return hal_link_send(g_link.kind, buf, len);
}
