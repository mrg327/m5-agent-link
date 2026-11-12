/* The full text of one request, fetched in chunks (detail.h, ANSWER-WINDOW.md §4). */
#include "detail.h"

#include <string.h>

#include "debug.h"

typedef struct {
    uint32_t rid, sid;
    detail_state_t state;
    uint32_t total, got;
    uint32_t last_ms; /* when it was last asked for or a chunk arrived */
    uint8_t tries;
} detail_t;

static detail_t s_d;
/* 4 KiB: PSRAM on the device, like the other large tables (model.h). */
MODEL_EXT_BSS static char s_text[DETAIL_CAP + 1];
static detail_send_fn s_send;

void detail_set_sender(detail_send_fn send) { s_send = send; }

void detail_reset(void)
{
    memset(&s_d, 0, sizeof s_d);
    s_text[0] = 0;
}

bool detail_fetchable(const attention_t *a)
{
    return a && a->len_known && a->full_len > 0 && a->full_len <= DETAIL_CAP &&
           model_settings()->privacy != PRIVACY_META;
}

static void fail(const char *why)
{
    dbg_event("detail", "failed", "\"rid\":%u,\"why\":\"%s\",\"got\":%u", (unsigned)s_d.rid, why, (unsigned)s_d.got);
    s_d.state = DETAIL_FAILED;
}

static void ask(uint32_t now_ms)
{
    s_d.last_ms = now_ms;
    s_d.tries++;
    bool sent = s_send && s_send(s_d.sid, s_d.rid, s_d.got);
    dbg_event("detail", "ask", "\"rid\":%u,\"off\":%u,\"try\":%u,\"sent\":%s", (unsigned)s_d.rid, (unsigned)s_d.got,
              (unsigned)s_d.tries, sent ? "true" : "false");
}

void detail_want(const attention_t *a, uint32_t now_ms)
{
    if (!detail_fetchable(a)) return;
    if (s_d.rid != a->rid) { /* a different request: whatever was here belongs to another one */
        detail_reset();
        s_d.rid = a->rid;
        s_d.sid = a->sid;
        s_d.total = a->full_len;
        s_d.state = DETAIL_LOADING;
        ask(now_ms);
        return;
    }
    if (s_d.state == DETAIL_NONE) {
        s_d.state = DETAIL_LOADING;
        ask(now_ms);
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
    if (total != s_d.total) { /* not the text the request announced */
        fail("total");
        return;
    }
    if (off < s_d.got && off + len <= s_d.got) return; /* a repeat after a resume: already here */
    if (off != s_d.got || off + len > total) {
        dbg_event("detail", "dropped", "\"rid\":%u,\"off\":%u,\"got\":%u,\"why\":\"order\"", (unsigned)rid, (unsigned)off,
                  (unsigned)s_d.got);
        return; /* a gap: the stall timer asks again from s_d.got */
    }
    if (memchr(text, 0, len)) { /* a NUL would end the text early on screen: an elision nobody sees */
        fail("nul");
        return;
    }
    memcpy(s_text + off, text, len);
    s_d.got = off + len;
    s_text[s_d.got] = 0;
    s_d.last_ms = now_ms;
    s_d.tries = 0;
    if (s_d.got == s_d.total) {
        s_d.state = DETAIL_DONE;
        dbg_event("detail", "done", "\"rid\":%u,\"bytes\":%u", (unsigned)rid, (unsigned)s_d.total);
    }
}

void detail_on_result(uint32_t rid, bool ok, int err)
{
    if (rid != s_d.rid || s_d.state != DETAIL_LOADING) return;
    if (!ok) {
        dbg_event("detail", "refused", "\"rid\":%u,\"err\":%d", (unsigned)rid, err);
        fail("refused");
    } else if (s_d.got != s_d.total) {
        /* every chunk is sent before the result: one went missing; the stall timer asks again */
        dbg_event("detail", "short", "\"rid\":%u,\"got\":%u,\"total\":%u", (unsigned)rid, (unsigned)s_d.got,
                  (unsigned)s_d.total);
    }
}

void detail_tick(uint32_t now_ms)
{
    if (!s_d.rid) return;
    const attention_t *a = model_attention_find(s_d.rid);
    if (!a) {
        /* the request is gone; keep nothing of it (a reconnect re-sends a live request, and the transfer
         * resumes when it is back: until then the text is still the one for this rid) */
        if (s_d.state == DETAIL_LOADING && now_ms - s_d.last_ms >= 2 * DETAIL_STALL_MS) detail_reset();
        return;
    }
    if (s_d.state != DETAIL_LOADING || now_ms - s_d.last_ms < DETAIL_STALL_MS) return;
    if (s_d.tries >= DETAIL_TRIES) {
        fail("stalled");
        return;
    }
    ask(now_ms); /* a lost link or a dropped chunk: from where it got to */
}

detail_state_t detail_state(uint32_t rid) { return rid && rid == s_d.rid ? s_d.state : DETAIL_NONE; }
const char *detail_text(uint32_t rid) { return rid && rid == s_d.rid && s_d.got ? s_text : NULL; }
uint32_t detail_got(uint32_t rid) { return rid && rid == s_d.rid ? s_d.got : 0; }
uint32_t detail_total(uint32_t rid) { return rid && rid == s_d.rid ? s_d.total : 0; }
