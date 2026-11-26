/* The request store (model.c): a re-sent request is known, not new (issue 20261126-d95a), also
 * after a welcome dropped every card; the queue's order (issue 20261126-aa28). */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "model.h"

void dbg_event(const char *c, const char *e, const char *f, ...)
{
    (void)c;
    (void)e;
    (void)f;
}
void dbg_event_at(dbg_level_t l, const char *c, const char *e, const char *f, ...)
{
    (void)l;
    (void)c;
    (void)e;
    (void)f;
}
const char *dbg_q(const char *s) { return s; }

static int failures;
#define EXPECT(cond, msg)                                  \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

static bool add(uint32_t rid, uint32_t sid, const char *title)
{
    attention_t a;
    memset(&a, 0, sizeof a);
    a.rid = rid;
    a.sid = sid;
    a.kind = ATT_APPROVAL;
    snprintf(a.title, sizeof a.title, "%s", title);
    snprintf(a.text, sizeof a.text, "npm test");
    return model_attention_add(&a);
}

static void sessions(void)
{
    model_init();
    for (uint32_t sid = 2; sid <= 5; sid++) {
        session_t s = {.sid = sid, .state = SESS_INPUT};
        model_upsert(&s, SF_STATE);
    }
}

static void test_known(void)
{
    sessions();
    EXPECT(add(7, 2, "Run shell command"), "a new rid is new");
    EXPECT(!add(7, 2, "Run shell command"), "the same rid again is a re-send");
    model_attention_set_answer(7, ATT_ANSWER_APPROVE, 0);
    EXPECT(!add(7, 2, "Run shell command"), "still a re-send once answered");
    EXPECT(model_attention_find(7)->answer_sent, "a re-send keeps the answer");
    EXPECT(add(8, 3, "Edit file"), "another rid is new");

    model_attention_clear_all(); /* a welcome */
    EXPECT(!model_attention_find(7), "the welcome drops the cards");
    EXPECT(!add(7, 2, "Run shell command"), "re-sent after the welcome: known");
    EXPECT(model_attention_find(7)->answer_sent, "and keeps the answer given before");
    EXPECT(add(8, 3, "Another request"), "same rid, other text (a restarted daemon): new");
    EXPECT(add(9, 4, "Fetch"), "a rid never seen: new");
    model_attention_clear(7);
    EXPECT(add(7, 2, "Run shell command"), "known once only: after it went, the rid is new");
}

static void add_win(uint32_t rid, uint32_t sid, uint32_t deadline_ms)
{
    attention_t a;
    memset(&a, 0, sizeof a);
    a.rid = rid;
    a.sid = sid;
    a.kind = ATT_APPROVAL;
    a.deadline_ms = deadline_ms;
    model_attention_add(&a);
}

static uint32_t next(uint32_t now, uint32_t except)
{
    const attention_t *a = model_attention_next(now, except);
    return a ? a->rid : 0;
}

static void test_queue(void)
{
    sessions();
    EXPECT(next(0, 0) == 0, "empty");
    add_win(7, 2, 0);     /* no window */
    add_win(8, 3, 60000); /* 60 s left at 0 */
    add_win(9, 4, 30000); /* 30 s left */
    add_win(6, 5, 0);     /* no window, older rid */
    EXPECT(next(0, 0) == 9, "least time left first");
    EXPECT(next(0, 9) == 8, "except the one on screen");
    model_attention_set_answer(9, ATT_ANSWER_APPROVE, 0);
    EXPECT(next(0, 0) == 8, "an answered request is not in the queue");
    model_attention_skip(8);
    EXPECT(next(0, 0) == 6, "a skipped one neither; then no window, oldest rid first");
    EXPECT(next(0, 6) == 7, "then the next rid");
    model_attention_clear_all(); /* a welcome: the re-send stays skipped */
    add_win(8, 3, 60000);
    EXPECT(next(0, 0) == 0, "skipped survives the welcome's re-send");
    add_win(9, 4, 30000);
    EXPECT(next(0, 0) == 0, "and so does the answer sent");
    add_win(10, 2, 1000);
    EXPECT(next(2000, 0) == 0, "a window that is over is not offered");
    EXPECT(next(500, 0) == 10, "a new request is");
}

int main(void)
{
    test_known();
    test_queue();
    if (failures) {
        printf("%d failure(s)\n", failures);
    } else {
        printf("model: all passed\n");
    }
    return failures ? 1 : 0;
}
