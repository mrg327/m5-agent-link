// NimBLE GATT peripheral link (PROTOCOL §1: rx/tx characteristics, 1-byte fragment header).
//
// Threading: NimBLE callbacks (on_rx / on_state) run on the NimBLE host task. core's app.c
// copies them into spinlock-protected rings that the core task drains, so they are safe to
// call from here directly. sw_ble_link_send() runs on the core task.
#include <stdio.h>
#include <string.h>

#include "board_priv.h"
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
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_subscribed;
static volatile bool s_up; /* last state reported to the core */

/* ── reassembler (host task only) ── */

static uint8_t s_rasm[BLE_MSG_MAX];
static size_t s_rasm_len;
static bool s_rasm_active;
static uint8_t s_rasm_next;
static uint16_t s_rx_frag_max; /* largest write this connection, logged when it grows */

static void rasm_reset(void)
{
    s_rasm_active = false;
    s_rasm_len = 0;
}

static void rasm_feed(const uint8_t *p, size_t n)
{
    if (n < 1) return;
    uint8_t hdr = p[0];
    uint8_t seq = hdr & BLE_HDR_SEQ;
    const uint8_t *data = p + 1;
    size_t dlen = n - 1;

    if (hdr & BLE_HDR_FIRST) {
        if (s_rasm_active) ESP_LOGW(TAG, "new FIRST mid-message, dropping partial (%u B)", (unsigned)s_rasm_len);
        rasm_reset();
        s_rasm_active = true;
    } else if (!s_rasm_active) {
        return; /* continuation of a dropped message */
    } else if (seq != s_rasm_next) {
        ESP_LOGW(TAG, "seq gap (got %u want %u), dropping message", seq, s_rasm_next);
        rasm_reset();
        return;
    }
    s_rasm_next = (seq + 1) & BLE_HDR_SEQ;
    if (s_rasm_len + dlen > BLE_MSG_MAX) {
        ESP_LOGW(TAG, "message exceeds %d B, dropped", BLE_MSG_MAX);
        rasm_reset();
        return;
    }
    memcpy(s_rasm + s_rasm_len, data, dlen);
    s_rasm_len += dlen;
    if (hdr & BLE_HDR_LAST) {
        size_t len = s_rasm_len;
        rasm_reset();
        if (len > 0 && s_on_rx) s_on_rx(HAL_LINK_BLE, s_rasm, len);
    }
}

/* ── link state ── */

static void report_state(bool up)
{
    if (s_up == up) return;
    s_up = up;
    if (!up) rasm_reset();
    if (s_on_state) s_on_state(HAL_LINK_BLE, up);
}

static void advertise(void);

/* ── connection interval: the central's choice, 15–30 ms while interactive (DESIGN §7), or as short as
 * the central allows during a bulk transfer (an update, FIRMWARE.md §9) ── */

enum { ITVL_BASE, ITVL_FAST, ITVL_BULK };
#define BLE_FAST_ITVL_MIN 12  /* 1.25 ms units: 15 ms, Apple's floor */
#define BLE_FAST_ITVL_MAX 24  /* 30 ms; Apple wants max ≥ min + 15 ms */
/* A central that cannot do long LL packets (no data length extension, e.g. the BCM4350C0) moves a few
 * 27-byte packets per connection event, so the interval sets the rate: ~3 KB/s at 30 ms, ~13 KB/s at
 * 7.5 ms. A central that refuses this gets the fast request instead. */
#define BLE_BULK_ITVL_MIN 6   /* 7.5 ms, the minimum */
#define BLE_BULK_ITVL_MAX 6
#define BLE_FAST_TIMEOUT 400  /* 10 ms units: 4 s */
#define BLE_IDLE_ITVL 24      /* 30 ms (to 45 ms): the base after a connect at a remembered bulk interval */
static uint16_t s_base_itvl, s_base_latency, s_base_timeout;
static volatile uint16_t s_cur_itvl; /* the interval in force (1.25 ms units), 0 = no central: power report */
static bool s_fast, s_bulk;      /* wanted */
static uint8_t s_applied;        /* ITVL_*: last granted (or, while s_req_pending, requested) */
static uint8_t s_prev_applied;   /* before the pending request */
static bool s_bulk_refused;      /* this central refused the bulk interval */
static bool s_req_pending;       /* one update at a time; the next is sent when it completes */

static void conn_params(const char *why, bool save_base)
{
    struct ble_gap_conn_desc d;
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_find(s_conn, &d) != 0) return;
    ESP_LOGI(TAG, "%s: interval %u.%02u ms, latency %u, timeout %u ms", why, d.conn_itvl * 125 / 100,
             d.conn_itvl * 125 % 100, d.conn_latency, d.supervision_timeout * 10);
    s_cur_itvl = d.conn_itvl;
    if (save_base) {
        s_base_itvl = d.conn_itvl;
        s_base_latency = d.conn_latency;
        s_base_timeout = d.supervision_timeout;
    }
}

static uint8_t wanted_level(void)
{
    uint8_t lv = s_bulk && !s_bulk_refused ? ITVL_BULK : (s_fast || s_bulk) ? ITVL_FAST : ITVL_BASE;
    uint16_t min = lv == ITVL_BULK ? BLE_BULK_ITVL_MIN : BLE_FAST_ITVL_MIN;
    if (lv == ITVL_BULK && s_base_itvl <= min) lv = ITVL_FAST; /* the central already runs this fast */
    if (lv == ITVL_FAST && s_base_itvl <= BLE_FAST_ITVL_MIN) lv = ITVL_BASE;
    return lv;
}

/* Asks the central for the interval that is wanted now, if that is not the one it runs. */
static void apply_interval(void)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_base_itvl || s_req_pending) return;
    uint8_t lv = wanted_level();
    if (lv == s_applied) return;
    static const char *const kName[] = {"base", "fast", "bulk"};
    struct ble_gap_upd_params p = {};
    p.itvl_min = lv == ITVL_BULK ? BLE_BULK_ITVL_MIN : lv == ITVL_FAST ? BLE_FAST_ITVL_MIN : s_base_itvl;
    p.itvl_max = lv == ITVL_BULK ? BLE_BULK_ITVL_MAX : lv == ITVL_FAST ? BLE_FAST_ITVL_MAX : (uint16_t)(s_base_itvl + 12);
    p.latency = lv == ITVL_BASE ? s_base_latency : 0;
    p.supervision_timeout = lv == ITVL_BASE ? s_base_timeout : BLE_FAST_TIMEOUT;
    int rc = ble_gap_update_params(s_conn, &p);
    s_req_pending = rc == 0;
    if (rc == 0) {
        s_prev_applied = s_applied;
        s_applied = lv;
    }
    ESP_LOGI(TAG, "interval request %s (%u–%u units): rc=%d", kName[lv], p.itvl_min, p.itvl_max, rc);
}

void sw_ble_link_interactive(bool on)
{
    if (on == s_fast) return;
    s_fast = on;
    apply_interval();
}

uint32_t sw_ble_link_interval_us(void) { return s_conn == BLE_HS_CONN_HANDLE_NONE ? 0 : s_cur_itvl * 1250u; }

void sw_ble_link_bulk(bool on)
{
    if (on == s_bulk) return;
    s_bulk = on;
    apply_interval();
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
        bool up = ev->connect.status == 0 || (ev->connect.conn_handle != BLE_HS_CONN_HANDLE_NONE &&
                                              ble_gap_conn_find(ev->connect.conn_handle, &cd) == 0);
        if (up) {
            if (ev->connect.status != 0)
                ESP_LOGW(TAG, "connect status %d, but the link is up: using it", ev->connect.status);
            s_conn = ev->connect.conn_handle;
            s_subscribed = false;
            s_rx_frag_max = 0;
            ESP_LOGI(TAG, "central connected (handle %u)", ev->connect.conn_handle);
            ble_gap_set_prefered_le_phy(s_conn, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK, 0);
            ble_att_set_preferred_mtu(BLE_PREFERRED_MTU);
            if (BLE_DATA_LEN_OCTETS) {
                int rc = ble_gap_set_data_len(s_conn, BLE_DATA_LEN_OCTETS, BLE_DATA_LEN_TIME_US);
                if (rc != 0) {
                    /* No long LL packets (the central lacks data length extension): an ATT packet then
                     * goes in 27-byte LL packets, and MTU 247 (251 B with L2CAP) needs ten, the last one
                     * 8 B. MTU 239 fills nine exactly. The central exchanges the MTU right after this. */
                    ESP_LOGW(TAG, "data length request rc=%d: asking for MTU %d", rc, BLE_SHORT_LL_MTU);
                    ble_att_set_preferred_mtu(BLE_SHORT_LL_MTU);
                }
            }
            s_req_pending = s_bulk_refused = false;
            s_applied = ITVL_BASE;
            conn_params("connected", true);
            if (s_base_itvl && s_base_itvl < BLE_FAST_ITVL_MIN) {
                /* No central picks this on its own: BlueZ keeps the parameters a peripheral last asked
                 * for and connects with them, so an update that ended in a restart left the bulk
                 * interval behind. Ask for the usual 30–45 ms instead of staying there. */
                s_base_itvl = BLE_IDLE_ITVL;
                s_applied = ITVL_BULK; /* what the link runs: apply_interval() asks for the base */
            }
            apply_interval();
        } else {
            ESP_LOGW(TAG, "connect failed: %d", ev->connect.status);
            advertise();
        }
        break;
    }
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        ESP_LOGI(TAG, "phy: tx %u rx %u (status %d)", ev->phy_updated.tx_phy, ev->phy_updated.rx_phy,
                 ev->phy_updated.status);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "central disconnected (handle %u, reason 0x%x)", ev->disconnect.conn.conn_handle,
                 ev->disconnect.reason);
        if (s_conn != BLE_HS_CONN_HANDLE_NONE && ev->disconnect.conn.conn_handle != s_conn) {
            ESP_LOGW(TAG, "disconnect of handle %u ignored: the link is on %u", ev->disconnect.conn.conn_handle, s_conn);
            break; /* a late event for an older connection must not drop the current one */
        }
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_subscribed = false;
        s_base_itvl = 0;
        s_cur_itvl = 0;
        report_state(false);
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_tx_handle) {
            if (ev->subscribe.cur_notify && ev->subscribe.conn_handle != s_conn) {
                ESP_LOGW(TAG, "subscribe on handle %u while tracking %u: adopting it", ev->subscribe.conn_handle, s_conn);
                s_conn = ev->subscribe.conn_handle;
            }
            s_subscribed = ev->subscribe.cur_notify;
            ESP_LOGI(TAG, "tx notify %s (mtu %u)", s_subscribed ? "on" : "off",
                     ble_att_mtu(ev->subscribe.conn_handle));
            report_state(s_subscribed);
        }
        break;
    case BLE_GAP_EVENT_CONN_UPDATE:
    {
        bool ours = s_req_pending;
        s_req_pending = false;
        if (ev->conn_update.status == 0) {
            conn_params("updated", !ours && s_applied == ITVL_BASE); /* the central's own change: new base */
            apply_interval();                                       /* wanted something else meanwhile */
        } else {
            ESP_LOGW(TAG, "interval update failed: %d", ev->conn_update.status);
            if (ours) {
                bool bulk = s_applied == ITVL_BULK;
                s_applied = s_prev_applied;
                if (bulk) { /* refused: ask for what every central takes */
                    s_bulk_refused = true;
                    apply_interval();
                }
            }
        }
        break;
    }
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu = %u (fragments up to %u B)", ev->mtu.value, ev->mtu.value - 3);
        break;
    case BLE_GAP_EVENT_DATA_LEN_CHG:
        ESP_LOGI(TAG, "data length: tx %u B / %u us, rx %u B / %u us", ev->data_len_chg.max_tx_octets,
                 ev->data_len_chg.max_tx_time, ev->data_len_chg.max_rx_octets, ev->data_len_chg.max_rx_time);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    default: break;
    }
    return 0;
}

static void advertise(void)
{
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
    struct ble_gap_adv_params p = {};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    p.itvl_max = BLE_GAP_ADV_ITVL_MS(150);
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGE(TAG, "adv start rc=%d", rc);
    else ESP_LOGI(TAG, "advertising as %s", s_name);
}

/* ── GATT ── */

static int rx_access(uint16_t conn_handle, uint16_t, struct ble_gatt_access_ctxt *ctxt, void *)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t buf[BLE_ATT_MTU_MAX + 8];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof buf) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf, &len) != 0) return BLE_ATT_ERR_UNLIKELY;
    if (len > s_rx_frag_max) {
        s_rx_frag_max = len;
        ESP_LOGI(TAG, "rx fragment %u B (mtu %u)", len, ble_att_mtu(conn_handle));
    }
    rasm_feed(buf, len);
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
    if (s_bat_handle && s_conn != BLE_HS_CONN_HANDLE_NONE) ble_gatts_chr_updated(s_bat_handle);
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

bool sw_ble_link_send(const uint8_t *frame, size_t len)
{
    if (!frame || len == 0 || len > BLE_MSG_MAX) return false;
    if (!s_send_lock || !s_subscribed || s_conn == BLE_HS_CONN_HANDLE_NONE) {
        send_failed("not linked (subscribed, conn)", s_subscribed, s_conn);
        return false;
    }
    xSemaphoreTake(s_send_lock, portMAX_DELAY);
    bool ok = true;
    uint16_t conn = s_conn;
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
            if (!s_subscribed || s_conn != conn) {
                send_failed("link changed mid-message (subscribed, conn)", s_subscribed, s_conn);
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
