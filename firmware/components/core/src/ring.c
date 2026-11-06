#include "ring.h"

#include <string.h>

static void lock(ring_t *r)
{
    while (atomic_flag_test_and_set_explicit(&r->lock, memory_order_acquire)) {
    }
}

static void unlock(ring_t *r)
{
    atomic_flag_clear_explicit(&r->lock, memory_order_release);
}

void ring_init(ring_t *r, void *storage, size_t item_size, size_t capacity)
{
    r->buf = storage;
    r->item_size = item_size;
    r->capacity = capacity;
    r->head = 0;
    r->count = 0;
    atomic_flag_clear(&r->lock);
}

bool ring_push(ring_t *r, const void *item)
{
    bool ok = false;
    lock(r);
    if (r->count < r->capacity) {
        size_t tail = (r->head + r->count) % r->capacity;
        memcpy(r->buf + tail * r->item_size, item, r->item_size);
        r->count++;
        ok = true;
    }
    unlock(r);
    return ok;
}

bool ring_pop(ring_t *r, void *item)
{
    bool ok = false;
    lock(r);
    if (r->count > 0) {
        memcpy(item, r->buf + r->head * r->item_size, r->item_size);
        r->head = (r->head + 1) % r->capacity;
        r->count--;
        ok = true;
    }
    unlock(r);
    return ok;
}
