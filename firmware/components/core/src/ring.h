/* Tiny fixed-size FIFO that is safe to push to from HAL callbacks (other tasks, never an ISR) while
 * core_step() pops. A spin flag guards the copy; items are copied by value. On the watch a contender
 * that keeps finding the flag set sleeps a tick, so a lower-priority holder on its core can finish. */
#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    size_t item_size, capacity;
    size_t head, count;
    atomic_flag lock;
} ring_t;

/* The ring's lock, for other data shared the same way (app.c's staging frame). */
void ring_spin_lock(atomic_flag *f);
void ring_spin_unlock(atomic_flag *f);

/* `storage` must hold capacity * item_size bytes. */
void ring_init(ring_t *r, void *storage, size_t item_size, size_t capacity);
/* Returns false (and drops the item) when full. */
bool ring_push(ring_t *r, const void *item);
bool ring_pop(ring_t *r, void *item);
/* For items that are mostly unused tail (a 4 KB frame slot holding an 80 byte message): only the leading
 * bytes that matter are copied, so the spin flag is held for those and not for the whole slot. `n` is how
 * much of `item` to store; `used` reads a stored item's header and returns how much of it to hand back. */
typedef size_t (*ring_used_fn)(const void *stored);
bool ring_push_n(ring_t *r, const void *item, size_t n);
bool ring_pop_n(ring_t *r, void *item, ring_used_fn used);
