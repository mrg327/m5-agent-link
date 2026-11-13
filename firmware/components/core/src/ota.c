#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "feedback.h"
#include "hal.h"
#include "keys.h"
#include "link.h"
#include "ota.h"
#include "power.h"
#include "sha256.h"
#include "ui.h"
#include "ui_ota.h"
#include "ui_theme.h"
#include "voice.h"

typedef enum { OTA_IDLE, OTA_RECV, OTA_RESTART } ota_state_t;

static struct {
    ota_state_t st;
    uint32_t size, next, last_ms, restart_at;
    uint8_t sha[32];
    char ver[32];
    sha256_t ctx;
    bool waiting;
} s;
static bool s_unconfirmed;
static uint32_t s_boot_ms;

void ota_init(uint32_t now_ms)
{
    memset(&s, 0, sizeof s);
    s_boot_ms = now_ms;
    s_unconfirmed = hal_ota_unconfirmed();
    if (s_unconfirmed) dbg_event("ota", "ota_unconfirmed", NULL);
}

bool ota_active(void) { return s.st != OTA_IDLE; }

static unsigned pct(void) { return s.size ? (unsigned)((uint64_t)s.next * 100 / s.size) : 0; }

static void ack(int st, const char *msg)
{
    link_send_fw_ack(st, s.st == OTA_IDLE ? 0 : s.next, msg);
}

/* Ends the transfer (the slot is already dropped or finished) and gives the screen, the radio and the
 * display timer back. A reason the user should see goes out as a toast. */
static void close_transfer(const char *toast)
{
    if (s.st != OTA_IDLE) hal_link_interactive(false);
    s.st = OTA_IDLE;
    ui_ota_hide();
    if (toast && toast[0]) ui_toast(toast, RGB_AMBER);
}

static void stop(const char *why, const char *toast, bool tell)
{
    hal_ota_abort();
    dbg_event("ota", "ota_stopped", "\"why\":%s,\"next\":%u", dbg_q(why), (unsigned)s.next);
    if (tell) ack(5, NULL);
    close_transfer(toast);
}

static const char *err_text(const char *fallback)
{
    const char *e = hal_ota_error();
    return e && e[0] ? e : fallback;
}

/* The transfer failed: tell the daemon, drop the slot, show the reason. */
static void fail_transfer(int st, const char *msg)
{
    hal_ota_abort();
    dbg_event("ota", "ota_bad", "\"st\":%d,\"msg\":%s,\"next\":%u", st, dbg_q(msg), (unsigned)s.next);
    ack(st, msg);
    close_transfer(msg);
    feedback_reject();
}

void ota_on_begin(uint32_t size, const uint8_t *sha, size_t sha_len, const char *ver, size_t ver_len, uint32_t now_ms)
{
    if (size == 0) {
        if (s.st == OTA_RECV) {
            stop("daemon", NULL, true);
        } else {
            if (s.st == OTA_IDLE) hal_ota_abort();
            ack(5, NULL);
        }
        return;
    }
    if (sha_len != 32) {
        ack(3, "Bad update request");
        return;
    }
    if (s.st == OTA_RECV && size == s.size && !memcmp(sha, s.sha, 32)) { /* the daemon came back: carry on */
        s.last_ms = now_ms;
        s.waiting = false;
        dbg_event("ota", "ota_resume", "\"next\":%u,\"size\":%u", (unsigned)s.next, (unsigned)s.size);
        ui_ota_progress(pct(), false);
        ack(0, NULL);
        return;
    }
    const char *busy = NULL;
    if (s.st != OTA_IDLE) busy = "Already updating";
    else if (voice_state() == VOICE_REC || voice_state() == VOICE_WAIT) busy = "Recording a message";
    else if (keys_active()) busy = "Typing a message";
    else {
        const hal_board_info_t *bi = hal_board_info();
        hal_battery_t b = hal_battery_read();
        if (bi->has_battery && b.percent < OTA_MIN_BATTERY && !b.charging && !b.usb_present) busy = "Battery too low";
    }
    if (busy) {
        dbg_event("ota", "ota_busy", "\"msg\":%s", dbg_q(busy));
        ack(2, busy);
        return;
    }
    hal_ota_result_t r = hal_ota_begin(size);
    if (r != HAL_OTA_OK) {
        const char *msg = r == HAL_OTA_UNSUPPORTED ? "This watch can't update itself"
                          : r == HAL_OTA_BAD       ? err_text("Update refused")
                                                   : err_text("Flash error");
        hal_ota_abort();
        dbg_event("ota", "ota_bad", "\"st\":%d,\"msg\":%s,\"next\":0", r == HAL_OTA_FAIL ? 4 : 3, dbg_q(msg));
        ack(r == HAL_OTA_FAIL ? 4 : 3, msg);
        return;
    }
    memset(&s, 0, sizeof s);
    s.st = OTA_RECV;
    s.size = size;
    memcpy(s.sha, sha, 32);
    if (ver && ver_len) {
        size_t n = ver_len < sizeof s.ver - 1 ? ver_len : sizeof s.ver - 1;
        memcpy(s.ver, ver, n);
    }
    s.last_ms = now_ms;
    s.waiting = !link_welcomed();
    sha256_init(&s.ctx);
    hal_link_interactive(true);
    ui_ota_show(s.ver);
    ui_ota_progress(0, false);
    feedback_tick();
    dbg_event("ota", "ota_begin", "\"size\":%u,\"ver\":%s", (unsigned)size, dbg_q(s.ver));
    ack(0, NULL);
}

static void finish(uint32_t now_ms)
{
    uint8_t got[32];
    sha256_final(&s.ctx, got);
    if (memcmp(got, s.sha, 32)) {
        fail_transfer(3, "Checksum mismatch");
        return;
    }
    hal_ota_result_t r = hal_ota_finish();
    if (r != HAL_OTA_OK) {
        fail_transfer(r == HAL_OTA_FAIL ? 4 : 3, r == HAL_OTA_FAIL ? err_text("Flash error") : err_text("Update refused"));
        return;
    }
    dbg_event("ota", "ota_done", "\"size\":%u,\"ver\":%s", (unsigned)s.size, dbg_q(s.ver));
    ack(1, NULL);
    s.st = OTA_RESTART;
    s.restart_at = now_ms + OTA_RESTART_DELAY_MS;
    ui_ota_restarting();
}

void ota_on_chunk(uint32_t off, const uint8_t *data, size_t len, uint32_t now_ms)
{
    if (s.st != OTA_RECV) {
        if (s.st == OTA_IDLE) ack(5, NULL);
        return;
    }
    s.last_ms = now_ms;
    if (off != s.next) { /* a duplicate or a gap: not written; the daemon resends from `next` */
        dbg_event("ota", "ota_gap", "\"off\":%u,\"next\":%u", (unsigned)off, (unsigned)s.next);
        ack(0, NULL);
        return;
    }
    if (len == 0) {
        ack(0, NULL);
        return;
    }
    if (len > s.size - s.next) {
        fail_transfer(3, "Too much data");
        return;
    }
    hal_ota_result_t r = hal_ota_write(data, len);
    if (r != HAL_OTA_OK) {
        fail_transfer(r == HAL_OTA_FAIL ? 4 : 3, r == HAL_OTA_FAIL ? err_text("Flash error") : err_text("Update refused"));
        return;
    }
    sha256_update(&s.ctx, data, len);
    s.next += (uint32_t)len;
    if (s.next == s.size) {
        finish(now_ms);
        return;
    }
    ui_ota_progress(pct(), s.waiting);
    ack(0, NULL);
}

void ota_on_link(bool welcomed, uint32_t now_ms)
{
    (void)now_ms;
    if (welcomed && s_unconfirmed) {
        hal_ota_confirm();
        s_unconfirmed = false;
        dbg_event("ota", "ota_confirm", "\"after_ms\":%u", (unsigned)(now_ms - s_boot_ms));
        /* Only here: a power-on or USB flash boots a confirmed image, a re-welcome finds s_unconfirmed cleared. */
        char msg[40];
        snprintf(msg, sizeof msg, "Updated to %s", hal_board_info()->fw_version);
        dbg_event("ota", "ota_updated_tone", "\"fw\":%s", dbg_q(hal_board_info()->fw_version));
        ui_toast(msg, RGB_SUCCESS);
        feedback_updated();
    }
    if (s.st == OTA_RECV) {
        s.waiting = !welcomed;
        ui_ota_progress(pct(), s.waiting);
    }
}

void ota_cancel(uint32_t now_ms)
{
    (void)now_ms;
    if (s.st != OTA_RECV) return;
    stop("user", "Update cancelled", true);
}

void ota_tick(uint32_t now_ms)
{
    if (s.st == OTA_RECV && now_ms - s.last_ms >= OTA_IDLE_TIMEOUT_MS) stop("timeout", "Update timed out", true);
    if (s.st == OTA_RESTART && (int32_t)(now_ms - s.restart_at) >= 0) {
        hal_restart(); /* does not return on the watch */
        close_transfer(NULL);
    }
    if (s_unconfirmed && now_ms - s_boot_ms >= OTA_CONFIRM_MS) {
        s_unconfirmed = false;
        dbg_event("ota", "ota_rollback", NULL);
        hal_restart(); /* the bootloader boots the previous image: this one was never confirmed */
    }
}
