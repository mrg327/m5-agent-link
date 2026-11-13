/* The script's synthetic firmware image: `fw_image` makes one, `fw_begin` / `fw_send` offer it to the device.
 * `fw_patch` loads a delta-OTA fixture instead (protocol/fixtures/ota): the image is new.bin, the chunks carry
 * the patch, and the device's running slot is base.bin. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "sim.h"

#define SIG_TAIL 4096u

static uint8_t *s_img;
static uint32_t s_img_size;
static uint8_t s_img_sha[32];
static uint8_t *s_patch;
static uint32_t s_patch_size;
static uint8_t s_base_sha[32];
static uint32_t s_base_size;

static void hash(const uint8_t *d, uint32_t n, uint8_t out[32])
{
    sha256_t c;
    sha256_init(&c);
    sha256_update(&c, d, n);
    sha256_final(&c, out);
}

static uint8_t *load(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    *size = d ? (uint32_t)n : 0;
    return d;
}

static void drop_patch(void)
{
    free(s_patch);
    s_patch = NULL;
    s_patch_size = 0;
    sim_ota_set_raw(false);
}

bool sim_fw_patch(const char *path, const char *mode)
{
    drop_patch();
    char dir[512], file[600];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    else snprintf(dir, sizeof dir, ".");
    uint32_t base_size = 0;
    snprintf(file, sizeof file, "%s/base.bin", dir);
    uint8_t *base = load(file, &base_size);
    snprintf(file, sizeof file, "%s/new.bin", dir);
    free(s_img);
    s_img = load(file, &s_img_size);
    s_patch = load(path, &s_patch_size);
    if (!base || !s_img || !s_patch) {
        free(base);
        drop_patch();
        return false;
    }
    hash(s_img, s_img_size, s_img_sha);
    hash(base, base_size, s_base_sha);
    s_base_size = base_size;
    if (!strcmp(mode, "otherbase")) base[base_size / 2] ^= 0x01; /* the watch runs something else */
    if (!strcmp(mode, "corrupt")) s_patch[s_patch_size / 2] ^= 0x55;
    sim_ota_set_running(base, base_size);
    sim_ota_set_raw(true); /* the fixtures are not app images: no header or signature stand-ins */
    free(base);
    return true;
}

bool sim_fw_is_patch(void) { return s_patch != NULL; }
const uint8_t *sim_fw_base_sha(void) { return s_base_sha; }
uint32_t sim_fw_base_size(void) { return s_base_size; }
uint32_t sim_fw_xfer_size(void) { return s_patch ? s_patch_size : s_img_size; }
const uint8_t *sim_fw_xfer_data(void) { return s_patch ? s_patch : s_img; }

bool sim_fw_slot_matches(void)
{
    uint32_t n = 0;
    const uint8_t *slot = sim_ota_slot(&n);
    return slot && n == s_img_size && !memcmp(slot, s_img, n);
}

bool sim_fw_image(uint32_t size, const char *mode)
{
    drop_patch();
    free(s_img);
    s_img = malloc(size ? size : 1);
    if (!s_img) return false;
    s_img_size = size;
    uint32_t x = 12345;
    for (uint32_t i = 0; i < size; i++) {
        x = x * 1664525u + 1013904223u;
        s_img[i] = (uint8_t)(x >> 24);
    }
    if (size) s_img[0] = strcmp(mode, "badhdr") ? 0xE9 : 0x00;
    if (size >= SIG_TAIL && strcmp(mode, "unsigned")) {
        s_img[size - SIG_TAIL] = 0xE7;
        s_img[size - SIG_TAIL + 1] = 0x02;
    }
    sha256_t c;
    sha256_init(&c);
    sha256_update(&c, s_img, size);
    sha256_final(&c, s_img_sha);
    if (!strcmp(mode, "badsha")) s_img_sha[0] ^= 1; /* announced hash does not match the bytes */
    return true;
}

uint32_t sim_fw_size(void) { return s_img_size; }
const uint8_t *sim_fw_sha(void) { return s_img_sha; }
const uint8_t *sim_fw_data(void) { return s_img; }
