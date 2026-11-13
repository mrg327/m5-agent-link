#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ring.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

/* A frame slot as app.c queues them: a short header and a mostly unused tail. */
typedef struct { uint16_t len; uint8_t data[64]; } slot_t;
static size_t used(const void *stored) { return offsetof(slot_t, data) + ((const slot_t *)stored)->len; }

/* The sized calls copy outside the lock: one pusher and one popper on other threads must still see every
 * item whole and in order. */
#define STRESS_N 200000
static ring_t s_sr;
static slot_t s_sstore[4];

static void *stress_push(void *arg)
{
    (void)arg;
    slot_t in;
    for (uint32_t i = 0; i < STRESS_N;) {
        in.len = (uint16_t)(5 + i % 60);
        memset(in.data, (int)(i & 0xFF), in.len);
        memcpy(in.data, &i, sizeof i);
        if (ring_push_n(&s_sr, &in, used(&in))) i++;
    }
    return NULL;
}

static void stress(void)
{
    ring_init(&s_sr, s_sstore, sizeof(slot_t), 4);
    pthread_t t;
    pthread_create(&t, NULL, stress_push, NULL);
    slot_t out;
    int bad = 0;
    for (uint32_t i = 0; i < STRESS_N;) {
        if (!ring_pop_n(&s_sr, &out, used)) continue;
        uint32_t seq;
        memcpy(&seq, out.data, sizeof seq);
        if (seq != i || out.len != 5 + i % 60 || out.data[out.len - 1] != (uint8_t)(i & 0xFF)) bad++;
        i++;
    }
    pthread_join(t, NULL);
    EXPECT(bad == 0, "concurrent sized push and pop keep every item whole and in order");
}

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

    stress();

    if (failures) printf("ring: FAILED\n");
    else printf("ring: ok\n");
    return failures != 0;
}
