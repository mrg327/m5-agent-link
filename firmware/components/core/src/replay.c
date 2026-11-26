#include "replay.h"

#include <string.h>

/* How much a code tells: the column keeps the highest; ABSENT and UNKNOWN tell nothing. */
static int rank(uint8_t c) { return c >= REPLAY_UNKNOWN ? -1 : (int)(c & 0x0Fu); }

static replay_track_t *find(replay_t *r, uint32_t id)
{
    for (int i = 0; i < REPLAY_TRACKS; i++) {
        if (r->t[i].used && r->t[i].id == id) return &r->t[i];
    }
    return NULL;
}

static const replay_track_t *find_c(const replay_t *r, uint32_t id)
{
    for (int i = 0; i < REPLAY_TRACKS; i++) {
        if (r->t[i].used && r->t[i].id == id) return &r->t[i];
    }
    return NULL;
}

void replay_init(replay_t *r)
{
    memset(r, 0, sizeof *r);
    r->lit = true;
}

/* Move on to the column of now_ms: each column that begins carries every session as it was. A gap
 * of an hour or more restarts the columns from now with the same carried state. */
static void advance(replay_t *r, uint32_t now_ms)
{
    if (!r->started) {
        r->started = true;
        r->step_ms = now_ms;
        r->filled = 1;
        return;
    }
    uint32_t gone = (now_ms - r->step_ms) / REPLAY_STEP_MS;
    if (gone == 0) return;
    uint32_t cols = gone < REPLAY_STEPS ? gone : REPLAY_STEPS;
    for (uint32_t k = 0; k < cols; k++) {
        r->head = (uint8_t)((r->head + 1) % REPLAY_STEPS);
        for (int i = 0; i < REPLAY_TRACKS; i++) {
            if (r->t[i].used) r->t[i].code[r->head] = r->t[i].live;
        }
        if (r->filled < REPLAY_STEPS) r->filled++;
    }
    r->serial = (uint16_t)(r->serial + gone);
    r->step_ms += gone * REPLAY_STEP_MS;
}

/* A track for a session new to the ring: an unused one, else the one gone longest. Never one on
 * the ring in this note (`seen` is the current serial). */
static replay_track_t *claim(replay_t *r, uint32_t id, uint8_t code)
{
    replay_track_t *best = NULL;
    for (int i = 0; i < REPLAY_TRACKS; i++) {
        replay_track_t *t = &r->t[i];
        if (!t->used) {
            best = t;
            break;
        }
        if (t->live != REPLAY_ABSENT || t->seen == r->serial) continue;
        if (!best || (uint16_t)(r->serial - t->seen) > (uint16_t)(r->serial - best->seen)) best = t;
    }
    if (!best) return NULL;
    best->used = true;
    best->id = id;
    for (int c = 0; c < REPLAY_STEPS; c++) {
        int age = (r->head - c + REPLAY_STEPS) % REPLAY_STEPS;
        best->code[c] = age < r->filled ? REPLAY_ABSENT : REPLAY_UNKNOWN;
    }
    best->code[r->head] = code;
    best->live = code;
    best->seen = r->serial;
    return best;
}

void replay_note(replay_t *r, const replay_seg_t *segs, int n, uint32_t now_ms)
{
    advance(r, now_ms);
    bool on[REPLAY_TRACKS] = {false};
    for (int k = 0; k < n; k++) {
        replay_track_t *t = find(r, segs[k].id);
        if (!t) {
            t = claim(r, segs[k].id, segs[k].code);
            if (!t) continue; /* more sessions than tracks: this one replays as now */
        } else {
            uint8_t *col = &t->code[r->head];
            if (rank(segs[k].code) >= rank(*col)) *col = segs[k].code; /* ties: the latest */
            t->live = segs[k].code;
            t->seen = r->serial;
        }
        on[t - r->t] = true;
    }
    for (int i = 0; i < REPLAY_TRACKS; i++) {
        if (r->t[i].used && !on[i]) r->t[i].live = REPLAY_ABSENT;
    }
}

bool replay_woke(replay_t *r, bool lit, uint32_t now_ms)
{
    bool was = r->lit;
    r->lit = lit;
    if (!lit) {
        if (was) r->dark_ms = now_ms;
        return false;
    }
    return !was && now_ms - r->dark_ms >= REPLAY_AWAY_MS;
}

uint8_t replay_code(const replay_t *r, uint32_t id, int step)
{
    if (step < 0 || step >= REPLAY_STEPS) return REPLAY_UNKNOWN;
    const replay_track_t *t = find_c(r, id);
    if (!t) return REPLAY_UNKNOWN;
    int head = r->playing ? r->play_head : r->head;
    return t->code[(head + 1 + step) % REPLAY_STEPS];
}

int replay_changes(const replay_t *r, const replay_seg_t *segs, int n)
{
    int changes = 0;
    for (int k = 0; k < n; k++) {
        uint8_t prev = REPLAY_UNKNOWN;
        for (int s = 0; s <= REPLAY_STEPS; s++) {
            uint8_t c = s < REPLAY_STEPS ? replay_code(r, segs[k].id, s) : segs[k].code;
            if (c == REPLAY_UNKNOWN) continue;
            if (prev != REPLAY_UNKNOWN && c != prev) changes++;
            prev = c;
        }
    }
    return changes;
}

void replay_start(replay_t *r, uint32_t now_ms)
{
    r->playing = true;
    r->play_head = r->head;
    r->play_ms = now_ms;
}

void replay_stop(replay_t *r) { r->playing = false; }

int replay_frame(replay_t *r, uint32_t now_ms)
{
    if (!r->playing) return REPLAY_OFF;
    uint32_t t = now_ms - r->play_ms;
    if (t < REPLAY_PLAY_MS) return (int)(t / REPLAY_FRAME_MS);
    if (t < REPLAY_PLAY_MS + REPLAY_FADE_MS) return REPLAY_STEPS;
    r->playing = false;
    return REPLAY_OFF;
}

int replay_arc_deg(int frame)
{
    if (frame < 0) return 0;
    if (frame >= REPLAY_STEPS) return 360;
    return (frame + 1) * 360 / REPLAY_STEPS;
}

int replay_arc_pct(const replay_t *r, uint32_t now_ms)
{
    if (!r->playing) return 0;
    uint32_t t = now_ms - r->play_ms;
    if (t < REPLAY_PLAY_MS) return 100;
    t -= REPLAY_PLAY_MS;
    if (t >= REPLAY_FADE_MS) return 0;
    /* four levels over the fade: 100, 75, 50, 25 */
    return 100 - (int)(t * 4 / REPLAY_FADE_MS) * 25;
}
