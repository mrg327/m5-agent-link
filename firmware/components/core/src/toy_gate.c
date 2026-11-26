#include "toy_gate.h"

void toy_gate_alert(toy_gate_t *g, uint32_t now_ms, uint32_t alert_ms)
{
    uint32_t hold = alert_ms > TOY_ALERT_HOLD_MS ? alert_ms : TOY_ALERT_HOLD_MS;
    /* a second alert inside the first one's hold-off: hold until the later of the two ends */
    if (toy_gate_holding(g, now_ms)) {
        uint32_t left = g->hold_ms - (now_ms - g->from_ms);
        if (left > hold) hold = left;
    }
    g->holding = true;
    g->from_ms = now_ms;
    g->hold_ms = hold;
}

bool toy_gate_holding(toy_gate_t *g, uint32_t now_ms)
{
    if (g->holding && now_ms - g->from_ms >= g->hold_ms) g->holding = false;
    return g->holding;
}

bool toy_gate_may_buzz(toy_gate_t *g, const toy_gate_in_t *in, uint32_t now_ms)
{
    if (toy_gate_holding(g, now_ms)) return false;
    return in->alert != ALERT_SILENT && !in->face_down && !in->fun_off;
}

bool toy_gate_may_sound(toy_gate_t *g, const toy_gate_in_t *in, uint32_t now_ms)
{
    if (toy_gate_holding(g, now_ms)) return false;
    return in->alert == ALERT_FULL && in->volume > 0 && !in->face_down && !in->mic_on &&
           !in->fun_off;
}
