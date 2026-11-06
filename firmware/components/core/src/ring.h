/* Tiny fixed-size FIFO that is safe to push to from HAL callbacks (other task or ISR context)
 * while core_step() pops. A spin flag guards a few instructions; items are copied by value. */
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

/* `storage` must hold capacity * item_size bytes. */
void ring_init(ring_t *r, void *storage, size_t item_size, size_t capacity);
/* Returns false (and drops the item) when full. */
bool ring_push(ring_t *r, const void *item);
bool ring_pop(ring_t *r, void *item);
