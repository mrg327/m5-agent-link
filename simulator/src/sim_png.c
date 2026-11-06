/* Smallest possible PNG writer: 8-bit RGB, stored (uncompressed) deflate blocks. */
#include <stdio.h>

#include "sim.h"

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    crc = ~crc;
    while (n--) crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    fwrite(b, 1, 4, f);
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    put32(f, len);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = crc32_update(0, (const uint8_t *)type, 4);
    if (len) crc = crc32_update(crc, data, len);
    put32(f, crc);
}

bool sim_png_write(const char *path, const uint8_t *rgb, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                        (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0};
    chunk(f, "IHDR", ihdr, 13);

    /* Raw data: each scanline is a filter byte (0) followed by the pixels. */
    size_t row = 1 + (size_t)w * 3, raw_len = row * (size_t)h;
    uint32_t a = 1, b = 0; /* adler32 */
    uint32_t idat_len = 2 + (uint32_t)((raw_len + 65534) / 65535) * 5 + (uint32_t)raw_len + 4;
    put32(f, idat_len);
    fwrite("IDAT", 1, 4, f);
    uint32_t crc = crc32_update(0, (const uint8_t *)"IDAT", 4);
    uint8_t zhdr[2] = {0x78, 0x01};
    fwrite(zhdr, 1, 2, f);
    crc = crc32_update(crc, zhdr, 2);

    uint8_t block[65535];
    size_t done = 0, fill = 0;
    for (size_t i = 0; i < raw_len; i++) {
        uint8_t v = i % row == 0 ? 0 : rgb[(i / row) * (size_t)w * 3 + (i % row) - 1];
        block[fill++] = v;
        a = (a + v) % 65521;
        b = (b + a) % 65521;
        if (fill == sizeof block || i + 1 == raw_len) {
            bool last = i + 1 == raw_len;
            uint8_t bh[5] = {last, (uint8_t)fill, (uint8_t)(fill >> 8), (uint8_t)~fill, (uint8_t)(~fill >> 8)};
            fwrite(bh, 1, 5, f);
            fwrite(block, 1, fill, f);
            crc = crc32_update(crc, bh, 5);
            crc = crc32_update(crc, block, fill);
            done += fill;
            fill = 0;
        }
    }
    (void)done;
    uint8_t ad[4] = {(uint8_t)(b >> 8), (uint8_t)b, (uint8_t)(a >> 8), (uint8_t)a};
    fwrite(ad, 1, 4, f);
    crc = crc32_update(crc, ad, 4);
    put32(f, crc);
    put32(f, 0);
    fwrite("IEND", 1, 4, f);
    put32(f, 0xae426082u);
    return fclose(f) == 0;
}
