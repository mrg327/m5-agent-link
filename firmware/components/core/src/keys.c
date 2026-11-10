/* KEYS swipe keyboard, device side (issue 1e57). See keys.h. */
#include "keys.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "link.h"

static const char *const kRows[2][3] = {{"qwertyuiop", "asdfghjkl", "zxcvbnm"}, {"1234567890", "-_./:~=+@", "#%&*?!'"}};
static const int16_t kRowX[3] = {33, 53, 93};
static const int16_t kRowY[3] = {166, 214, 262};
#define KEY_W 40
#define KEY_H 48
#define BOTTOM_Y 310

int keys_layout(int layer, keys_key_t *out, int cap)
{
    int n = 0;
    layer = layer ? 1 : 0;
    for (int r = 0; r < 3; r++)
        for (int i = 0; kRows[layer][r][i] && n < cap; i++)
            out[n++] = (keys_key_t){(uint16_t)kRows[layer][r][i], (int16_t)(kRowX[r] + KEY_W * i), kRowY[r], KEY_W, KEY_H};
    if (n + 3 <= cap) {
        out[n++] = (keys_key_t){KEYS_CP_LAYER, 93, BOTTOM_Y, 60, KEY_H};
        out[n++] = (keys_key_t){KEYS_CP_SPACE, 153, BOTTOM_Y, 160, KEY_H};
        out[n++] = (keys_key_t){KEYS_CP_BKSP, 313, BOTTOM_Y, 60, KEY_H};
    }
    return n;
}

static bool s_active;
static uint32_t s_sid, s_kid, s_seq;
static int s_layer;
static bool s_was_welcomed;

static keys_stroke_t s_buf[2];
static int s_live, s_q[2], s_qn;
static bool s_down, s_rec;
static uint32_t s_t0, s_last_t;

static keys_cands_t s_cands = {.typed = -1, .err = -1};

bool keys_active(void) { return s_active; }
int keys_layer(void) { return s_layer; }
uint32_t keys_kid(void) { return s_kid; }
uint32_t keys_seq(void) { return s_seq; }
const keys_cands_t *keys_cands(void) { return &s_cands; }

void keys_cands_clear(void)
{
    if (!s_cands.n && s_cands.typed < 0 && s_cands.err < 0) return;
    s_cands = (keys_cands_t){.typed = -1, .err = -1, .rev = s_cands.rev + 1};
}

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
}

static bool send_start(void)
{
    keys_key_t k[KEYS_MAX_KEYS];
    uint8_t buf[KEYS_MAX_KEYS * 10];
    int n = keys_layout(s_layer, k, KEYS_MAX_KEYS);
    for (int i = 0; i < n; i++) {
        uint8_t *p = buf + i * 10;
        le16(p, k[i].cp);
        le16(p + 2, (uint16_t)k[i].x);
        le16(p + 4, (uint16_t)k[i].y);
        le16(p + 6, k[i].w);
        le16(p + 8, k[i].h);
    }
    return link_send_kb_start(s_kid, s_sid, buf, (size_t)n * 10);
}

bool keys_open(uint32_t sid, uint32_t now_ms)
{
    s_kid = s_kid + 1 + (now_ms & 0xff); /* differs across boots: the daemon keys a keyboard by kid */
    s_sid = sid;
    s_seq = 0;
    s_layer = 0;
    s_qn = 0;
    s_active = true;
    s_was_welcomed = link_welcomed();
    keys_cands_clear();
    dbg_event("keys", "open", "\"kid\":%u,\"sid\":%u", (unsigned)s_kid, (unsigned)sid);
    return send_start();
}

void keys_close(bool erase)
{
    if (!s_active) return;
    s_active = false;
    s_qn = 0;
    link_send_cmd_arg(LINK_OP_KB_END, s_sid, 0, erase ? 1 : 0, 0);
    keys_cands_clear();
    dbg_event("keys", "close", "\"kid\":%u,\"erase\":%s", (unsigned)s_kid, erase ? "true" : "false");
}

bool keys_set_layer(int layer)
{
    s_layer = layer ? 1 : 0;
    dbg_event("keys", "layer", "\"layer\":%d", s_layer);
    return send_start();
}

/* A reconnect (maybe a restarted daemon) forgot the keyboard: announce it again. */
void keys_tick(uint32_t now_ms)
{
    (void)now_ms;
    if (!s_active) return;
    bool up = link_welcomed();
    if (up && !s_was_welcomed) send_start();
    s_was_welcomed = up;
}

/* ── stroke recorder ── */

static void add_pt(keys_stroke_t *s, int x, int y, uint32_t now_ms)
{
    uint32_t dt = now_ms - s_t0;
    keys_pt_t p = {(int16_t)x, (int16_t)y, (uint16_t)(dt > 65535 ? 65535 : dt)};
    if (s->n < KEYS_MAX_PTS) s->pts[s->n++] = p;
    else s->pts[KEYS_MAX_PTS - 1] = p; /* full: the end of the stroke still counts */
    s_last_t = now_ms;
}

static int free_slot(void)
{
    for (int i = 0; i < 2; i++) {
        bool queued = false;
        for (int j = 0; j < s_qn; j++) queued |= s_q[j] == i;
        if (!queued) return i;
    }
    return -1;
}

bool keys_touch(bool down, int x, int y, uint32_t now_ms)
{
    if (!s_active && !s_down) return false;
    if (down && !s_down) {
        s_down = true;
        int slot = s_active ? free_slot() : -1;
        s_rec = slot >= 0;
        if (s_rec) {
            s_live = slot;
            s_buf[slot].n = 0;
            s_t0 = now_ms;
            add_pt(&s_buf[slot], x, y, now_ms);
        }
    } else if (down) {
        if (s_rec && now_ms - s_last_t >= KEYS_SAMPLE_MS) add_pt(&s_buf[s_live], x, y, now_ms);
    } else if (s_down) {
        s_down = false;
        if (s_rec && s_active) {
            keys_stroke_t *s = &s_buf[s_live];
            const keys_pt_t *l = &s->pts[s->n - 1];
            if (l->x != x || l->y != y) add_pt(s, x, y, now_ms);
            s_q[s_qn++] = s_live;
        }
        s_rec = false;
    }
    return true;
}

const keys_stroke_t *keys_live(bool *down)
{
    if (down) *down = s_down && s_rec;
    return &s_buf[s_live];
}

const keys_stroke_t *keys_next_stroke(void) { return s_qn ? &s_buf[s_q[0]] : NULL; }

void keys_stroke_done(void)
{
    if (!s_qn) return;
    s_q[0] = s_q[1];
    s_qn--;
}

bool keys_is_tap(const keys_stroke_t *s)
{
    int x0 = s->pts[0].x, x1 = x0, y0 = s->pts[0].y, y1 = y0;
    for (int i = 1; i < s->n; i++) {
        if (s->pts[i].x < x0) x0 = s->pts[i].x;
        if (s->pts[i].x > x1) x1 = s->pts[i].x;
        if (s->pts[i].y < y0) y0 = s->pts[i].y;
        if (s->pts[i].y > y1) y1 = s->pts[i].y;
    }
    return x1 - x0 < KEYS_TAP_PX && y1 - y0 < KEYS_TAP_PX;
}

bool keys_send_stroke(const keys_stroke_t *s)
{
    static uint8_t buf[KEYS_MAX_PTS * 6];
    for (int i = 0; i < s->n; i++) {
        uint8_t *p = buf + i * 6;
        le16(p, (uint16_t)s->pts[i].x);
        le16(p + 2, (uint16_t)s->pts[i].y);
        le16(p + 4, s->pts[i].t_ms);
    }
    bool ok = link_send_kb_stroke(s_kid, s_seq, buf, (size_t)s->n * 6);
    dbg_event("keys", "stroke", "\"seq\":%u,\"pts\":%u,\"tap\":%s,\"sent\":%s", (unsigned)s_seq, (unsigned)s->n,
              keys_is_tap(s) ? "true" : "false", ok ? "true" : "false");
    if (ok) s_seq++;
    return ok;
}

/* ── commands ── */

bool keys_pick(int idx) { return link_send_cmd_arg(LINK_OP_KB_PICK, s_sid, 0, (uint32_t)idx, 0); }

bool keys_del(void)
{
    keys_cands_clear(); /* they described the word that is gone */
    return link_send_cmd(LINK_OP_KB_DEL, s_sid, 0, 0);
}

bool keys_send(uint32_t hold_ms)
{
    keys_cands_clear(); /* a new composition starts */
    return link_send_cmd(LINK_OP_KB_SEND, s_sid, 0, hold_ms);
}

/* ── candidates ── */

void keys_on_cands(uint32_t kid, uint32_t seq, const char *const *words, int n, int typed, int err)
{
    (void)seq;
    if (!s_active || kid != s_kid) return; /* another keyboard, or one already closed */
    s_cands.n = n > KEYS_MAX_CANDS ? KEYS_MAX_CANDS : n;
    for (int i = 0; i < s_cands.n; i++) snprintf(s_cands.word[i], KEYS_CAND_LEN, "%s", words[i]);
    s_cands.typed = typed >= 0 && typed < s_cands.n ? typed : -1;
    s_cands.err = err;
    s_cands.rev++;
}
