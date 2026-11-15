// NimBLE GATT peripheral link (PROTOCOL §1: rx/tx characteristics, 1-byte fragment header).
//
// Threading: NimBLE callbacks (on_rx / on_state) run on the NimBLE host task. core's app.c
// copies them into spinlock-protected rings that the core task drains, so they are safe to
// call from here directly. sw_ble_link_send() runs on the core task.
#include <stdio.h>
#include <string.h>

#include "board_priv.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#pragma GCC diagnostic ignored "-Wmissing-field-initializers" /* NimBLE def tables are sparse */

static const char *TAG = "link_ble";

#define BLE_MSG_MAX 4096
#define BLE_PREFERRED_MTU 247 /* 247 + 4 (L2CAP) = 251: one ATT PDU per max-length LL PDU */
/* LE Data Length Extension: 251-octet LL payloads (2120 us covers them on the 1M PHY) so a full
 * MTU-sized write/notification goes in one LL PDU instead of ten 27-octet ones. 0 disables. */
#define BLE_DATA_LEN_OCTETS 251
#define BLE_DATA_LEN_TIME_US 2120
#define BLE_SHORT_LL_MTU 239 /* 239 + 4 (L2CAP) = 243 = 9 × 27: whole 27-octet LL packets without DLE */
#define BLE_HDR_FIRST 0x80
#define BLE_HDR_LAST 0x40
#define BLE_HDR_SEQ 0x3f
#define BLE_NOTIFY_RETRY_MS 10
#define BLE_NOTIFY_TIMEOUT_MS 1000
#define BLE_NAME_LEN 15 /* "AgentLink-XXXX" + NUL */

/* Single definition of the AgentLink UUIDs on this side (little-endian byte order as NimBLE
 * wants it). Canonical: a9e1f000-7c3b-4d52-9e18-b06a35c4d2f1 (service), ...f001 rx, ...f002 tx. */
#define AL_UUID128(w)                                                                              \
    BLE_UUID128_INIT(0xf1, 0xd2, 0xc4, 0x35, 0x6a, 0xb0, 0x18, 0x9e, 0x52, 0x4d, 0x3b, 0x7c,      \
                     (w) & 0xff, ((w) >> 8) & 0xff, 0xe1, 0xa9)
static const ble_uuid128_t kSvcUuid = AL_UUID128(0xf000);
static const ble_uuid128_t kRxUuid = AL_UUID128(0xf001);
static const ble_uuid128_t kTxUuid = AL_UUID128(0xf002);

static hal_link_rx_cb_t s_on_rx;
static hal_link_state_cb_t s_on_state;
static SemaphoreHandle_t s_send_lock;

static uint8_t s_own_addr_type;
static char s_name[BLE_NAME_LEN];
static uint16_t s_tx_handle;

/* ── centrals (MULTI-LINK.md): up to BLE_PEERS at once, each its own port (HAL_LINK_BLE, HAL_LINK_BLE2). The
 * host task owns a peer's fields; the core task reads conn / subscribed / cur_itvl, and sets fast / bulk
 * through the calls below, which run on it. ── */

#define BLE_PEERS CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#if BLE_PEERS < 2
#warning "one BLE central only: firmware/sdkconfig predates MULTI-LINK.md; delete it to take sdkconfig.defaults"
#endif
enum { ITVL_BASE, ITVL_FAST, ITVL_BULK };

typedef struct {
    volatile uint16_t conn; /* BLE_HS_CONN_HANDLE_NONE: a free peer */
    volatile bool subscribed;
    bool up; /* last state reported to the core */
    /* reassembler (host task only) */
    uint8_t *rasm;
    size_t rasm_len;
    bool rasm_active;
    uint8_t rasm_next;
    uint16_t rx_frag_max; /* largest write this connection, logged when it grows */
    /* connection interval: the central's choice, 15–30 ms while interactive (DESIGN §7), or as short as the
     * central allows during a bulk transfer (an update, FIRMWARE.md §9) */
    uint16_t base_itvl, base_latency, base_timeout;
    volatile uint16_t cur_itvl; /* the interval in force (1.25 ms units), 0 = no central: power report */
    bool fast, bulk;            /* wanted */
    uint8_t applied;            /* ITVL_*: last granted (or, while req_pending, requested) */
    uint8_t prev_applied;       /* before the pending request */
    bool bulk_refused;          /* this central refused the bulk interval */
    bool req_pending;           /* one update at a time; the next is sent when it completes */
} peer_t;

static peer_t s_peers[BLE_PEERS];
/* 4 KB each: PSRAM, as internal DRAM is what the BLE controller needs (sdkconfig.defaults). */
EXT_RAM_BSS_ATTR static uint8_t s_rasm_buf[BLE_PEERS][BLE_MSG_MAX];

static hal_link_kind_t port_of(const peer_t *p) { return p == &s_peers[0] ? HAL_LINK_BLE : HAL_LINK_BLE2; }

static peer_t *peer_for_port(hal_link_kind_t port)
{
    int i = port == HAL_LINK_BLE ? 0 : port == HAL_LINK_BLE2 ? 1 : -1;
    return i >= 0 && i < BLE_PEERS ? &s_peers[i] : NULL;
}

static peer_t *peer_for_conn(uint16_t conn)
{
    if (conn == BLE_HS_CONN_HANDLE_NONE) return NULL;
    for (int i = 0; i < BLE_PEERS; i++)
        if (s_peers[i].conn == conn) return &s_peers[i];
    return NULL;
}

static int peers_linked(void)
{
    int n = 0;
    for (int i = 0; i < BLE_PEERS; i++) n += s_peers[i].conn != BLE_HS_CONN_HANDLE_NONE;
    return n;
}

/* ── reassembler (host task only) ── */

static void rasm_reset(peer_t *p)
{
    p->rasm_active = false;
    p->rasm_len = 0;
}

static void rasm_feed(peer_t *p, const uint8_t *buf, size_t n)
{
    if (n < 1) return;
    uint8_t hdr = buf[0];
    uint8_t seq = hdr & BLE_HDR_SEQ;
    const uint8_t *data = buf + 1;
    size_t dlen = n - 1;

    if (hdr & BLE_HDR_FIRST) {
        if (p->rasm_active) ESP_LOGW(TAG, "new FIRST mid-message, dropping partial (%u B)", (unsigned)p->rasm_len);
        rasm_reset(p);
        p->rasm_active = true;
    } else if (!p->rasm_active) {
        return; /* continuation of a dropped message */
    } else if (seq != p->rasm_next) {
        ESP_LOGW(TAG, "seq gap (got %u want %u), dropping message", seq, p->rasm_next);
        rasm_reset(p);
        return;
    }
    p->rasm_next = (seq + 1) & BLE_HDR_SEQ;
    if (p->rasm_len + dlen > BLE_MSG_MAX) {
        ESP_LOGW(TAG, "message exceeds %d B, dropped", BLE_MSG_MAX);
        rasm_reset(p);
        return;
    }
    memcpy(p->rasm + p->rasm_len, data, dlen);
    p->rasm_len += dlen;
    if (hdr & BLE_HDR_LAST) {
        size_t len = p->rasm_len;
        rasm_reset(p);
        if (len > 0 && s_on_rx) s_on_rx(port_of(p), p->rasm, len);
    }
}

/* ── link state ── */

static void report_state(peer_t *p, bool up)
{
    if (p->up == up) return;
    p->up = up;
    if (!up) rasm_reset(p);
    if (s_on_state) s_on_state(port_of(p), up);
}

static void advertise(void);

/* ── connection interval ── */

#define BLE_FAST_ITVL_MIN 12  /* 1.25 ms units: 15 ms, Apple's floor */
#define BLE_FAST_ITVL_MAX 24  /* 30 ms; Apple wants max ≥ min + 15 ms */
/* A central that cannot do long LL packets (no data length extension, e.g. the BCM4350C0) moves a few
 * 27-byte packets per connection event, so the interval sets the rate: ~3 KB/s at 30 ms, ~13 KB/s at
 * 7.5 ms. A central that refuses this gets the fast request instead. */
#define BLE_BULK_ITVL_MIN 6   /* 7.5 ms, the minimum */
#define BLE_BULK_ITVL_MAX 6
#define BLE_FAST_TIMEOUT 400  /* 10 ms units: 4 s */
#define BLE_IDLE_ITVL 24      /* 30 ms (to 45 ms): the base after a connect at a remembered bulk interval */

static void conn_params(peer_t *p, const char *why, bool save_base)
{
    struct ble_gap_conn_desc d;
    if (p->conn == BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_find(p->conn, &d) != 0) return;
    ESP_LOGI(TAG, "%s (handle %u): interval %u.%02u ms, latency %u, timeout %u ms", why, p->conn,
             d.conn_itvl * 125 / 100, d.conn_itvl * 125 % 100, d.conn_latency, d.supervision_timeout * 10);
    p->cur_itvl = d.conn_itvl;
    if (save_base) {
        p->base_itvl = d.conn_itvl;
        p->base_latency = d.conn_latency;
        p->base_timeout = d.supervision_timeout;
    }
}

static uint8_t wanted_level(const peer_t *p)
{
    uint8_t lv = p->bulk && !p->bulk_refused ? ITVL_BULK : (p->fast || p->bulk) ? ITVL_FAST : ITVL_BASE;
    uint16_t min = lv == ITVL_BULK ? BLE_BULK_ITVL_MIN : BLE_FAST_ITVL_MIN;
    if (lv == ITVL_BULK && p->base_itvl <= min) lv = ITVL_FAST; /* the central already runs this fast */
    if (lv == ITVL_FAST && p->base_itvl <= BLE_FAST_ITVL_MIN) lv = ITVL_BASE;
    return lv;
}

/* Asks the central for the interval that is wanted now, if that is not the one it runs. */
static void apply_interval(peer_t *p)
{
    if (p->conn == BLE_HS_CONN_HANDLE_NONE || !p->base_itvl || p->req_pending) return;
    uint8_t lv = wanted_level(p);
    if (lv == p->applied) return;
    static const char *const kName[] = {"base", "fast", "bulk"};
    struct ble_gap_upd_params u = {};
    u.itvl_min = lv == ITVL_BULK ? BLE_BULK_ITVL_MIN : lv == ITVL_FAST ? BLE_FAST_ITVL_MIN : p->base_itvl;
    u.itvl_max = lv == ITVL_BULK ? BLE_BULK_ITVL_MAX : lv == ITVL_FAST ? BLE_FAST_ITVL_MAX : (uint16_t)(p->base_itvl + 12);
    u.latency = lv == ITVL_BASE ? p->base_latency : 0;
    u.supervision_timeout = lv == ITVL_BASE ? p->base_timeout : BLE_FAST_TIMEOUT;
    int rc = ble_gap_update_params(p->conn, &u);
    p->req_pending = rc == 0;
    if (rc == 0) {
        p->prev_applied = p->applied;
        p->applied = lv;
    }
    ESP_LOGI(TAG, "interval request %s (handle %u, %u–%u units): rc=%d", kName[lv], p->conn, u.itvl_min, u.itvl_max, rc);
}

void sw_ble_link_interactive(hal_link_kind_t port, bool on)
{
    peer_t *p = peer_for_port(port);
    if (!p || on == p->fast) return;
    p->fast = on;
    apply_interval(p);
}

void sw_ble_link_bulk(hal_link_kind_t port, bool on)
{
    peer_t *p = peer_for_port(port);
    if (!p || on == p->bulk) return;
    p->bulk = on;
    apply_interval(p);
}

/* The shortest interval in force: the one that costs the most. */
uint32_t sw_ble_link_interval_us(void)
{
    uint32_t us = 0;
    for (int i = 0; i < BLE_PEERS; i++) {
        uint32_t v = s_peers[i].conn == BLE_HS_CONN_HANDLE_NONE ? 0 : s_peers[i].cur_itvl * 1250u;
        if (v && (!us || v < us)) us = v;
    }
    return us;
}

/* A new central: the link setup (PHY, data length, MTU, interval). */
static void peer_connected(peer_t *p, uint16_t conn)
{
    p->conn = conn;
    p->subscribed = false;
    p->rx_frag_max = 0;
    rasm_reset(p);
    ESP_LOGI(TAG, "central connected (handle %u, port %d, %d linked)", conn, (int)port_of(p), peers_linked());
    ble_gap_set_prefered_le_phy(conn, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK, 0);
    ble_att_set_preferred_mtu(BLE_PREFERRED_MTU);
    if (BLE_DATA_LEN_OCTETS) {
        int rc = ble_gap_set_data_len(conn, BLE_DATA_LEN_OCTETS, BLE_DATA_LEN_TIME_US);
        if (rc != 0) {
            /* No long LL packets (the central lacks data length extension): an ATT packet then goes in
             * 27-byte LL packets, and MTU 247 (251 B with L2CAP) needs ten, the last one 8 B. MTU 239 fills
             * nine exactly. The central exchanges the MTU right after this. The preferred MTU is the host's,
             * not the connection's: a second central connecting meanwhile gets the same answer. */
            ESP_LOGW(TAG, "data length request rc=%d: asking for MTU %d", rc, BLE_SHORT_LL_MTU);
            ble_att_set_preferred_mtu(BLE_SHORT_LL_MTU);
        }
    }
    p->req_pending = p->bulk_refused = false;
    p->applied = ITVL_BASE;
    conn_params(p, "connected", true);
    if (p->base_itvl && p->base_itvl < BLE_FAST_ITVL_MIN) {
        /* No central picks this on its own: BlueZ keeps the parameters a peripheral last asked for and
         * connects with them, so an update that ended in a restart left the bulk interval behind. Ask for the
         * usual 30–45 ms instead of staying there. */
        p->base_itvl = BLE_IDLE_ITVL;
        p->applied = ITVL_BULK; /* what the link runs: apply_interval() asks for the base */
    }
    apply_interval(p);
}

static void peer_gone(peer_t *p)
{
    p->conn = BLE_HS_CONN_HANDLE_NONE;
    p->subscribed = false;
    p->base_itvl = 0;
    p->cur_itvl = 0;
    report_state(p, false);
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
    {
        /* NimBLE reports a peripheral's connect only after reading the central's features, and passes
         * that read's status on: a central that refuses it (the BCM4350C0, HCI 0x1a "unsupported remote
         * feature") yields status 26 for a link that is up. Treated as failed, the link skipped the
         * setup below (data length, PHY, fast interval) and ran at ~1.7 KB/s (issue 2047). */
        struct ble_gap_conn_desc cd;
        uint16_t h = ev->connect.conn_handle;
        bool up = ev->connect.status == 0 || (h != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(h, &cd) == 0);
        if (!up) {
            ESP_LOGW(TAG, "connect failed: %d", ev->connect.status);
            advertise();
            break;
        }
        if (ev->connect.status != 0) ESP_LOGW(TAG, "connect status %d, but the link is up: using it", ev->connect.status);
        if (peer_for_conn(h)) break; /* already set up (a subscribe came first) */
        peer_t *p = NULL;
        for (int i = 0; !p && i < BLE_PEERS; i++)
            if (s_peers[i].conn == BLE_HS_CONN_HANDLE_NONE) p = &s_peers[i];
        if (!p) { /* the controller allows no more than BLE_PEERS: not expected */
            ESP_LOGW(TAG, "central on handle %u: no free peer, ending it", h);
            ble_gap_terminate(h, BLE_ERR_CONN_LIMIT);
            break;
        }
        peer_connected(p, h);
        advertise(); /* for the next central, while a peer is free */
        break;
    }
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        ESP_LOGI(TAG, "phy (handle %u): tx %u rx %u (status %d)", ev->phy_updated.conn_handle, ev->phy_updated.tx_phy,
                 ev->phy_updated.rx_phy, ev->phy_updated.status);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
    {
        uint16_t h = ev->disconnect.conn.conn_handle;
        ESP_LOGI(TAG, "central disconnected (handle %u, reason 0x%x)", h, ev->disconnect.reason);
        peer_t *p = peer_for_conn(h);
        if (!p) {
            ESP_LOGW(TAG, "disconnect of handle %u ignored: no peer holds it", h);
            break; /* a late event for an older connection must not drop a current one */
        }
        peer_gone(p);
        advertise();
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_tx_handle) {
            uint16_t h = ev->subscribe.conn_handle;
            peer_t *p = peer_for_conn(h);
            if (!p && ev->subscribe.cur_notify) {
                /* A connect reported as failed for a link that was up (see CONNECT): set it up now. */
                for (int i = 0; !p && i < BLE_PEERS; i++)
                    if (s_peers[i].conn == BLE_HS_CONN_HANDLE_NONE) p = &s_peers[i];
                if (!p) {
                    ESP_LOGW(TAG, "subscribe on handle %u: no free peer", h);
                    break;
                }
                ESP_LOGW(TAG, "subscribe on unknown handle %u: adopting it", h);
                peer_connected(p, h);
                advertise();
            }
            if (!p) break;
            p->subscribed = ev->subscribe.cur_notify;
            ESP_LOGI(TAG, "tx notify %s (handle %u, mtu %u)", p->subscribed ? "on" : "off", h, ble_att_mtu(h));
            report_state(p, p->subscribed);
        }
        break;
    case BLE_GAP_EVENT_CONN_UPDATE:
    {
        peer_t *p = peer_for_conn(ev->conn_update.conn_handle);
        if (!p) break;
        bool ours = p->req_pending;
        p->req_pending = false;
        if (ev->conn_update.status == 0) {
            conn_params(p, "updated", !ours && p->applied == ITVL_BASE); /* the central's own change: new base */
            apply_interval(p);                                          /* wanted something else meanwhile */
        } else {
            ESP_LOGW(TAG, "interval update failed (handle %u): %d", p->conn, ev->conn_update.status);
            if (ours) {
                bool bulk = p->applied == ITVL_BULK;
                p->applied = p->prev_applied;
                if (bulk) { /* refused: ask for what every central takes */
                    p->bulk_refused = true;
                    apply_interval(p);
                }
            }
        }
        break;
    }
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu (handle %u) = %u (fragments up to %u B)", ev->mtu.conn_handle, ev->mtu.value, ev->mtu.value - 3);
        break;
    case BLE_GAP_EVENT_DATA_LEN_CHG:
        ESP_LOGI(TAG, "data length (handle %u): tx %u B / %u us, rx %u B / %u us", ev->data_len_chg.conn_handle,
                 ev->data_len_chg.max_tx_octets, ev->data_len_chg.max_tx_time, ev->data_len_chg.max_rx_octets,
                 ev->data_len_chg.max_rx_time);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    default: break;
    }
    return 0;
}

/* Advertises while a peer is free: every 100–150 ms with no central, so the first one finds the watch at
 * once; every 500–750 ms while one is linked, as the second machine can wait a second (MULTI-LINK.md §3).
 * Stops when all peers are taken. Host task only. */
static int8_t s_adv_mode = -1; /* -1 off, 0 fast, 1 slow: what runs */

static void advertise(void)
{
    int linked = peers_linked();
    int8_t want = linked >= BLE_PEERS ? -1 : linked ? 1 : 0;
    bool on = ble_gap_adv_active();
    if (on && want == s_adv_mode) return;
    if (on) ble_gap_adv_stop();
    s_adv_mode = -1;
    if (want < 0) {
        ESP_LOGI(TAG, "advertising off: %d centrals linked", linked);
        return;
    }
    struct ble_hs_adv_fields adv = {};
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128 = &kSvcUuid;
    adv.num_uuids128 = 1;
    adv.uuids128_is_complete = 1;
    struct ble_hs_adv_fields rsp = {};
    rsp.name = (const uint8_t *)s_name;
    rsp.name_len = (uint8_t)strlen(s_name);
    rsp.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&adv);
    if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields rc=%d", rc);
        return;
    }
    struct ble_gap_adv_params a = {};
    a.conn_mode = BLE_GAP_CONN_MODE_UND;
    a.disc_mode = BLE_GAP_DISC_MODE_GEN;
    a.itvl_min = want ? BLE_GAP_ADV_ITVL_MS(500) : BLE_GAP_ADV_ITVL_MS(100);
    a.itvl_max = want ? BLE_GAP_ADV_ITVL_MS(750) : BLE_GAP_ADV_ITVL_MS(150);
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &a, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv start rc=%d", rc);
        return;
    }
    s_adv_mode = want;
    ESP_LOGI(TAG, "advertising as %s (%s, %d linked)", s_name, want ? "slow" : "fast", linked);
}

/* ── GATT ── */

static int rx_access(uint16_t conn_handle, uint16_t, struct ble_gatt_access_ctxt *ctxt, void *)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t buf[BLE_ATT_MTU_MAX + 8];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof buf) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf, &len) != 0) return BLE_ATT_ERR_UNLIKELY;
    peer_t *p = peer_for_conn(conn_handle);
    if (!p) return BLE_ATT_ERR_UNLIKELY; /* a central that is not set up (yet) */
    if (len > p->rx_frag_max) {
        p->rx_frag_max = len;
        ESP_LOGI(TAG, "rx fragment %u B (handle %u, mtu %u)", len, conn_handle, ble_att_mtu(conn_handle));
    }
    rasm_feed(p, buf, len);
    return 0;
}

static int tx_access(uint16_t, uint16_t, struct ble_gatt_access_ctxt *, void *)
{
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static int str_access(uint16_t, uint16_t, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *s = (const char *)arg;
    return os_mbuf_append(ctxt->om, s, strlen(s)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* Battery Level (0x2A19): read, and notified to a subscribed central when the percent moves by 1 or more.
 * The value is the one the core task last read (hal_battery_read, at least once a minute); the host task
 * never touches the PM1 itself. */
static uint16_t s_bat_handle;
static volatile uint8_t s_bat_pct;

void sw_ble_battery_level(uint8_t percent)
{
    if (percent == s_bat_pct) return;
    s_bat_pct = percent;
    if (s_bat_handle && peers_linked()) ble_gatts_chr_updated(s_bat_handle); /* every subscribed central */
}

static int bat_access(uint16_t, uint16_t, struct ble_gatt_access_ctxt *ctxt, void *)
{
    uint8_t pct = s_bat_pct;
    return os_mbuf_append(ctxt->om, &pct, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const char kMfr[] = "M5Stack";
static const char kModel[] = "StopWatch AgentLink";
static const char kFw[] = "0.1";

static const ble_uuid16_t kInfoSvcUuid = BLE_UUID16_INIT(0x180A);
static const ble_uuid16_t kMfrUuid = BLE_UUID16_INIT(0x2A29);
static const ble_uuid16_t kModelUuid = BLE_UUID16_INIT(0x2A24);
static const ble_uuid16_t kFwUuid = BLE_UUID16_INIT(0x2A26);
static const ble_uuid16_t kBatSvcUuid = BLE_UUID16_INIT(0x180F);
static const ble_uuid16_t kBatLvlUuid = BLE_UUID16_INIT(0x2A19);

/* Member order matters: C++ requires designators in declaration order. */
static const struct ble_gatt_chr_def kLinkChrs[] = {
    {.uuid = &kRxUuid.u, .access_cb = rx_access, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP},
    {.uuid = &kTxUuid.u, .access_cb = tx_access, .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_tx_handle},
    {},
};
static const struct ble_gatt_chr_def kInfoChrs[] = {
    {.uuid = &kMfrUuid.u, .access_cb = str_access, .arg = (void *)kMfr, .flags = BLE_GATT_CHR_F_READ},
    {.uuid = &kModelUuid.u, .access_cb = str_access, .arg = (void *)kModel, .flags = BLE_GATT_CHR_F_READ},
    {.uuid = &kFwUuid.u, .access_cb = str_access, .arg = (void *)kFw, .flags = BLE_GATT_CHR_F_READ},
    {},
};
static const struct ble_gatt_chr_def kBatChrs[] = {
    {.uuid = &kBatLvlUuid.u, .access_cb = bat_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &s_bat_handle},
    {},
};

static const struct ble_gatt_svc_def kSvcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &kSvcUuid.u, .characteristics = kLinkChrs},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &kInfoSvcUuid.u, .characteristics = kInfoChrs},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &kBatSvcUuid.u, .characteristics = kBatChrs},
    {},
};

static void on_sync(void)
{
    uint8_t addr[6] = {};
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    ble_hs_id_copy_addr(s_own_addr_type, addr, NULL);
    snprintf(s_name, sizeof s_name, "AgentLink-%02X%02X", addr[1], addr[0]);
    ble_svc_gap_device_name_set(s_name);
    advertise();
}

static void on_reset(int reason) { ESP_LOGW(TAG, "host reset, reason %d", reason); }

static void host_task(void *)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void sw_ble_link_start(hal_link_rx_cb_t on_rx, hal_link_state_cb_t on_state)
{
    s_on_rx = on_rx;
    s_on_state = on_state;
    s_send_lock = xSemaphoreCreateMutex();
    for (int i = 0; i < BLE_PEERS; i++) {
        s_peers[i].conn = BLE_HS_CONN_HANDLE_NONE;
        s_peers[i].rasm = s_rasm_buf[i];
    }
    s_bat_pct = hal_battery_read().percent; /* the core task, before the host runs */
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(err));
        return;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_att_set_preferred_mtu(BLE_PREFERRED_MTU);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(kSvcs);
    if (rc == 0) rc = ble_gatts_add_svcs(kSvcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt init rc=%d", rc);
        return;
    }
    nimble_port_freertos_init(host_task);
}

/* Why a send failed, at most every 5 s: a silent failure here once cost a long hunt. */
static void send_failed(const char *why, int a, int b)
{
    static uint32_t last_ms;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (last_ms && now - last_ms < 5000) return;
    last_ms = now;
    ESP_LOGW(TAG, "send failed: %s (%d, %d)", why, a, b);
}

bool sw_ble_link_send(hal_link_kind_t port, const uint8_t *frame, size_t len)
{
    peer_t *p = peer_for_port(port);
    if (!p || !frame || len == 0 || len > BLE_MSG_MAX) return false;
    if (!s_send_lock || !p->subscribed || p->conn == BLE_HS_CONN_HANDLE_NONE) {
        send_failed("not linked (subscribed, conn)", p->subscribed, p->conn);
        return false;
    }
    xSemaphoreTake(s_send_lock, portMAX_DELAY);
    bool ok = true;
    uint16_t conn = p->conn;
    size_t chunk = ble_att_mtu(conn);
    chunk = chunk > 4 ? chunk - 4 : 0; /* ATT header (3) + fragment header (1) */
    if (chunk == 0) {
        send_failed("no ATT MTU for the tracked connection (conn, mtu)", conn, (int)ble_att_mtu(conn));
        ok = false;
    }
    size_t off = 0;
    uint8_t seq = 0;
    uint8_t pkt[BLE_ATT_MTU_MAX + 8];
    if (chunk > sizeof pkt - 1) chunk = sizeof pkt - 1;
    while (ok && off < len) {
        size_t n = len - off < chunk ? len - off : chunk;
        pkt[0] = (seq & BLE_HDR_SEQ) | (off == 0 ? BLE_HDR_FIRST : 0) | (off + n == len ? BLE_HDR_LAST : 0);
        memcpy(pkt + 1, frame + off, n);
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BLE_NOTIFY_TIMEOUT_MS);
        for (;;) {
            if (!p->subscribed || p->conn != conn) {
                send_failed("link changed mid-message (subscribed, conn)", p->subscribed, p->conn);
                ok = false;
                break;
            }
            struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, n + 1);
            int rc = om ? ble_gatts_notify_custom(conn, s_tx_handle, om) : BLE_HS_ENOMEM;
            if (rc == 0) break;
            if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY) {
                ESP_LOGW(TAG, "notify rc=%d", rc);
                ok = false;
                break;
            }
            if ((int32_t)(xTaskGetTickCount() - deadline) >= 0) {
                ESP_LOGW(TAG, "notify back-pressure timeout");
                ok = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(BLE_NOTIFY_RETRY_MS));
        }
        off += n;
        seq++;
    }
    xSemaphoreGive(s_send_lock);
    return ok;
}
