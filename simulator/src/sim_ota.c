/* hal_ota_* for the simulator: the image is buffered in RAM and checked the way the watch does, with a
 * stand-in trust rule (the last 4 KB of an image start with E7 02 when it is "signed"). The running slot a
 * delta update reads as its base is whatever sim_ota_set_running() was given (none by default).
 * $AGENTLINK_SIM_OTA_WRITE_US makes each write take that long, like the watch's flash (issue 203f). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sim.h"

#define SIM_SLOT_MAX 0x600000u
#define SIG_TAIL 4096u

static uint8_t *s_buf;
static uint32_t s_size, s_got;
static bool s_open;
static char s_err[48] = "";

static bool s_unconfirmed_init, s_unconfirmed;
static uint8_t *s_run;
static uint32_t s_run_size;
static bool s_raw;

void sim_ota_set_running(const uint8_t *img, uint32_t size)
{
    free(s_run);
    s_run = size ? malloc(size) : NULL;
    s_run_size = s_run ? size : 0;
    if (s_run) memcpy(s_run, img, size);
}

void sim_ota_set_raw(bool raw) { s_raw = raw; }

const uint8_t *sim_ota_slot(uint32_t *size)
{
    *size = s_got;
    return s_buf;
}

hal_ota_result_t hal_ota_base_read(uint32_t off, void *buf, size_t len)
{
    if (!s_run) return HAL_OTA_UNSUPPORTED;
    if (off > s_run_size || len > s_run_size - off) return HAL_OTA_BAD;
    memcpy(buf, s_run + off, len);
    return HAL_OTA_OK;
}

uint32_t hal_ota_base_size(void) { return s_run_size; }

hal_ota_result_t hal_ota_begin(uint32_t size)
{
    free(s_buf);
    s_buf = NULL;
    s_open = false;
    if (size > SIM_SLOT_MAX) {
        snprintf(s_err, sizeof s_err, "Image too big");
        return HAL_OTA_BAD;
    }
    s_buf = malloc(size ? size : 1);
    if (!s_buf) {
        snprintf(s_err, sizeof s_err, "Out of memory");
        return HAL_OTA_FAIL;
    }
    s_size = size;
    s_got = 0;
    s_open = true;
    s_err[0] = 0;
    sim_log("ota_slot_open", "\"size\":%u", (unsigned)size);
    return HAL_OTA_OK;
}

hal_ota_result_t hal_ota_write(const uint8_t *data, size_t len)
{
    if (!s_open || len > s_size - s_got) {
        snprintf(s_err, sizeof s_err, "Write past the end");
        return HAL_OTA_BAD;
    }
    if (!s_raw && s_got == 0 && len && data[0] != 0xE9) {
        snprintf(s_err, sizeof s_err, "Not an app image");
        return HAL_OTA_BAD;
    }
    static int write_us = -1;
    if (write_us < 0) {
        const char *e = getenv("AGENTLINK_SIM_OTA_WRITE_US");
        write_us = e ? atoi(e) : 0;
    }
    if (write_us > 0) usleep((useconds_t)write_us);
    memcpy(s_buf + s_got, data, len);
    s_got += (uint32_t)len;
    return HAL_OTA_OK;
}

hal_ota_result_t hal_ota_finish(void)
{
    if (!s_open || s_got != s_size) {
        snprintf(s_err, sizeof s_err, "Incomplete image");
        return HAL_OTA_BAD;
    }
    if (!s_raw && (s_size < SIG_TAIL || s_buf[s_size - SIG_TAIL] != 0xE7 || s_buf[s_size - SIG_TAIL + 1] != 0x02)) {
        snprintf(s_err, sizeof s_err, "Signature not trusted");
        return HAL_OTA_BAD;
    }
    s_open = false;
    sim_log("ota_slot_ready", "\"size\":%u", (unsigned)s_size);
    return HAL_OTA_OK;
}

void hal_ota_abort(void)
{
    if (s_open) sim_log("ota_slot_abort", NULL);
    s_open = false;
}

const char *hal_ota_error(void) { return s_err; }

bool hal_ota_unconfirmed(void)
{
    if (!s_unconfirmed_init) {
        s_unconfirmed_init = true;
        const char *e = getenv("AGENTLINK_SIM_UNCONFIRMED");
        s_unconfirmed = e && e[0] == '1';
    }
    return s_unconfirmed;
}

void hal_ota_confirm(void)
{
    if (hal_ota_unconfirmed()) sim_log("ota_confirmed", NULL);
    s_unconfirmed = false;
}

void hal_restart(void) { sim_log("restart", NULL); }
