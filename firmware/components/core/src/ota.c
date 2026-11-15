#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "detools.h"
#include "feedback.h"
#include "hal.h"
#include "keys.h"
#include "link.h"
#include "lvgl.h"
#include "model.h"
#include "ota.h"
#include "power.h"
#include "sha256.h"
#include "ui.h"
#include "ui_ota.h"
#include "ui_theme.h"
#include "voice.h"

typedef enum { OTA_IDLE, OTA_RECV, OTA_RESTART } ota_state_t;

#define OTA_BUF 4096 /* base hashing reads, then the rebuilt image batched into flash writes */
/* Delta updates (issue 203f): one 2 KB patch chunk can rebuild most of the image, many seconds of flash
 * writes. So the patch is kept whole in PSRAM and acked as it arrives, like an image, and the decoder gets it
 * OTA_FEED bytes at a time for about OTA_SLICE_MS per main-loop pass, which keeps the screen and the buttons
 * running. Once the whole patch is in, a `go` ack repeating `next` every OTA_KEEPALIVE_MS keeps the daemon's
 * 10 s ack timeout from firing while the rest is rebuilt (with nothing unanswered it changes nothing else). */
#define OTA_SLICE_MS 40 /* each 4 KB flash write takes ~35 ms; a pass costs ~25 ms more, so take a few per pass */
#define OTA_FEED 16 /* a heatshrink back-reference rebuilds at most 1 KB: one call is a few flash writes at most */
#define OTA_KEEPALIVE_MS 3000
#define OTA_PATCH_MAX (1024u * 1024u) /* a bigger patch is refused; the daemon then sends the image */
#define OTA_PATCH_MIN_ALLOC (32u * 1024u) /* above CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: lands in PSRAM */

static struct {
    ota_state_t st;
    uint32_t size, next, last_ms, restart_at, begin_ms;
    hal_link_kind_t port; /* the port whose interval went to bulk */
    uint8_t sha[32];
    char ver[32];
    sha256_t ctx;
    bool waiting;
    /* delta update: psize 0 = a whole image; `next` then counts patch bytes received, `applied` decoded */
    uint32_t psize, bsize, out, fill, applied, keep_ms;
    uint8_t base[32];
    int64_t boff;               /* the decoder's position in the base (it seeks relative to it) */
    hal_ota_result_t werr;      /* a flash write the decoder's output hit */
    bool overflow;              /* the patch rebuilt more than `size` bytes */
    uint32_t step_max_ms, steps; /* the longest decoder step, how many there were (ota_done) */
} s;
/* PSRAM: the decoder (~2.3 KB, mostly heatshrink's 2 KB window) and the buffer stay off internal RAM. */
MODEL_EXT_BSS static struct detools_apply_patch_t s_dt;
MODEL_EXT_BSS static uint8_t s_buf[OTA_BUF];
static uint8_t *s_patch; /* the patch received so far (malloc, PSRAM); outside `s`, which begin clears */
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

static uint32_t total(void) { return s.psize ? s.psize : s.size; }

/* A patch's progress is the image rebuilt so far: patch bytes say little about the work left. */
static unsigned pct(void)
{
    if (s.psize) return s.size ? (unsigned)((uint64_t)s.out * 100 / s.size) : 0;
    return s.size ? (unsigned)((uint64_t)s.next * 100 / s.size) : 0;
}

static void ack(int st, const char *msg)
{
    link_send_fw_ack(st, s.st == OTA_IDLE ? 0 : s.next, msg);
}

static void free_patch(void)
{
    free(s_patch);
    s_patch = NULL;
}

/* Ends the transfer (the slot is already dropped or finished) and gives the screen, the radio and the
 * display timer back. A reason the user should see goes out as a toast. */
static void close_transfer(const char *toast)
{
    if (s.st != OTA_IDLE) hal_link_bulk(s.port, false);
    s.st = OTA_IDLE;
    free_patch();
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

/* The first `p->bsize` bytes of the running slot hash to `p->base`. Blocks the main loop while it reads
 * them (estimated well under a second for a 1.3 MB image; `hash_ms` in ota_begin). */
static bool base_matches(const ota_patch_t *p)
{
    if (p->bsize == 0 || p->bsize > hal_ota_base_size()) return false;
    sha256_t c;
    sha256_init(&c);
    for (uint32_t off = 0; off < p->bsize;) {
        size_t n = p->bsize - off < OTA_BUF ? p->bsize - off : OTA_BUF;
        if (hal_ota_base_read(off, s_buf, n) != HAL_OTA_OK) return false;
        sha256_update(&c, s_buf, n);
        off += (uint32_t)n;
    }
    uint8_t got[32];
    sha256_final(&c, got);
    return !memcmp(got, p->base, 32);
}

/* detools callbacks: the base is read where the decoder says, the image goes out in OTA_BUF pieces. */
static int base_read(void *arg, uint8_t *buf, size_t len)
{
    (void)arg;
    if (s.boff < 0 || (uint64_t)s.boff + len > s.bsize) return -1;
    if (hal_ota_base_read((uint32_t)s.boff, buf, len) != HAL_OTA_OK) return -1;
    s.boff += (int64_t)len;
    return 0;
}

static int base_seek(void *arg, int off)
{
    (void)arg;
    s.boff += off;
    return 0;
}

static bool flush_image(void)
{
    if (!s.fill) return true;
    hal_ota_result_t r = hal_ota_write(s_buf, s.fill);
    if (r != HAL_OTA_OK) {
        s.werr = r;
        return false;
    }
    sha256_update(&s.ctx, s_buf, s.fill);
    s.fill = 0;
    return true;
}

static int image_write(void *arg, const uint8_t *data, size_t len)
{
    (void)arg;
    if (len > s.size - s.out) {
        s.overflow = true;
        return -1;
    }
    s.out += (uint32_t)len;
    while (len) {
        size_t n = OTA_BUF - s.fill < len ? OTA_BUF - s.fill : len;
        memcpy(s_buf + s.fill, data, n);
        s.fill += (uint32_t)n;
        data += n;
        len -= n;
        if (s.fill == OTA_BUF && !flush_image()) return -1;
    }
    return 0;
}

static void patch_failed(int err)
{
    dbg_event("ota", "ota_patch_error", "\"err\":%d,\"out\":%u", err, (unsigned)s.out);
    if (s.werr != HAL_OTA_OK)
        fail_transfer(s.werr == HAL_OTA_FAIL ? 4 : 3,
                      s.werr == HAL_OTA_FAIL ? err_text("Flash error") : err_text("Update refused"));
    else fail_transfer(3, s.overflow ? "Too much data" : "Bad patch");
}

static bool same_patch(const ota_patch_t *p)
{
    if (!p) return s.psize == 0;
    return s.psize == p->psize && s.bsize == p->bsize && !memcmp(s.base, p->base, 32);
}

void ota_on_begin(uint32_t size, const uint8_t *sha, size_t sha_len, const char *ver, size_t ver_len,
                  const ota_patch_t *patch, uint32_t now_ms)
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
    if (sha_len != 32 || (patch && (patch->base_len != 32 || patch->psize == 0))) {
        ack(3, "Bad update request");
        return;
    }
    if (s.st == OTA_RECV && size == s.size && !memcmp(sha, s.sha, 32) && same_patch(patch)) { /* the daemon came back: carry on */
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
    uint32_t hash_ms = 0;
    if (patch) {
        uint32_t t0 = lv_tick_get();
        bool ok = base_matches(patch);
        hash_ms = lv_tick_get() - t0;
        if (!ok) {
            dbg_event("ota", "ota_base_mismatch", "\"bsize\":%u,\"have\":%u,\"ms\":%u", (unsigned)patch->bsize,
                      (unsigned)hal_ota_base_size(), (unsigned)hash_ms);
            ack(3, "Base mismatch");
            return;
        }
    }
    if (patch) {
        free_patch();
        if (patch->psize <= OTA_PATCH_MAX)
            s_patch = malloc(patch->psize > OTA_PATCH_MIN_ALLOC ? patch->psize : OTA_PATCH_MIN_ALLOC);
        if (!s_patch) {
            dbg_event("ota", "ota_bad", "\"st\":3,\"msg\":\"Patch too big\",\"psize\":%u", (unsigned)patch->psize);
            ack(3, "Patch too big");
            return;
        }
    }
    hal_ota_result_t r = hal_ota_begin(size);
    if (r != HAL_OTA_OK) {
        const char *msg = r == HAL_OTA_UNSUPPORTED ? "This watch can't update itself"
                          : r == HAL_OTA_BAD       ? err_text("Update refused")
                                                   : err_text("Flash error");
        hal_ota_abort();
        free_patch();
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
    s.last_ms = s.begin_ms = now_ms;
    s.waiting = !link_welcomed();
    sha256_init(&s.ctx);
    if (patch) {
        s.psize = patch->psize;
        s.bsize = patch->bsize;
        memcpy(s.base, patch->base, 32);
        detools_apply_patch_init(&s_dt, base_read, base_seek, s.psize, image_write, NULL);
    }
    s.port = link_active_kind();
    hal_link_bulk(s.port, true);
    ui_ota_show(s.ver);
    ui_ota_progress(0, false);
    feedback_tick();
    if (patch)
        dbg_event("ota", "ota_begin", "\"size\":%u,\"ver\":%s,\"patch\":%u,\"hash_ms\":%u", (unsigned)size,
                  dbg_q(s.ver), (unsigned)s.psize, (unsigned)hash_ms);
    else dbg_event("ota", "ota_begin", "\"size\":%u,\"ver\":%s", (unsigned)size, dbg_q(s.ver));
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
    if (s.psize)
        dbg_event("ota", "ota_done", "\"size\":%u,\"ver\":%s,\"ms\":%u,\"steps\":%u,\"step_max_ms\":%u",
                  (unsigned)s.size, dbg_q(s.ver), (unsigned)(now_ms - s.begin_ms), (unsigned)s.steps,
                  (unsigned)s.step_max_ms);
    else dbg_event("ota", "ota_done", "\"size\":%u,\"ver\":%s,\"ms\":%u", (unsigned)s.size, dbg_q(s.ver),
                   (unsigned)(now_ms - s.begin_ms));
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
    if (len > total() - s.next) {
        fail_transfer(3, "Too much data");
        return;
    }
    if (s.psize) { /* kept; ota_tick decodes it */
        memcpy(s_patch + s.next, data, len);
        s.next += (uint32_t)len;
        s.keep_ms = now_ms;
        ack(0, NULL);
        return;
    }
    hal_ota_result_t r = hal_ota_write(data, len);
    if (r != HAL_OTA_OK) {
        fail_transfer(r == HAL_OTA_FAIL ? 4 : 3,
                      r == HAL_OTA_FAIL ? err_text("Flash error") : err_text("Update refused"));
        return;
    }
    sha256_update(&s.ctx, data, len);
    s.next += (uint32_t)len;
    if (s.next == total()) {
        finish(now_ms);
        return;
    }
    ui_ota_progress(pct(), s.waiting);
    ack(0, NULL);
}

static void note_step(uint32_t t0)
{
    uint32_t ms = lv_tick_get() - t0;
    s.steps++;
    if (ms > s.step_max_ms) s.step_max_ms = ms;
}

/* The last patch byte is in: the decoder's tail, the last flash write, then the image checks. */
static void finish_patch(uint32_t t0, uint32_t now_ms)
{
    int res = detools_apply_patch_finalize(&s_dt);
    if (res < 0 || !flush_image()) {
        patch_failed(res < 0 ? res : -DETOOLS_IO_FAILED);
        return;
    }
    if ((uint32_t)res != s.size || s.out != s.size) {
        dbg_event("ota", "ota_patch_error", "\"err\":0,\"out\":%u", (unsigned)s.out);
        fail_transfer(3, "Bad patch");
        return;
    }
    note_step(t0);
    finish(now_ms);
}

/* Feeds received patch bytes to the decoder for about OTA_SLICE_MS. */
static void apply_step(uint32_t now_ms)
{
    if (!ota_working()) return;
    uint32_t t0 = lv_tick_get();
    do {
        uint32_t n = s.next - s.applied < OTA_FEED ? s.next - s.applied : OTA_FEED;
        int res = detools_apply_patch_process(&s_dt, s_patch + s.applied, n);
        if (res < 0) {
            patch_failed(res);
            return;
        }
        s.applied += n;
        if (s.applied == s.psize) {
            finish_patch(t0, now_ms);
            return;
        }
    } while (s.applied < s.next && lv_tick_get() - t0 < OTA_SLICE_MS);
    note_step(t0);
    s.last_ms = now_ms; /* still working: not idle */
    /* the whole patch is in and acked: nothing is unanswered, so this only tells the daemon we are alive */
    if (s.next == s.psize && now_ms - s.keep_ms >= OTA_KEEPALIVE_MS) {
        s.keep_ms = now_ms;
        dbg_event("ota", "ota_keepalive", "\"out\":%u", (unsigned)s.out);
        ack(0, NULL);
    }
    ui_ota_progress(pct(), s.waiting);
}

bool ota_working(void) { return s.st == OTA_RECV && s.psize && s.applied < s.next; }

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
    apply_step(now_ms);
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
