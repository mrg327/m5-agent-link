/* Firmware update, device side (PROTOCOL.md 6c, FIRMWARE.md 9): one transfer of a whole app image from the
 * daemon into the spare slot (hal_ota_*), checked against the SHA-256 it announced, then a restart. Also the
 * rollback guard: a new image keeps itself only once a daemon has welcomed it. Runs in core_step. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_IDLE_TIMEOUT_MS 30000 /* no chunk for this long: the transfer is dropped */
#define OTA_RESTART_DELAY_MS 1000 /* from the `done` ack to the restart */
#define OTA_CONFIRM_MS 300000     /* an unconfirmed image not welcomed by then restarts into the old one */
#define OTA_MIN_BATTERY 15        /* percent; below it, and not charging, the watch answers busy */

void ota_init(uint32_t now_ms);
void ota_tick(uint32_t now_ms);
/* The Updating overlay owns the screen and the buttons (a transfer is open, or the restart is pending). */
bool ota_active(void);
/* fw_begin; size 0 stops. `sha` is `sha_len` bytes (32 expected), `ver` may be NULL. */
void ota_on_begin(uint32_t size, const uint8_t *sha, size_t sha_len, const char *ver, size_t ver_len, uint32_t now_ms);
void ota_on_chunk(uint32_t off, const uint8_t *data, size_t len, uint32_t now_ms);
/* The link was welcomed (true) or lost (false). */
void ota_on_link(bool welcomed, uint32_t now_ms);
/* The user held B on the Updating overlay. */
void ota_cancel(uint32_t now_ms);
