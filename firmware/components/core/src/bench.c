/* Bench (BENCH.md): stroke player and frame timeline. */
#include "bench.h"

#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "debug.h"
#include "hal.h"
#include "link_internal.h"
#include "model.h"
#include "power.h"

/* ── the stroke player ── */

typedef struct {
    bool playing, seen; /* seen: a touch read has reported this stroke's press */
    bool warned;        /* touch_unread was logged for this stroke */
    uint32_t t0, dt, late_ms;
    uint16_t n;
    uint16_t reads,
        panel; /* touch reads that played it; of those, reads where the panel was pressed */
} play_t;
static play_t s_play;
/* The last touch read, so a stroke that no read plays says whether reads stopped (issue 435a). */
static uint32_t s_read_ms;
static bool s_read_any, s_read_down; /* s_read_down: what that read reported, after a stroke's */

/* ms since the last touch read, -1 before the first */
static long read_age(uint32_t now_ms) { return s_read_any ? (long)(now_ms - s_read_ms) : -1; }
MODEL_EXT_BSS static int16_t s_pts[BENCH_MAX_POINTS][2]; /* PSRAM on the watch */

static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

void bench_on_touch(uint32_t dt_ms, const uint8_t *pts, size_t len, uint32_t now_ms)
{
    size_t n = len / 4;
    if (n == 0 || n > BENCH_MAX_POINTS || dt_ms == 0 || dt_ms > 2000) {
        dbg_event_at(DBG_WARN, "bench", "touch_bad", "\"n\":%u,\"dt\":%u", (unsigned)n,
                     (unsigned)dt_ms);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        s_pts[i][0] = rd16(pts + 4 * i);
        s_pts[i][1] = rd16(pts + 4 * i + 2);
    }
    /* A stroke that arrives while another plays lifts that one first: the next read reports the
     * lift. */
    bool prev = s_play.playing, prev_seen = s_play.seen;
    s_play = (play_t){.playing = true, .t0 = now_ms, .dt = dt_ms, .n = (uint16_t)n};
    /* read_ago, prev and down say whether the read path is alive and what state it was left in */
    dbg_event("bench", "touch",
              "\"n\":%u,\"dt\":%u,\"x\":%d,\"y\":%d,\"read_ago\":%ld,\"prev\":%d,\"prev_seen\":%d,"
              "\"down\":%d",
              (unsigned)n, (unsigned)dt_ms, s_pts[0][0], s_pts[0][1], read_age(now_ms), prev,
              prev_seen, s_read_down);
}

bool bench_playing(void) { return s_play.playing; }

/* ── the timeline ── */

static struct {
    bool rec;
    uint64_t t0_us;
    uint32_t n, lost;
    /* the refresh being measured */
    uint64_t render_us, wait_from_us;
    uint32_t wait_us, px;
    /* the dump */
    bool dumping;
    uint32_t sent, fails;
    /* the last touch state, for down / up records */
    bool touch_down;
} s;
/* BENCH_MAX_RECS records, allocated at the first start (PSRAM on the watch) */
static uint8_t *s_recs;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void record(int kind, uint64_t t_us, uint32_t a, uint32_t b, uint32_t c)
{
    if (!s.rec) return;
    if (s.n >= BENCH_MAX_RECS) {
        s.lost++;
        return;
    }
    uint8_t *r = s_recs + (size_t)s.n++ * BENCH_REC_BYTES;
    put32(r, (uint32_t)(t_us - s.t0_us));
    put32(r + 4, a);
    put32(r + 8, b);
    put32(r + 12, (c & 0xFFFFFFu) | (uint32_t)kind << 24);
}

void bench_note(int kind, uint32_t arg) { record(kind, hal_time_us(), 0, arg, 0); }

bool bench_touch(uint32_t now_ms, bool *down, int32_t *x, int32_t *y)
{
    bool played = false;
    uint32_t late_ms = 0;
    s_read_ms = now_ms;
    s_read_any = true;
    if (s_play.playing) {
        played = true;
        if (s_play.reads < UINT16_MAX) s_play.reads++;
        /* a real finger, or a stuck panel */
        if (*down && s_play.panel < UINT16_MAX) s_play.panel++;
        uint32_t i = (now_ms - s_play.t0) / s_play.dt;
        /* The touch is polled, and a read can come tens of ms late while a frame renders: the
         * stroke's clock starts at the first read that plays it, so the watch sees the whole
         * stroke, a short tap included. */
        if (!s_play.seen) {
            late_ms = now_ms - s_play.t0;
            s_play.late_ms = late_ms;
            s_play.seen = true;
            s_play.t0 = now_ms;
            i = 0;
        }
        if (i < s_play.n) {
            *down = true;
            *x = s_pts[i][0];
            *y = s_pts[i][1];
        } else {
            *down = false; /* lifted where it last was */
            *x = s_pts[s_play.n - 1][0];
            *y = s_pts[s_play.n - 1][1];
            s_play.playing = false;
            dbg_event("bench", "stroke_end", "\"reads\":%u,\"panel\":%u,\"late\":%u",
                      (unsigned)s_play.reads, (unsigned)s_play.panel, (unsigned)s_play.late_ms);
        }
    }
    s_read_down = *down;
    if (*down != s.touch_down) {
        s.touch_down = *down;
        int32_t cx = *x < 0 ? 0 : *x > 4095 ? 4095 : *x, cy = *y < 0 ? 0 : *y > 4095 ? 4095 : *y;
        record(*down ? BENCH_DOWN : BENCH_UP, hal_time_us(), late_ms * 1000, 0,
               (uint32_t)cx << 12 | (uint32_t)cy);
    }
    return played;
}

static void on_refr_start(lv_event_t *e)
{
    (void)e;
    s.render_us = 0;
    s.wait_us = 0;
    s.px = 0;
}

static void on_render_start(lv_event_t *e)
{
    (void)e;
    if (!s.render_us) s.render_us = hal_time_us();
}

static void on_flush_start(lv_event_t *e)
{
    const lv_area_t *a = lv_event_get_param(e);
    if (a) s.px += (uint32_t)lv_area_get_size(a);
}

static void on_wait_start(lv_event_t *e)
{
    (void)e;
    s.wait_from_us = hal_time_us();
}

static void on_wait_finish(lv_event_t *e)
{
    (void)e;
    if (s.wait_from_us) s.wait_us += (uint32_t)(hal_time_us() - s.wait_from_us);
    s.wait_from_us = 0;
}

static void on_refr_ready(lv_event_t *e)
{
    (void)e;
    if (!s.px || !s.render_us) return; /* nothing was drawn */
    uint64_t now = hal_time_us();
    record(BENCH_FRAME, s.render_us, (uint32_t)(now - s.render_us), s.wait_us, s.px);
    s.render_us = 0;
}

void bench_init(lv_display_t *disp)
{
    lv_display_add_event_cb(disp, on_refr_start, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, on_render_start, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, on_flush_start, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(disp, on_wait_start, LV_EVENT_FLUSH_WAIT_START, NULL);
    lv_display_add_event_cb(disp, on_wait_finish, LV_EVENT_FLUSH_WAIT_FINISH, NULL);
    lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);
}

void bench_on_ctl(int op, uint32_t arg, uint32_t now_ms)
{
    switch (op) {
    case BENCH_OP_START:
        if (!s_recs) s_recs = malloc((size_t)BENCH_MAX_RECS * BENCH_REC_BYTES);
        if (!s_recs) {
            dbg_event_at(DBG_ERR, "bench", "no_memory", NULL);
            return;
        }
        bool down = s.touch_down;
        memset(&s, 0, sizeof s);
        s.touch_down = down;
        s.rec = true;
        s.t0_us = hal_time_us();
        /* A run starts with the user looking: the display wakes. The step's own time: power_tick()
         * later in this step compares against it unsigned, and a later stamp would read as an idle
         * eternity. */
        power_activity(now_ms);
        dbg_event("bench", "start", NULL);
        break;
    case BENCH_OP_STOP:
        s.rec = false;
        s.dumping = true;
        s.sent = 0;
        s.fails = 0;
        dbg_event("bench", "stop", "\"recs\":%u,\"lost\":%u", (unsigned)s.n, (unsigned)s.lost);
        break;
    case BENCH_OP_MARK: record(BENCH_MARK, hal_time_us(), 0, arg, 0); break;
    default: break;
    }
}

void bench_tick(uint32_t now_ms)
{
    /* A stroke no touch read has played for 500 ms: say so once, with the age of the last read. */
    if (s_play.playing && !s_play.seen && !s_play.warned && now_ms - s_play.t0 >= 500) {
        s_play.warned = true;
        dbg_event_at(DBG_WARN, "bench", "touch_unread", "\"read_ago\":%ld", read_age(now_ms));
    }
    for (int k = 0; k < 4 && s.dumping; k++) {
        uint32_t left = s.n - s.sent, m = left > BENCH_RECS_PER_MSG ? BENCH_RECS_PER_MSG : left;
        bool last = s.sent + m == s.n;
        const uint8_t *p = s_recs ? s_recs + (size_t)s.sent * BENCH_REC_BYTES : NULL;
        if (!link_send_bench_frames(s.sent, p, (size_t)m * BENCH_REC_BYTES, last, s.lost)) {
            /* a busy link: try again next step; a link that stays down ends the dump */
            if (++s.fails > 500 || !link_welcomed()) {
                s.dumping = false;
                dbg_event_at(DBG_WARN, "bench", "dump_failed", "\"sent\":%u", (unsigned)s.sent);
            }
            return;
        }
        s.fails = 0;
        s.sent += m;
        if (last) {
            s.dumping = false;
            dbg_event("bench", "dump_done", "\"recs\":%u", (unsigned)s.n);
        }
    }
}

const uint8_t *bench_records(size_t *count, uint32_t *lost)
{
    *count = s.n;
    *lost = s.lost;
    return s_recs;
}
