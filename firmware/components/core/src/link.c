#include <string.h>

#include "link_internal.h"

link_state_t g_link;
/* The decoded and the to-be-encoded message: 6 KB each, only touched from core_step. PSRAM. */
MODEL_EXT_BSS struct envelope_r g_rx_env;
MODEL_EXT_BSS struct envelope_r g_tx_env;

#define HELLO_RETRY_MS 3000
/* While welcomed on another transport with USB plugged in, offer a hello on USB this often: a daemon
 * there takes the link over (USB is preferred when plugged in). */
#define USB_PREFER_MS 5000
#define USB_PREFER_BACKOFF_MS 30000
static uint32_t s_usb_offer_gap = USB_PREFER_MS;
#define IDLE_PING_MS 60000

static uint32_t s_now_ms;

void link_init(const link_hooks_t *hooks)
{
    memset(&g_link, 0, sizeof g_link);
    g_link.hooks = *hooks;
    s_usb_offer_gap = USB_PREFER_MS;
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
        if (g_link.welcomed) {
            /* stay on the active transport, unless USB arrives: it is preferred, so offer it a hello */
            if (kind == HAL_LINK_USB && g_link.kind != HAL_LINK_USB) hello_on(HAL_LINK_USB);
            return;
        }
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
    /* Another transport is only heard when it is USB taking the link over with a welcome. */
    if (g_link.welcomed && kind != g_link.kind && kind != HAL_LINK_USB) return;
    size_t used = 0;
    memset(&g_rx_env, 0, sizeof g_rx_env);
    int rc = cbor_decode_envelope(frame, len, &g_rx_env, &used);
    if (rc != ZCBOR_SUCCESS) {
        /* Unknown message types land here too; the protocol says to ignore them. */
        dbg_event_at(DBG_DEBUG, "link", "rx_undecodable", "\"len\":%u,\"rc\":%d", (unsigned)len, rc);
        return;
    }
    if (g_link.welcomed && kind != g_link.kind) {
        if (g_rx_env.envelope_choice != envelope_msg_welcome_m_c) return;
        /* USB is preferred when plugged in: its daemon takes over. The welcome below starts the new
         * link (cards dropped, a snapshot follows); the old transport is ignored from now on. */
        dbg_event("link", "prefer_usb", "\"from\":%d", (int)g_link.kind);
        g_link.welcomed = false;
        memset(g_link.pending, 0, sizeof g_link.pending);
        g_link.tx_id = 0;
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
    if (g_link.welcomed && g_link.kind != HAL_LINK_USB && g_link.up[HAL_LINK_USB] &&
        now_ms - g_link.hello_ms[HAL_LINK_USB] >= s_usb_offer_gap) {
        /* A USB daemon would take the link over. A host that is attached but not reading fills the
         * USB buffer and each send can block 200 ms, so back off while the offer does not go out. */
        g_link.hello_ms[HAL_LINK_USB] = now_ms;
        s_usb_offer_gap = link_tx_hello(HAL_LINK_USB) ? USB_PREFER_MS : USB_PREFER_BACKOFF_MS;
    }
    if (g_link.welcomed && now_ms - g_link.last_activity_ms >= IDLE_PING_MS) {
        g_link.last_activity_ms = now_ms;
        link_tx_ping();
    }
}

/* Fills g_tx_env with a cmd envelope (no arg yet); NULL unless welcomed. */
static struct cmd *begin_cmd(link_op_t op, uint32_t sid, uint32_t rid, uint32_t hold_ms)
{
    if (!g_link.welcomed) return NULL;
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
    return &m->msg_cmd_cmd_m;
}

/* Numbers, registers and sends the cmd begun with begin_cmd(). `arg_log` is -1 for no scalar arg. */
static bool finish_cmd(link_op_t op, uint32_t sid, uint32_t rid, uint32_t hold_ms, int arg_log)
{
    struct msg_cmd *m = &g_tx_env.envelope_msg_cmd_m;
    uint32_t id = ++g_link.tx_id;
    m->msg_cmd_hdr_m.hdr_uint1uint = id;
    /* Unanswered commands are simply overwritten by newer ones. */
    g_link.pending[id % LINK_MAX_PENDING] = (pending_cmd_t){.id = id, .op = (int)op, .sid = sid, .rid = rid};
    uint8_t buf[64];
    size_t len = 0;
    if (cbor_encode_envelope(buf, sizeof buf, &g_tx_env, &len) != ZCBOR_SUCCESS) return false;
    dbg_event("link", "tx_cmd", "\"id\":%u,\"op\":%d,\"sid\":%u,\"rid\":%u,\"hold_ms\":%u,\"arg\":%d", (unsigned)id,
              (int)op, (unsigned)sid, (unsigned)rid, (unsigned)hold_ms, arg_log);
    return hal_link_send(g_link.kind, buf, len);
}

bool link_send_cmd(link_op_t op, uint32_t sid, uint32_t rid, uint32_t hold_ms)
{
    if (!begin_cmd(op, sid, rid, hold_ms)) return false;
    return finish_cmd(op, sid, rid, hold_ms, -1);
}

bool link_send_cmd_arg(link_op_t op, uint32_t sid, uint32_t rid, uint32_t arg, uint32_t hold_ms)
{
    struct cmd *c = begin_cmd(op, sid, rid, hold_ms);
    if (!c) return false;
    c->cmd_arg_m_present = true;
    c->cmd_arg_m.cmd_arg_m.cmd_arg_choice = cmd_arg_uint_c;
    c->cmd_arg_m.cmd_arg_m.cmd_arg_uint = arg;
    return finish_cmd(op, sid, rid, hold_ms, (int)arg);
}

bool link_send_approve_rule(uint32_t sid, uint32_t rid, uint32_t hold_ms, uint32_t rh)
{
    struct cmd *c = begin_cmd(LINK_OP_APPROVE, sid, rid, hold_ms);
    if (!c) return false;
    c->cmd_uint5uint_present = true;
    c->cmd_uint5uint.cmd_uint5uint = rh;
    dbg_event("link", "tx_rule", "\"rid\":%u,\"rh\":%u", (unsigned)rid, (unsigned)rh);
    return finish_cmd(LINK_OP_APPROVE, sid, rid, hold_ms, -1);
}

bool link_send_scroll(uint32_t sid, int unit, int delta, uint32_t seq)
{
    struct cmd *c = begin_cmd(LINK_OP_SCROLL, sid, 0, 0);
    if (!c) return false;
    c->cmd_arg_m_present = true;
    struct cmd_arg_r *a = &c->cmd_arg_m.cmd_arg_m;
    a->cmd_arg_choice = cmd_arg_scroll_arg_m_c;
    a->cmd_arg_scroll_arg_m.scroll_arg_scroll_unit_m.scroll_unit_choice = unit;
    a->cmd_arg_scroll_arg_m.scroll_arg_uint1int = delta;
    a->cmd_arg_scroll_arg_m.scroll_arg_uint2uint_present = true;
    a->cmd_arg_scroll_arg_m.scroll_arg_uint2uint.scroll_arg_uint2uint = seq;
    dbg_event("link", "tx_scroll", "\"unit\":%d,\"delta\":%d,\"seq\":%u", unit, delta, (unsigned)seq);
    return finish_cmd(LINK_OP_SCROLL, sid, 0, 0, -1);
}
