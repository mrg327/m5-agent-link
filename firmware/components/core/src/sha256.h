/* SHA-256 (FIPS 180-4), incremental. The OTA transfer hashes the image as it arrives. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t buf[64];
} sha256_t;

void sha256_init(sha256_t *c);
void sha256_update(sha256_t *c, const uint8_t *data, size_t len);
void sha256_final(sha256_t *c, uint8_t out[32]);
