/* DONE-SUMMARY.md on the device: which parts of the done summary are shown, their words, what gives way when
 * the line is too wide, and how the model keeps the summary (cleared by a change of state, replaced whole by
 * a delta that carries any of it). */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "done.h"

void dbg_event(const char *c, const char *e, const char *f, ...) { (void)c; (void)e; (void)f; }
void dbg_event_at(dbg_level_t l, const char *c, const char *e, const char *f, ...) { (void)l; (void)c; (void)e; (void)f; }
const char *dbg_q(const char *s) { return s; }

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

/* 10 px per character; a 3-byte icon counts as one */
static int measure(const char *t)
{
    int n = 0;
    for (; *t; t++)
        if (((unsigned char)*t & 0xC0) != 0x80) n++;
    return n * 10;
}

static void line(const done_sum_t *d, int max_w, char *out, size_t cap, done_line_t *l)
{
    done_line_build(d, " \xC2\xB7 ", max_w, measure, l);
    done_line_text(l, out, cap);
}

int main(void)
{
    char txt[160];
    done_line_t l;
    done_sum_t none = {0};
    EXPECT(!done_known(&none), "nothing known: no line");

    done_sum_t all = {.files_known = true, .files = 3, .lines_known = true, .added = 120, .removed = 14,
                      .tests_known = true, .tests_pass = true, .secs_known = true, .secs = 252};
    line(&all, 1000, txt, sizeof txt, &l);
    EXPECT(strstr(txt, "3 files \xC2\xB7 +120 \xE2\x80\x93" "14 \xC2\xB7 ") == txt, "files, lines, in that order");
    EXPECT(strstr(txt, "tests passed \xC2\xB7 4m 12s") != NULL, "tests, then the time");
    EXPECT(!l.fail[DONE_TESTS], "passed tests are not drawn as a failure");

    done_sum_t one = {.files_known = true, .files = 1, .secs_known = true, .secs = 0};
    line(&one, 1000, txt, sizeof txt, &l);
    EXPECT(!strcmp(txt, "1 file \xC2\xB7 <1s"), "one file; a zero duration reads <1s, never 0s");

    /* unknown stays absent: no files figure, no lines, no tests */
    done_sum_t only_time = {.secs_known = true, .secs = 3725};
    line(&only_time, 1000, txt, sizeof txt, &l);
    EXPECT(!strcmp(txt, "1h 02m"), "only the time is known");
    done_sum_t zero_files = {.files_known = true, .files = 0, .secs_known = true, .secs = 9};
    line(&zero_files, 1000, txt, sizeof txt, &l);
    EXPECT(!strcmp(txt, "9s"), "zero files is never shown");

    done_sum_t failed = {.tests_known = true, .tests_pass = false, .secs_known = true, .secs = 37};
    line(&failed, 1000, txt, sizeof txt, &l);
    EXPECT(l.fail[DONE_TESTS] && !l.fail[DONE_TIME], "failed tests are the one part in the error colour");
    EXPECT(strstr(txt, "tests failed \xC2\xB7 37s") != NULL, "tests failed, then the time");

    /* too wide: "tests passed" shortens first, then the lines go, then the time, then the files; the tests stay */
    int full = measure("3 files") + measure(" \xC2\xB7 +120 \xE2\x80\x93" "14") + measure(" \xC2\xB7 \xEE\x86\x84 tests passed") + measure(" \xC2\xB7 4m 12s");
    line(&all, full - 1, txt, sizeof txt, &l);
    EXPECT(l.on[DONE_LINES] && l.on[DONE_TIME] && strstr(txt, "\xEE\x86\x84 passed") && !strstr(txt, "tests"), "the tests word shortens first");
    line(&all, full - 70, txt, sizeof txt, &l);
    EXPECT(!l.on[DONE_LINES] && l.on[DONE_FILES] && l.on[DONE_TIME], "then the lines give way");
    EXPECT(strstr(txt, "3 files \xC2\xB7 ") == txt && !strstr(txt, "+120"), "the separator follows the survivors");
    line(&all, measure("3 files") + measure(" \xC2\xB7 \xEE\x86\x84 passed") + 5, txt, sizeof txt, &l);
    EXPECT(!l.on[DONE_TIME] && l.on[DONE_FILES] && l.on[DONE_TESTS], "then the time");
    line(&all, measure("\xEE\x86\x84 passed") + 5, txt, sizeof txt, &l);
    EXPECT(!l.on[DONE_FILES] && l.on[DONE_TESTS], "then the files");
    EXPECT(strstr(txt, " passed") && txt[0] != ' ', "the tests alone lead without a separator");
    line(&all, 10, txt, sizeof txt, &l);
    EXPECT(l.on[DONE_TESTS], "the tests are never dropped");

    /* the model */
    model_init();
    session_t s = {.sid = 4, .agent = AGENT_CLAUDE, .state = SESS_DONE};
    model_upsert(&s, SF_AGENT | SF_STATE);
    session_t d = {.sid = 4, .sum = all};
    model_upsert(&d, SF_DONE);
    const session_t *m = model_find(4);
    EXPECT(m && m->sum.files_known && m->sum.files == 3 && m->sum.secs == 252, "the summary is stored");
    session_t part = {.sid = 4, .sum = {.secs_known = true, .secs = 90}};
    model_upsert(&part, SF_DONE);
    m = model_find(4);
    EXPECT(!m->sum.files_known && !m->sum.tests_known && m->sum.secs == 90, "a delta with any key replaces the whole summary");
    session_t quiet = {.sid = 4, .summary = "x"};
    model_upsert(&quiet, SF_SUMMARY);
    EXPECT(model_find(4)->sum.secs == 90, "a delta without the keys leaves it");
    session_t work = {.sid = 4, .state = SESS_WORK};
    model_upsert(&work, SF_STATE);
    EXPECT(!done_known(&model_find(4)->sum), "a change of state clears it");

    if (failures) return 1;
    printf("done: all ok\n");
    return 0;
}
