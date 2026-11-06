/* Decodes the `cbor_hex` of every vector json file in protocol/vectors and checks that re-encoding gives the same bytes.
 * usage: test_vectors <vectors-dir> */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agentlink_codec_decode.h"
#include "agentlink_codec_encode.h"

static struct envelope_r g_env; /* large: keep off the stack */

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = 0;
    fclose(f);
    return buf;
}

/* Returns the number of bytes decoded from the "cbor_hex" value, or -1. */
static int extract_cbor(const char *json, uint8_t *out, size_t cap)
{
    const char *p = strstr(json, "\"cbor_hex\"");
    if (!p) return -1;
    p = strchr(p + 10, '"');
    if (!p) return -1;
    p++;
    size_t n = 0;
    while (hex_val(p[0]) >= 0 && hex_val(p[1]) >= 0) {
        if (n >= cap) return -1;
        out[n++] = (uint8_t)(hex_val(p[0]) << 4 | hex_val(p[1]));
        p += 2;
    }
    return (int)n;
}

static int check_vector(const char *path, const char *name)
{
    uint8_t want[4096], got[4096];
    char *json = read_file(path);
    if (!json) { printf("FAIL %s: unreadable\n", name); return 1; }
    int n = extract_cbor(json, want, sizeof want);
    free(json);
    if (n <= 0) { printf("FAIL %s: no cbor_hex\n", name); return 1; }

    size_t used = 0, out_len = 0;
    memset(&g_env, 0, sizeof g_env);
    int rc = cbor_decode_envelope(want, (size_t)n, &g_env, &used);
    if (rc != ZCBOR_SUCCESS || used != (size_t)n) {
        printf("FAIL %s: decode rc=%d used=%zu/%d\n", name, rc, used, n);
        return 1;
    }
    rc = cbor_encode_envelope(got, sizeof got, &g_env, &out_len);
    if (rc != ZCBOR_SUCCESS) { printf("FAIL %s: encode rc=%d\n", name, rc); return 1; }
    if (out_len != (size_t)n || memcmp(got, want, out_len) != 0) {
        printf("FAIL %s: re-encoded bytes differ (%zu vs %d)\n", name, out_len, n);
        return 1;
    }
    printf("ok   %s (%d bytes)\n", name, n);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <vectors-dir>\n", argv[0]); return 2; }
    DIR *d = opendir(argv[1]);
    if (!d) { perror("opendir"); return 2; }
    int failed = 0, total = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 6 || strcmp(e->d_name + l - 5, ".json") != 0) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", argv[1], e->d_name);
        failed += check_vector(path, e->d_name);
        total++;
    }
    closedir(d);
    printf("%d vectors, %d failed\n", total, failed);
    return (failed || total == 0) ? 1 : 0;
}
