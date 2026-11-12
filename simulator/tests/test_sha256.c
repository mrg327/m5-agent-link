/* SHA-256 against the FIPS 180-4 / NIST vectors, with the message fed in odd-sized pieces. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"

static int fails;

static void check(const char *name, const uint8_t *msg, size_t len, size_t piece, const char *want)
{
    sha256_t c;
    uint8_t out[32];
    char hex[65];
    sha256_init(&c);
    for (size_t i = 0; i < len; i += piece) sha256_update(&c, msg + i, len - i < piece ? len - i : piece);
    sha256_final(&c, out);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", out[i]);
    if (strcmp(hex, want)) {
        printf("FAIL %s (piece %zu): %s\n", name, piece, hex);
        fails++;
    }
}

int main(void)
{
    const size_t pieces[] = {1, 7, 63, 64, 65, 1000};
    uint8_t *million = malloc(1000000);
    memset(million, 'a', 1000000);
    for (size_t k = 0; k < sizeof pieces / sizeof *pieces; k++) {
        size_t p = pieces[k];
        check("empty", (const uint8_t *)"", 0, p, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        check("abc", (const uint8_t *)"abc", 3, p, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        check("448 bits", (const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, p,
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
        check("million a", million, 1000000, p, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }
    free(million);
    puts(fails ? "sha256: FAILED" : "sha256: ok");
    return fails != 0;
}
