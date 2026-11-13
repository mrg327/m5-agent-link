/* Long texts fetched in chunks (detail.h): a request's full text (ANSWER-WINDOW.md §4) and a session's last
 * message (LAST-MESSAGE.md §2.3). Each has a slot of its own, so reading a message never costs the request's. */
#include "detail.h"

#include <string.h>

#include "debug.h"

typedef struct {
    uint32_t rid, sid, lm; /* a request: rid; a last message: sid and lm (rid 0) */
    detail_state_t state;
    uint32_t total, got;
    uint32_t last_ms; /* when it was last asked for or a chunk arrived */
    uint8_t tries;
    char *text;
} detail_t;

/* 4 KiB each: PSRAM on the device, like the other large tables (model.h). */
MODEL_EXT_BSS static char s_req_text[DETAIL_CAP + 1];
MODEL_EXT_BSS static char s_msg_text[DETAIL_CAP + 1];
static detail_t s_d = {.text = s_req_text}, s_m = {.text = s_msg_text};
static detail_send_fn s_send;

void detail_set_sender(detail_send_fn send) { s_send = send; }

static void clear(detail_t *d)
{
    char *t = d->text;
    memset(d, 0, sizeof *d);
    d->text = t;
    t[0] = 0;
}

void detail_reset(void)
{
    clear(&s_d);
    clear(&s_m);
}

bool detail_fetchable(const attention_t *a)
{
    return a && a->len_known && a->full_len > 0 && a->full_len <= DETAIL_CAP &&
           model_settings()->privacy != PRIVACY_META;
}

bool detail_msg_fetchable(const session_t *s)
{
    return s && s->lm && s->ll > 0 && s->ll <= DETAIL_CAP && s->lp[0] && model_peer_minor() >= 18 &&
           model_settings()->privacy != PRIVACY_META;
}

static void fail(detail_t *d, const char *why)
{
    dbg_event("detail", "failed", "\"rid\":%u,\"why\":\"%s\",\"got\":%u,\"lm\":%u", (unsigned)d->rid, why, (unsigned)d->got,
              (unsigned)d->lm);
    d->state = DETAIL_FAILED;
}

static void ask(detail_t *d, uint32_t now_ms)
{
    d->last_ms = now_ms;
    d->tries++;
    bool sent = s_send && s_send(d->sid, d->rid, d->got);
    dbg_event("detail", "ask", "\"rid\":%u,\"off\":%u,\"try\":%u,\"sent\":%s,\"sid\":%u,\"lm\":%u", (unsigned)d->rid,
              (unsigned)d->got, (unsigned)d->tries, sent ? "true" : "false", (unsigned)d->sid, (unsigned)d->lm);
}

void detail_want(const attention_t *a, uint32_t now_ms)
{
    if (!detail_fetchable(a)) return;
    if (s_d.rid != a->rid) { /* a different request: whatever was here belongs to another one */
        clear(&s_d);
        s_d.rid = a->rid;
        s_d.sid = a->sid;
        s_d.total = a->full_len;
        s_d.state = DETAIL_LOADING;
        ask(&s_d, now_ms);
        return;
    }
    if (s_d.state == DETAIL_NONE) {
        s_d.state = DETAIL_LOADING;
        ask(&s_d, now_ms);
    }
}

void detail_want_msg(const session_t *s, uint32_t now_ms)
{
    if (!detail_msg_fetchable(s)) return;
    if (s_m.sid != s->sid || s_m.lm != s->lm) { /* another message: start over */
        clear(&s_m);
        s_m.sid = s->sid;
        s_m.lm = s->lm;
        s_m.total = s->ll;
        s_m.state = DETAIL_LOADING;
        ask(&s_m, now_ms);
        return;
    }
    if (s_m.state == DETAIL_NONE) {
        s_m.state = DETAIL_LOADING;
        ask(&s_m, now_ms);
    }
}

/* A chunk for slot `d`, already known to be the text it asked for. */
static void take(detail_t *d, uint32_t off, uint32_t total, const uint8_t *text, uint32_t len, uint32_t now_ms)
{
    if (total != d->total) { /* not the text that was announced */
        fail(d, "total");
        return;
    }
    if (off < d->got && off + len <= d->got) return; /* a repeat after a resume: already here */
    if (off != d->got || off + len > total) {
        dbg_event("detail", "dropped", "\"rid\":%u,\"off\":%u,\"got\":%u,\"why\":\"order\",\"lm\":%u", (unsigned)d->rid,
                  (unsigned)off, (unsigned)d->got, (unsigned)d->lm);
        return; /* a gap: the stall timer asks again from d->got */
    }
    if (memchr(text, 0, len)) { /* a NUL would end the text early on screen: an elision nobody sees */
        fail(d, "nul");
        return;
    }
    memcpy(d->text + off, text, len);
    d->got = off + len;
    d->text[d->got] = 0;
    d->last_ms = now_ms;
    d->tries = 0;
    if (d->got == d->total) {
        d->state = DETAIL_DONE;
        dbg_event("detail", "done", "\"rid\":%u,\"bytes\":%u,\"lm\":%u", (unsigned)d->rid, (unsigned)d->total, (unsigned)d->lm);
    }
}

void detail_on_chunk(uint32_t rid, uint32_t sid, uint32_t off, uint32_t total, const uint8_t *text, uint32_t len,
                     uint32_t now_ms)
{
    /* bound to the request asked for: anything else is dropped, never shown */
    if (!rid || rid != s_d.rid || s_d.state != DETAIL_LOADING || sid != s_d.sid) {
        dbg_event("detail", "dropped", "\"rid\":%u,\"want\":%u,\"why\":\"not_asked\"", (unsigned)rid, (unsigned)s_d.rid);
        return;
    }
    take(&s_d, off, total, text, len, now_ms);
}

void detail_on_msg_chunk(uint32_t sid, uint32_t lm, uint32_t off, uint32_t total, const uint8_t *text, uint32_t len,
                         uint32_t now_ms)
{
    /* bound to the session and the message asked for: a chunk of another message is dropped */
    if (!lm || lm != s_m.lm || sid != s_m.sid || s_m.state != DETAIL_LOADING) {
        dbg_event("detail", "dropped", "\"lm\":%u,\"want\":%u,\"why\":\"not_asked\"", (unsigned)lm, (unsigned)s_m.lm);
        return;
    }
    take(&s_m, off, total, text, len, now_ms);
}

void detail_on_result(uint32_t sid, uint32_t rid, bool ok, int err)
{
    detail_t *d = rid ? &s_d : &s_m;
    if (rid != d->rid || (!rid && (!d->lm || sid != d->sid)) || d->state != DETAIL_LOADING) return;
    if (!ok) {
        dbg_event("detail", "refused", "\"rid\":%u,\"err\":%d,\"lm\":%u", (unsigned)rid, err, (unsigned)d->lm);
        fail(d, "refused");
    } else if (d->got != d->total) {
        /* every chunk is sent before the result: one went missing; the stall timer asks again */
        dbg_event("detail", "short", "\"rid\":%u,\"got\":%u,\"total\":%u,\"lm\":%u", (unsigned)rid, (unsigned)d->got,
                  (unsigned)d->total, (unsigned)d->lm);
    }
}

static void stall(detail_t *d, uint32_t now_ms)
{
    if (d->state != DETAIL_LOADING || now_ms - d->last_ms < DETAIL_STALL_MS) return;
    if (d->tries >= DETAIL_TRIES) {
        fail(d, "stalled");
        return;
    }
    ask(d, now_ms); /* a lost link or a dropped chunk: from where it got to */
}

void detail_tick(uint32_t now_ms)
{
    if (s_d.rid) {
        if (!model_attention_find(s_d.rid)) {
            /* the request is gone; keep nothing of it (a reconnect re-sends a live request, and the transfer
             * resumes when it is back: until then the text is still the one for this rid) */
            if (s_d.state == DETAIL_LOADING && now_ms - s_d.last_ms >= 2 * DETAIL_STALL_MS) clear(&s_d);
        } else {
            stall(&s_d, now_ms);
        }
    }
    if (s_m.lm) {
        const session_t *s = model_find(s_m.sid);
        if (!s || s->lm != s_m.lm) clear(&s_m); /* the message was replaced or cleared, or its session went */
        else stall(&s_m, now_ms);
    }
}

detail_state_t detail_state(uint32_t rid) { return rid && rid == s_d.rid ? s_d.state : DETAIL_NONE; }
const char *detail_text(uint32_t rid) { return rid && rid == s_d.rid && s_d.got ? s_d.text : NULL; }
uint32_t detail_got(uint32_t rid) { return rid && rid == s_d.rid ? s_d.got : 0; }
uint32_t detail_total(uint32_t rid) { return rid && rid == s_d.rid ? s_d.total : 0; }

static bool msg_is(uint32_t sid, uint32_t lm) { return lm && lm == s_m.lm && sid == s_m.sid; }
detail_state_t detail_msg_state(uint32_t sid, uint32_t lm) { return msg_is(sid, lm) ? s_m.state : DETAIL_NONE; }
const char *detail_msg_text(uint32_t sid, uint32_t lm) { return msg_is(sid, lm) && s_m.got ? s_m.text : NULL; }
uint32_t detail_msg_got(uint32_t sid, uint32_t lm) { return msg_is(sid, lm) ? s_m.got : 0; }
uint32_t detail_msg_total(uint32_t sid, uint32_t lm) { return msg_is(sid, lm) ? s_m.total : 0; }
