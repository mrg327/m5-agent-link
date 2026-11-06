// NimBLE GATT peripheral link (PROTOCOL §1: rx/tx characteristics, 1-byte fragment header).
//
// Threading: NimBLE callbacks (on_rx / on_state) run on the NimBLE host task. core's app.c
// copies them into spinlock-protected rings that the core task drains, so they are safe to
// call from here directly. sw_ble_link_send() runs on the core task.
#include <stdio.h>
#include <string.h>

#include "board_priv.h"
#include "esp_log.h"
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
#define BLE_PREFERRED_MTU 247
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

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle;
            s_subscribed = false;
            ESP_LOGI(TAG, "central connected (handle %u)", ev->connect.conn_handle);
            ble_gap_set_prefered_le_phy(s_conn, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK, 0);
        } else {
            ESP_LOGW(TAG, "connect failed: %d", ev->connect.status);
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "central disconnected (reason 0x%x)", ev->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_subscribed = false;
        report_state(false);
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_tx_handle) {
            s_subscribed = ev->subscribe.cur_notify;
            ESP_LOGI(TAG, "tx notify %s (mtu %u)", s_subscribed ? "on" : "off",
                     ble_att_mtu(ev->subscribe.conn_handle));
            report_state(s_subscribed);
        }
        break;
    case BLE_GAP_EVENT_MTU: ESP_LOGI(TAG, "mtu = %u", ev->mtu.value); break;
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

static int rx_access(uint16_t, uint16_t, struct ble_gatt_access_ctxt *ctxt, void *)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t buf[BLE_ATT_MTU_MAX + 8];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof buf) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf, &len) != 0) return BLE_ATT_ERR_UNLIKELY;
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

static int bat_access(uint16_t, uint16_t, struct ble_gatt_access_ctxt *ctxt, void *)
{
    uint8_t pct = hal_battery_read().percent;
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
    {.uuid = &kBatLvlUuid.u, .access_cb = bat_access, .flags = BLE_GATT_CHR_F_READ},
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

bool sw_ble_link_send(const uint8_t *frame, size_t len)
{
    if (!frame || len == 0 || len > BLE_MSG_MAX) return false;
    if (!s_send_lock || !s_subscribed || s_conn == BLE_HS_CONN_HANDLE_NONE) return false;
    xSemaphoreTake(s_send_lock, portMAX_DELAY);
    bool ok = true;
    uint16_t conn = s_conn;
    size_t chunk = ble_att_mtu(conn);
    chunk = chunk > 4 ? chunk - 4 : 0; /* ATT header (3) + fragment header (1) */
    if (chunk == 0) ok = false;
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
