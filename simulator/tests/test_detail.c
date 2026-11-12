/* ANSWER-WINDOW.md on the device: the full text of a request (detail.c) is bound to its rid, arrives in
 * order, resumes after a lost link, fails closed on anything that does not add up; and the answer window
 * is counted on the device's own clock and ends the request when no word comes from the daemon. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "detail.h"
#include "model.h"

void dbg_event(const char *c, const char *e, const char *f, ...) { (void)c; (void)e; (void)f; }
void dbg_event_at(dbg_level_t l, const char *c, const char *e, const char *f, ...) { (void)l; (void)c; (void)e; (void)f; }
const char *dbg_q(const char *s) { return s; }

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

static int s_sent;
static uint32_t s_last_off, s_last_rid;
static bool s_link_up = true;
static bool sender(uint32_t sid, uint32_t rid, uint32_t off)
{
    (void)sid;
    s_sent++;
    s_last_off = off;
    s_last_rid = rid;
    return s_link_up;
}

static attention_t request(uint32_t rid, uint32_t full_len)
{
    attention_t a;
    memset(&a, 0, sizeof a);
    a.rid = rid;
    a.sid = 2;
    a.kind = ATT_APPROVAL;
    snprintf(a.text, sizeof a.text, "preview…");
    a.len_known = true;
    a.full_len = full_len;
    model_attention_add(&a);
    return a;
}

static void chunk(uint32_t rid, uint32_t off, uint32_t total, const char *t, uint32_t now)
{
    detail_on_chunk(rid, 2, off, total, (const uint8_t *)t, (uint32_t)strlen(t), now);
}

int main(void)
{
    model_init();
    session_t s = {.sid = 2, .state = SESS_INPUT};
    model_upsert(&s, SF_STATE);
    model_settings()->privacy = PRIVACY_PREVIEW;
    detail_reset();
    detail_set_sender(sender);

    /* what can be fetched at all */
    attention_t a = request(7, 12);
    EXPECT(detail_fetchable(&a), "a cut text within the cap");
    attention_t big = a;
    big.full_len = DETAIL_CAP + 1;
    EXPECT(!detail_fetchable(&big), "past the cap: never asked for");
    attention_t old = a;
    old.len_known = false;
    EXPECT(!detail_fetchable(&old), "an older daemon has no detail");
    model_settings()->privacy = PRIVACY_META;
    EXPECT(!detail_fetchable(&a), "privacy meta withholds it");
    model_settings()->privacy = PRIVACY_PREVIEW;

    /* bound to the rid, in order */
    detail_want(&a, 1000);
    EXPECT(s_sent == 1 && s_last_off == 0 && s_last_rid == 7, "asked from byte 0 for rid 7");
    EXPECT(detail_state(7) == DETAIL_LOADING, "loading");
    chunk(8, 0, 12, "other reques", 1010);
    EXPECT(detail_got(7) == 0 && !detail_text(7), "another rid's text is dropped");
    EXPECT(detail_text(8) == NULL, "and never readable under its own rid either");
    chunk(7, 6, 12, "second", 1020);
    EXPECT(detail_got(7) == 0, "a gap is dropped");
    chunk(7, 0, 12, "first ", 1030);
    EXPECT(detail_got(7) == 6 && !strcmp(detail_text(7), "first "), "the first chunk");
    chunk(7, 0, 12, "first ", 1040);
    EXPECT(detail_got(7) == 6, "a repeat after a resume adds nothing");
    detail_on_result(7, true, -1); /* ok, but short: a chunk went missing */
    EXPECT(detail_state(7) == DETAIL_LOADING, "still loading");
    detail_tick(1040 + DETAIL_STALL_MS);
    EXPECT(s_sent == 2 && s_last_off == 6, "a stall asks again from where it got to");
    chunk(7, 6, 12, "second", 5000);
    EXPECT(detail_state(7) == DETAIL_DONE && !strcmp(detail_text(7), "first second"), "whole");
    detail_want(&a, 5100);
    EXPECT(s_sent == 2, "nothing more to ask for");

    /* a new request starts over: the old text is not the new request's */
    attention_t b = request(9, 5);
    detail_want(&b, 6000);
    EXPECT(detail_text(7) == NULL && detail_state(9) == DETAIL_LOADING && s_last_rid == 9, "rid 9 replaces rid 7");

    /* fail closed: a total that disagrees, a NUL, a refusal */
    chunk(9, 0, 6, "abcdef", 6010);
    EXPECT(detail_state(9) == DETAIL_FAILED && detail_text(9) == NULL, "wrong total: failed, nothing shown");
    attention_t c = request(10, 3);
    detail_want(&c, 7000);
    detail_on_chunk(10, 2, 0, 3, (const uint8_t *)"a\0b", 3, 7010);
    EXPECT(detail_state(10) == DETAIL_FAILED, "a NUL would hide the rest: failed");
    attention_t d = request(11, 3);
    detail_want(&d, 8000);
    detail_on_result(11, false, 0);
    EXPECT(detail_state(11) == DETAIL_FAILED, "stale / denied / unsupported: failed");

    /* a link that never comes back: a few tries, then failed */
    attention_t e = request(12, 3);
    s_link_up = false;
    detail_want(&e, 9000);
    uint32_t t = 9000;
    for (int i = 0; i < DETAIL_TRIES + 1; i++) {
        t += DETAIL_STALL_MS;
        detail_tick(t);
    }
    EXPECT(detail_state(12) == DETAIL_FAILED, "gave up after DETAIL_TRIES");
    s_link_up = true;

    /* the answer window on the device's own clock */
    model_init();
    model_upsert(&s, SF_STATE);
    attention_t w;
    memset(&w, 0, sizeof w);
    w.rid = 20;
    w.sid = 2;
    w.kind = ATT_APPROVAL;
    w.win_ms = 60000;
    w.deadline_ms = 100000;
    model_attention_add(&w);
    const attention_t *pw = model_attention_find(20);
    EXPECT(model_attention_left_ms(pw, 70000) == 30000, "30 s left");
    EXPECT(model_attention_left_ms(pw, 100500) == 0, "over");
    model_attention_expire_ms(100000 + MODEL_WINDOW_GRACE_MS);
    EXPECT(model_attention_find(20) != NULL, "the daemon's clear gets a moment");
    model_attention_expire_ms(100001 + MODEL_WINDOW_GRACE_MS);
    EXPECT(model_attention_find(20) == NULL, "then it ends on the watch");
    EXPECT(model_find(2)->back && model_find(2)->back_why == BACK_EXPIRED, "and the card says where it went");
    session_t moved = {.sid = 2, .state = SESS_WORK};
    model_upsert(&moved, SF_STATE);
    EXPECT(!model_find(2)->back, "the session moved on");
    attention_t nw = w;
    nw.rid = 21;
    model_attention_add(&nw);
    model_attention_set_answer(21, ATT_ANSWER_APPROVE, 0);
    EXPECT(model_attention_went_back(21, BACK_EXPIRED), "cleared");
    EXPECT(!model_find(2)->back, "an answer already sent says Waiting, not On your computer");
    EXPECT(model_attention_left_ms(NULL, 0) == UINT32_MAX, "no window known");

    if (failures) printf("%d failure(s)\n", failures);
    else printf("detail: all passed\n");
    return failures ? 1 : 0;
}
