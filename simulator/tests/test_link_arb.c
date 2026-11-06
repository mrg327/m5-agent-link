/* Link arbitration: hello on every up transport, first welcome wins, fallback when it drops.
 * Builds link*.c and model.c against stubbed HAL/debug; no simulator needed. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "link.h"
#include "model.h"

/* welcome (id 1) copied from scripts/link_indicator.txt */
static const char *WELCOME_HEX =
    "a400020101020103a40082000101a30048a1a2a3a4a5a6a7a801676465762d6d6163020002a2001a6aee31c0013901df03a4000101000318a00465406f63746f";

static int sent[3], hellos[3], links_up, links_down;
static const hal_board_info_t s_bi = {.board = "sim", .fw_version = "t"};

const hal_board_info_t *hal_board_info(void) { return &s_bi; }
hal_battery_t hal_battery_read(void) { hal_battery_t b = {0}; return b; }
void hal_rtc_set_unix(uint32_t s) { (void)s; }
void hal_display_set_brightness(uint8_t l) { (void)l; }
bool hal_link_send(hal_link_kind_t k, const uint8_t *f, size_t n)
{
    sent[k]++;
    if (n > 3 && f[2] == 0x01) hellos[k]++; /* envelope type 1 = hello */
    return true;
}
void dbg_event(const char *c, const char *e, const char *f, ...) { (void)c; (void)e; (void)f; }
void dbg_event_at(dbg_level_t l, const char *c, const char *e, const char *f, ...) { (void)l; (void)c; (void)e; (void)f; }
const char *dbg_q(const char *s) { return s; }
void dbg_set_level(dbg_level_t l) { (void)l; }
void dbg_on_ui_state_req(uint32_t r) { (void)r; }
void dbg_on_screenshot_req(uint32_t r) { (void)r; }
void dbg_on_inject(int e, bool h, int x, int y) { (void)e; (void)h; (void)x; (void)y; }

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

static void on_link(bool w) { w ? links_up++ : links_down++; }

static void rx_welcome(hal_link_kind_t k)
{
    uint8_t buf[256];
    size_t n = 0;
    for (const char *h = WELCOME_HEX; h[0]; h += 2, n++) {
        unsigned v;
        sscanf(h, "%2x", &v);
        buf[n] = (uint8_t)v;
    }
    link_on_frame_from(k, buf, n);
}

static void reset(void)
{
    memset(sent, 0, sizeof sent);
    memset(hellos, 0, sizeof hellos);
    links_up = links_down = 0;
    link_hooks_t h = {.on_link = on_link};
    link_init(&h);
    link_tick(0);
}

int main(void)
{
    reset();
    link_on_state(HAL_LINK_USB, true);
    link_on_state(HAL_LINK_BLE, true);
    EXPECT(hellos[HAL_LINK_USB] == 1 && hellos[HAL_LINK_BLE] == 1, "hello on every up transport");
    link_tick(3000);
    EXPECT(hellos[HAL_LINK_USB] == 2 && hellos[HAL_LINK_BLE] == 2, "retry on each transport");

    /* USB is silent (no daemon), BLE answers */
    rx_welcome(HAL_LINK_BLE);
    EXPECT(link_welcomed() && link_active_kind() == HAL_LINK_BLE && links_up == 1, "BLE welcome wins");
    int usb_before = sent[HAL_LINK_USB], ble_before = sent[HAL_LINK_BLE];
    link_tick(6000);
    EXPECT(sent[HAL_LINK_BLE] == ble_before && sent[HAL_LINK_USB] == usb_before, "no more hellos once welcomed");
    EXPECT(link_send_cmd(LINK_OP_FOCUS, 1, 0, 0) && sent[HAL_LINK_BLE] == ble_before + 1 &&
               sent[HAL_LINK_USB] == usb_before,
           "commands go to the active transport only");

    rx_welcome(HAL_LINK_USB); /* ignored while BLE is active */
    EXPECT(link_active_kind() == HAL_LINK_BLE && links_up == 1, "frames from other transport ignored");

    int h0 = hellos[HAL_LINK_USB];
    link_on_state(HAL_LINK_BLE, false);
    EXPECT(!link_welcomed() && link_connected() && links_down == 1, "active down -> unwelcomed, still connected");
    EXPECT(hellos[HAL_LINK_USB] == h0 + 1, "fallback restarts hello on remaining transport");
    EXPECT(!link_send_cmd(LINK_OP_FOCUS, 1, 0, 0), "no commands before welcome");
    rx_welcome(HAL_LINK_USB);
    EXPECT(link_welcomed() && link_active_kind() == HAL_LINK_USB && links_up == 2, "USB welcome after fallback");

    link_on_state(HAL_LINK_USB, false);
    EXPECT(!link_connected() && !link_welcomed() && links_down == 2, "all down");

    reset();
    link_on_state(HAL_LINK_USB, true);
    link_on_state(HAL_LINK_WS, true);
    EXPECT(hellos[HAL_LINK_WS] == 1, "late transport gets hello");
    link_on_state(HAL_LINK_USB, false);
    EXPECT(link_connected() && !link_welcomed() && links_down == 0, "non-active down while unwelcomed is silent");

    reset();
    link_on_state(HAL_LINK_WS, true);
    rx_welcome(HAL_LINK_WS);
    link_on_state(HAL_LINK_USB, true);
    EXPECT(hellos[HAL_LINK_USB] == 0 && link_active_kind() == HAL_LINK_WS, "no hello to extra transport while welcomed");

    if (failures) return EXIT_FAILURE;
    puts("link arbitration OK");
    return 0;
}
