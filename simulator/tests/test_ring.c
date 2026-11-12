#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ring.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

/* A frame slot as app.c queues them: a short header and a mostly unused tail. */
typedef struct { uint16_t len; uint8_t data[64]; } slot_t;
static size_t used(const void *stored) { return offsetof(slot_t, data) + ((const slot_t *)stored)->len; }

int main(void)
{
    static slot_t store[3];
    ring_t r;
    ring_init(&r, store, sizeof(slot_t), 3);

    slot_t in, out;
    for (int i = 0; i < 3; i++) {
        memset(&in, 0xEE, sizeof in); /* the tail holds stale bytes that must not travel */
        in.len = (uint16_t)(4 + i);
        memset(in.data, 'a' + i, in.len);
        EXPECT(ring_push_n(&r, &in, used(&in)), "a short item is queued");
    }
    EXPECT(!ring_push_n(&r, &in, used(&in)), "a full ring drops the item");
    for (int i = 0; i < 3; i++) {
        memset(&out, 0x11, sizeof out);
        EXPECT(ring_pop_n(&r, &out, used), "items come back");
        EXPECT(out.len == 4 + i && out.data[0] == 'a' + i && out.data[out.len - 1] == 'a' + i, "in order, with their bytes");
        EXPECT(out.data[out.len] == 0x11, "only the used bytes are copied out");
    }
    EXPECT(!ring_pop_n(&r, &out, used), "an empty ring pops nothing");

    /* the sized calls and the whole-item calls work on the same ring, and wrap */
    in.len = 64;
    memset(in.data, 'z', 64);
    EXPECT(ring_push(&r, &in) && ring_push_n(&r, &in, sizeof in + 100), "an oversized count is clamped to the slot");
    EXPECT(ring_pop_n(&r, &out, used) && out.data[63] == 'z', "a full slot survives the sized pop");
    EXPECT(ring_pop(&r, &out) && out.len == 64, "and the plain pop");

    if (failures) printf("ring: FAILED\n");
    else printf("ring: ok\n");
    return failures != 0;
}
