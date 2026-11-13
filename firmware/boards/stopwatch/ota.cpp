// Firmware update: the spare app slot through esp_ota_* (FIRMWARE.md §9). Core drives the transfer.
// The trust check is ESP-IDF's "signed app images without secure boot": esp_ota_end() refuses an image
// that is not signed by the key in the running app's own signature block.

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "sdkconfig.h"

#include "hal.h"

static const char *TAG = "ota";

#define HEAD_LEN (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

static esp_ota_handle_t s_handle;
static const esp_partition_t *s_part;
static uint32_t s_size, s_written;
static uint8_t s_head[HEAD_LEN];
static char s_err[48];

static hal_ota_result_t fail(hal_ota_result_t r, const char *msg, esp_err_t err)
{
    snprintf(s_err, sizeof s_err, "%s", msg);
    ESP_LOGW(TAG, "%s (%s)", msg, esp_err_to_name(err));
    hal_ota_abort();
    return r;
}

// The image header and app description, before anything is written: a stray file is refused at once
// rather than after a 1.5 MB transfer.
static hal_ota_result_t check_head(void)
{
    const esp_image_header_t *h = (const esp_image_header_t *)s_head;
    const esp_app_desc_t *d = (const esp_app_desc_t *)(s_head + sizeof(esp_image_header_t) +
                                                       sizeof(esp_image_segment_header_t));
    if (h->magic != ESP_IMAGE_HEADER_MAGIC || d->magic_word != ESP_APP_DESC_MAGIC_WORD)
        return fail(HAL_OTA_BAD, "Not an app image", ESP_ERR_INVALID_ARG);
    if (h->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID)
        return fail(HAL_OTA_BAD, "Built for another chip", ESP_ERR_INVALID_ARG);
    if (strncmp(d->project_name, esp_app_get_description()->project_name, sizeof d->project_name) != 0)
        return fail(HAL_OTA_BAD, "Not agentlink firmware", ESP_ERR_INVALID_ARG);
    ESP_LOGI(TAG, "image %.32s %.32s", d->project_name, d->version);
    return HAL_OTA_OK;
}

hal_ota_result_t hal_ota_begin(uint32_t size)
{
    hal_ota_abort();
    s_err[0] = 0;
    s_part = esp_ota_get_next_update_partition(NULL);
    if (!s_part) {
        snprintf(s_err, sizeof s_err, "No spare app slot");
        return HAL_OTA_UNSUPPORTED;
    }
    if (size < HEAD_LEN || size > s_part->size) {
        snprintf(s_err, sizeof s_err, size < HEAD_LEN ? "Image too small" : "Image too big");
        s_part = NULL;
        return HAL_OTA_BAD;
    }
    // Sequential writes erase sector by sector as the image arrives, instead of the whole slot up front.
    esp_err_t err = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (err != ESP_OK) {
        s_part = NULL;
        return fail(HAL_OTA_FAIL, "Flash error", err);
    }
    s_size = size;
    s_written = 0;
    ESP_LOGI(TAG, "begin %lu B into %s", (unsigned long)size, s_part->label);
    return HAL_OTA_OK;
}

hal_ota_result_t hal_ota_write(const uint8_t *data, size_t len)
{
    if (!s_handle) return fail(HAL_OTA_FAIL, "No update open", ESP_ERR_INVALID_STATE);
    if (len > s_size - s_written) return fail(HAL_OTA_BAD, "Image longer than announced", ESP_ERR_INVALID_SIZE);
    if (s_written < HEAD_LEN) {
        size_t n = len < HEAD_LEN - s_written ? len : HEAD_LEN - s_written;
        memcpy(s_head + s_written, data, n);
        if (s_written + n == HEAD_LEN) {
            hal_ota_result_t r = check_head();
            if (r != HAL_OTA_OK) return r;
        }
    }
    esp_err_t err = esp_ota_write(s_handle, data, len);
    if (err != ESP_OK)
        return fail(err == ESP_ERR_OTA_VALIDATE_FAILED ? HAL_OTA_BAD : HAL_OTA_FAIL,
                    err == ESP_ERR_OTA_VALIDATE_FAILED ? "Not an app image" : "Flash error", err);
    s_written += len;
    return HAL_OTA_OK;
}

hal_ota_result_t hal_ota_finish(void)
{
    if (!s_handle) return fail(HAL_OTA_FAIL, "No update open", ESP_ERR_INVALID_STATE);
    if (s_written != s_size) return fail(HAL_OTA_BAD, "Image incomplete", ESP_ERR_INVALID_SIZE);
    esp_err_t err = esp_ota_end(s_handle); // verifies the hash and the signature; frees the handle either way
    s_handle = 0;
    if (err != ESP_OK) {
        s_part = NULL;
        snprintf(s_err, sizeof s_err, "%s",
                 err == ESP_ERR_OTA_VALIDATE_FAILED ? "Signature not trusted" : "Flash error");
        ESP_LOGW(TAG, "end: %s", esp_err_to_name(err));
        return err == ESP_ERR_OTA_VALIDATE_FAILED ? HAL_OTA_BAD : HAL_OTA_FAIL;
    }
    err = esp_ota_set_boot_partition(s_part);
    s_part = NULL;
    if (err != ESP_OK) {
        snprintf(s_err, sizeof s_err, "%s", err == ESP_ERR_OTA_VALIDATE_FAILED ? "Signature not trusted" : "Flash error");
        ESP_LOGW(TAG, "set boot: %s", esp_err_to_name(err));
        return err == ESP_ERR_OTA_VALIDATE_FAILED ? HAL_OTA_BAD : HAL_OTA_FAIL;
    }
    ESP_LOGI(TAG, "image verified; boots next");
    return HAL_OTA_OK;
}

void hal_ota_abort(void)
{
    if (s_handle) esp_ota_abort(s_handle);
    s_handle = 0;
    s_part = NULL;
    s_size = s_written = 0;
}

const char *hal_ota_error(void)
{
    return s_err;
}

bool hal_ota_unconfirmed(void)
{
    esp_ota_img_states_t st;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
           st == ESP_OTA_IMG_PENDING_VERIFY;
}

void hal_ota_confirm(void)
{
    if (!hal_ota_unconfirmed()) return;
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "image confirmed: %s", esp_err_to_name(err));
}

// The base of a delta update: the slot this image runs from, read through the flash driver (the bytes as
// written, not the cache's mapping).
hal_ota_result_t hal_ota_base_read(uint32_t off, void *buf, size_t len)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (!run) return HAL_OTA_UNSUPPORTED;
    if (off > run->size || len > run->size - off) return HAL_OTA_BAD;
    esp_err_t err = esp_partition_read(run, off, buf, len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "base read at %lu: %s", (unsigned long)off, esp_err_to_name(err));
        return HAL_OTA_FAIL;
    }
    return HAL_OTA_OK;
}

uint32_t hal_ota_base_size(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    return run ? run->size : 0;
}

void hal_restart(void)
{
    esp_restart();
}
