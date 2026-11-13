#include "ring.h"

#include <string.h>

#ifdef ESP_PLATFORM
static void lock(ring_t *r) { portENTER_CRITICAL(&r->lock); }
static void unlock(ring_t *r) { portEXIT_CRITICAL(&r->lock); }

void ring_mutex_init(ring_mutex_t *m) { m->h = xSemaphoreCreateMutexStatic(&m->buf); }
void ring_mutex_lock(ring_mutex_t *m) { xSemaphoreTake(m->h, portMAX_DELAY); }
void ring_mutex_unlock(ring_mutex_t *m) { xSemaphoreGive(m->h); }
#else
static void spin(atomic_flag *f)
{
    while (atomic_flag_test_and_set_explicit(f, memory_order_acquire)) {
    }
}
static void lock(ring_t *r) { spin(&r->lock); }
static void unlock(ring_t *r) { atomic_flag_clear_explicit(&r->lock, memory_order_release); }

void ring_mutex_init(ring_mutex_t *m) { atomic_flag_clear(m); }
void ring_mutex_lock(ring_mutex_t *m) { spin(m); }
void ring_mutex_unlock(ring_mutex_t *m) { atomic_flag_clear_explicit(m, memory_order_release); }
#endif

void ring_init(ring_t *r, void *storage, size_t item_size, size_t capacity)
{
    r->buf = storage;
    r->item_size = item_size;
    r->capacity = capacity;
    r->head = 0;
    r->count = 0;
#ifdef ESP_PLATFORM
    portMUX_INITIALIZE(&r->lock);
#else
    atomic_flag_clear(&r->lock);
#endif
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

/* The free slot at the tail is the pusher's alone until count covers it, and the slot at the head is the
 * popper's until head moves past it; the lock only guards head and count. */
bool ring_push_n(ring_t *r, const void *item, size_t n)
{
    if (n > r->item_size) n = r->item_size;
    lock(r);
    bool room = r->count < r->capacity;
    size_t tail = (r->head + r->count) % r->capacity;
    unlock(r);
    if (!room) return false;
    memcpy(r->buf + tail * r->item_size, item, n);
    lock(r);
    r->count++;
    unlock(r);
    return true;
}

bool ring_pop_n(ring_t *r, void *item, ring_used_fn used)
{
    lock(r);
    bool any = r->count > 0;
    size_t head = r->head;
    unlock(r);
    if (!any) return false;
    const uint8_t *src = r->buf + head * r->item_size;
    size_t n = used(src);
    memcpy(item, src, n > r->item_size ? r->item_size : n);
    lock(r);
    r->head = (r->head + 1) % r->capacity;
    r->count--;
    unlock(r);
    return true;
}
