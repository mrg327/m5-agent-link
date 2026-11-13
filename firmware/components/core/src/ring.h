/* Tiny fixed-size FIFO that is safe to push to from HAL callbacks (other tasks, never an ISR) while
 * core_step() pops. Items are copied by value.
 *
 * Locking (issue 20261111-92d5): a plain spin flag livelocks on the watch when a higher-priority task
 * preempts the holder on the same core, so on ESP-IDF the ring's lock is a critical section (portMUX:
 * no preemption on this core while held, spins safely against the other core). Critical sections must
 * stay short, so only index bookkeeping and the small whole-item copies run inside one; the sized calls
 * that move kilobyte frames copy outside it (see below). On the host it stays an atomic spin flag. */
#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
typedef portMUX_TYPE ring_lock_t;
/* A lock that may be held across a long copy: a mutex with priority inheritance (tasks only). */
typedef struct { StaticSemaphore_t buf; SemaphoreHandle_t h; } ring_mutex_t;
#else
typedef atomic_flag ring_lock_t;
typedef atomic_flag ring_mutex_t;
#endif

typedef struct {
    uint8_t *buf;
    size_t item_size, capacity;
    size_t head, count;
    ring_lock_t lock;
} ring_t;

/* For other data shared the same way (app.c's staging frame). Init before first use. */
void ring_mutex_init(ring_mutex_t *m);
void ring_mutex_lock(ring_mutex_t *m);
void ring_mutex_unlock(ring_mutex_t *m);

/* `storage` must hold capacity * item_size bytes. */
void ring_init(ring_t *r, void *storage, size_t item_size, size_t capacity);
/* Returns false (and drops the item) when full. */
bool ring_push(ring_t *r, const void *item);
bool ring_pop(ring_t *r, void *item);
/* For items that are mostly unused tail (a 4 KB frame slot holding an 80 byte message): only the leading
 * bytes that matter are copied. `n` is how much of `item` to store; `used` reads a stored item's header and
 * returns how much of it to hand back. The copy runs outside the ring's lock (a slot is reserved, filled,
 * then published), so on a ring that uses them the caller serializes all pushes (app.c holds its rx mutex)
 * and pops from one task only. */
typedef size_t (*ring_used_fn)(const void *stored);
bool ring_push_n(ring_t *r, const void *item, size_t n);
bool ring_pop_n(ring_t *r, void *item, ring_used_fn used);
