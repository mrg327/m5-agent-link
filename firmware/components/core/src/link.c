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

hal_link_kind_t link_active_kind(void)
{
    if (g_link.welcomed) return g_link.kind;
    for (int k = 0; k < LINK_N_KINDS; k++)
        if (g_link.up[k]) return (hal_link_kind_t)k;
    return g_link.kind;
}

static void hello_on(int k)
{
    g_link.hello_ms[k] = s_now_ms;
    link_tx_hello((hal_link_kind_t)k);
}

/* New handshake: per-link id state starts over and every transport that is up gets a hello. */
static void start_handshake(void)
{
    g_link.tx_id = 0;
    g_link.last_rx_id = 0;
    memset(g_link.pending, 0, sizeof g_link.pending);
    for (int k = 0; k < LINK_N_KINDS; k++)
        if (g_link.up[k]) hello_on(k);
}

void link_on_state(hal_link_kind_t kind, bool connected)
{
    dbg_event("link", connected ? "up" : "down", "\"transport\":%d", (int)kind);
    if ((int)kind < 0 || (int)kind >= LINK_N_KINDS || g_link.up[kind] == connected) return;
    g_link.up[kind] = connected;
    g_link.connected = false;
    for (int k = 0; k < LINK_N_KINDS; k++) g_link.connected |= g_link.up[k];
    if (connected) {
        if (g_link.welcomed) return; /* stay on the active transport */
        if (!g_link.hello_cycle) {
            g_link.hello_cycle = true;
            start_handshake();
        } else {
            hello_on(kind);
        }
    } else if (!g_link.connected) {
        g_link.hello_cycle = false;
        if (g_link.welcomed) {
            g_link.welcomed = false;
            if (g_link.hooks.on_link) g_link.hooks.on_link(false);
        }
    } else if (g_link.welcomed && kind == g_link.kind) {
        g_link.welcomed = false;
        if (g_link.hooks.on_link) g_link.hooks.on_link(false);
        g_link.hello_cycle = true;
        start_handshake(); /* fall back to whichever transport is still up */
    }
}

void link_on_frame_from(hal_link_kind_t kind, const uint8_t *frame, size_t len)
{
    if ((int)kind < 0 || (int)kind >= LINK_N_KINDS || !g_link.up[kind]) return;
    if (g_link.welcomed && kind != g_link.kind) return;
    size_t used = 0;
    memset(&g_rx_env, 0, sizeof g_rx_env);
    int rc = cbor_decode_envelope(frame, len, &g_rx_env, &used);
    if (rc != ZCBOR_SUCCESS) {
        /* Unknown message types land here too; the protocol says to ignore them. */
        dbg_event_at(DBG_DEBUG, "link", "rx_undecodable", "\"len\":%u,\"rc\":%d", (unsigned)len, rc);
        return;
    }
    if (!g_link.welcomed) {
        if (g_rx_env.envelope_choice != envelope_msg_welcome_m_c) return;
        /* A welcome from any transport may claim the link; rx ids restart for it. */
        g_link.kind = kind;
        g_link.last_rx_id = 0;
    }
    g_link.last_activity_ms = s_now_ms;
    link_rx_dispatch(&g_rx_env, s_now_ms);
    if (!g_link.welcomed) return; /* welcome rejected */
    g_link.hello_cycle = false;
}

void link_on_frame(const uint8_t *frame, size_t len)
{
    link_on_frame_from(link_active_kind(), frame, len);
}

void link_tick(uint32_t now_ms)
{
    s_now_ms = now_ms;
    if (!g_link.connected) return;
    if (!g_link.welcomed) {
        for (int k = 0; k < LINK_N_KINDS; k++) {
            if (!g_link.up[k] || now_ms - g_link.hello_ms[k] < HELLO_RETRY_MS) continue;
            dbg_event("link", "hello_retry", "\"transport\":%d", k);
            hello_on(k);
        }
    }
    if (g_link.welcomed && now_ms - g_link.last_activity_ms >= IDLE_PING_MS) {
        g_link.last_activity_ms = now_ms;
        link_tx_ping();
    }
}

bool link_send_cmd(link_op_t op, uint32_t sid, uint32_t rid, uint32_t hold_ms)
{
    if (!g_link.welcomed) return false;
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
