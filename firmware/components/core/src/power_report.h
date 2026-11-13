/* Power report (docs/design/POWER.md, proto 0.19): the time spent in each power state, sent with the battery
 * voltages to a daemon that can read it, right after its welcome and then every 5 minutes. The figures add up
 * while no daemon listens and survive a deep sleep in RTC memory. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PREPORT_EVERY_MS (5u * 60u * 1000u)
#define PREPORT_RETRY_MS 30000u
/* The battery is read at least this often, also when nothing else reads it (display off): it keeps the
 * board's BLE Battery Level notification current. The core loop runs anyway; this adds no wakeup. */
#define PREPORT_SAMPLE_MS 60000u

typedef enum { PREPORT_ON = 0, PREPORT_DIM = 1, PREPORT_OFF = 2 } preport_state_t;

/* ms per state since the last report that went out (saturating), and what happened meanwhile. */
typedef struct {
    uint32_t on, dim, off, deep; /* display on / dimmed / off, deep sleep */
    uint32_t lk, ble;            /* welcomed by a daemon / a BLE central connected */
    uint32_t nd;                 /* deep sleeps that ended */
} preport_times_t;

/* ── pure parts (simulator/tests/test_power_report.c) ── */
void preport_account(preport_times_t *t, preport_state_t st, bool linked, bool ble, uint32_t dt_ms);
/* The figures as RTC words (HAL_RETAINED_WORDS) and back; unpack is false for words that hold none. */
void preport_pack(const preport_times_t *t, uint32_t seq, uint32_t *words);
bool preport_unpack(const uint32_t *words, preport_times_t *t, uint32_t *seq);
/* A boot that ended a deep sleep of slept_ms. */
void preport_add_deep(preport_times_t *t, uint32_t slept_ms);

/* ── the running report ── */
/* At boot, before the first tick: takes back figures a deep sleep kept, and the sleep itself. */
void preport_init(uint32_t now_ms);
void preport_tick(uint32_t now_ms);
/* A daemon welcomed the device (or the link went): a new link gets a report at once. */
void preport_on_link(bool welcomed, uint32_t now_ms);
/* Just before DEEP: the unsent figures go to RTC memory. Undone (the figures stay live) when DEEP is refused. */
void preport_save(uint32_t now_ms);
void preport_unsave(void);
/* For tests and the debug channel. */
const preport_times_t *preport_times(void);
uint32_t preport_seq(void);
