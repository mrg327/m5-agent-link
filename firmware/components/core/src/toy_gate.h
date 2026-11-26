/* Toy feedback gate (EXPERIMENTS.md "Toys"): whether a toy may buzz or sound now. An agent alert
 * always wins: a toy never buzzes or sounds over one, nor in a setting that holds alerts back.
 * Pure, with an explicit clock; feedback.c feeds it and does the playing. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "model.h"

/* After an alert starts, toys stay quiet at least this long, or as long as its cue and buzz last.
 */
#define TOY_ALERT_HOLD_MS 1500

typedef struct {
    bool holding; /* an alert started at from_ms and is held for hold_ms */
    uint32_t from_ms, hold_ms;
} toy_gate_t;

/* What the watch is set to and doing when a toy asks. */
typedef struct {
    alert_mode_t alert; /* full: buzz and sound; quiet: buzz only; silent: neither */
    bool face_down;     /* MOTION.md: nothing buzzes or sounds */
    unsigned volume;    /* the stored speaker step; 0 is muted */
    bool mic_on;        /* the microphone records: the speaker is silent anyway */
    bool fun_off;       /* the fun preference keeps toys still (feedback.c fills it in) */
} toy_gate_in_t;

/* An alert starts now; its cue and buzz last alert_ms (0 if unknown). */
void toy_gate_alert(toy_gate_t *g, uint32_t now_ms, uint32_t alert_ms);
/* An alert's hold-off still runs (wrap-safe). */
bool toy_gate_holding(toy_gate_t *g, uint32_t now_ms);
/* A toy's buzz: refused in silent mode, face down, with the fun bit off and during the hold-off. */
bool toy_gate_may_buzz(toy_gate_t *g, const toy_gate_in_t *in, uint32_t now_ms);
/* A toy's sound: only in the full alert mode, at a volume above 0, face up, with the microphone
 * off, the fun bit on and no hold-off running. */
bool toy_gate_may_sound(toy_gate_t *g, const toy_gate_in_t *in, uint32_t now_ms);
