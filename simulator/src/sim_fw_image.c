/* The script's synthetic firmware image: `fw_image` makes one, `fw_begin` / `fw_send` offer it to the device. */
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "sim.h"

#define SIG_TAIL 4096u

static uint8_t *s_img;
static uint32_t s_img_size;
static uint8_t s_img_sha[32];

bool sim_fw_image(uint32_t size, const char *mode)
{
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
