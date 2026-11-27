/* Reminders that grow louder (issue 20261126-a909): see nag.h. */
#include "nag.h"

#include <stddef.h>

static uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }

int nag_plan(uint32_t win_ms, uint32_t out_ms[NAG_STEPS])
{
    if (!win_ms) { /* no window known: a fixed ladder */
        out_ms[0] = NAG_R1_MS;
        out_ms[1] = NAG_R2_MS;
        out_ms[2] = NAG_NO_WIN_R3_MS;
        return NAG_STEPS;
    }
    if (win_ms < NAG_MIN_WIN_MS) return 0;
    if (win_ms < NAG_SHORT_WIN_MS) {
        out_ms[0] = win_ms / 2;
        return 1;
    }
    uint32_t t[NAG_STEPS];
    t[0] = NAG_R1_MS;
    t[1] = max_u32(NAG_R2_MS, win_ms / 2);
    t[2] = max_u32(t[1] + NAG_R3_AFTER_MS, win_ms - NAG_R3_BEFORE_MS);
    int n = 0;
    /* none in the window's last NAG_TAIL_MS; the times rise, so a step left out drops the rest */
    for (int i = 0; i < NAG_STEPS && t[i] + NAG_TAIL_MS < win_ms; i++) out_ms[n++] = t[i];
    return n;
}

void nag_reset(nag_t *n)
{
    n->rid = 0;
    n->done = 0;
    n->fired = false;
    n->fired_ms = 0;
    n->input = false;
    n->input_ms = 0;
}

void nag_note_input(nag_t *n, uint32_t now_ms)
{
    n->input = true;
    n->input_ms = now_ms;
}

/* Arrived after the last input: its ladder still runs. */
static bool unseen(const nag_t *n, const nag_req_t *r)
{ return !n->input || (int32_t)(r->arrived_ms - n->input_ms) > 0; }

/* The most urgent request whose ladder runs: least time left, then the oldest rid. */
static const nag_req_t *pick(const nag_t *n, const nag_req_t *reqs, int count)
{
    const nag_req_t *best = NULL;
    for (int i = 0; i < count; i++) {
        const nag_req_t *r = &reqs[i];
        if (!unseen(n, r)) continue;
        if (!best || r->left_ms < best->left_ms ||
            (r->left_ms == best->left_ms && r->rid < best->rid)) {
            best = r;
        }
    }
    return best;
}

/* How long the request's ladder has run; 0 before its start (a stamp later than this clock). */
static uint32_t elapsed(uint32_t now_ms, uint32_t start_ms)
{
    int32_t d = (int32_t)(now_ms - start_ms);
    return d > 0 ? (uint32_t)d : 0;
}

int nag_step(nag_t *n, const nag_req_t *reqs, int count, uint32_t now_ms, uint32_t *rid)
{
    const nag_req_t *r = pick(n, reqs, count);
    if (!r) {
        n->rid = 0;
        return 0;
    }
    uint32_t plan[NAG_STEPS];
    int steps = nag_plan(r->win_ms, plan);
    uint32_t el = elapsed(now_ms, r->start_ms);
    if (r->rid != n->rid) { /* a new ladder: the steps already past are skipped */
        n->rid = r->rid;
        n->done = 0;
        while (n->done < steps && el > plan[n->done]) n->done++;
    }
    int due = n->done;
    while (due < steps && el >= plan[due]) due++;
    if (due == n->done) return 0;
    if (r->left_ms <= NAG_TAIL_MS) { /* about to go back to the computer anyway */
        n->done = (uint8_t)due;
        return 0;
    }
    if (n->fired && (int32_t)(now_ms - n->fired_ms) < (int32_t)NAG_FLOOR_MS) return 0;
    n->done = (uint8_t)due;
    n->fired = true;
    n->fired_ms = now_ms;
    if (rid) *rid = r->rid;
    return due;
}
