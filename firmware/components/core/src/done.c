/* See done.h. */
#include <stdio.h>
#include <string.h>

#include "done.h"
#include "ui_icons.h"

bool done_known(const done_sum_t *d)
{
    return d->files_known || d->lines_known || d->tests_known || d->secs_known;
}

/* "4m 12s", "1h 04m"; under a second "<1s" (a duration of 0 would read as unknown). */
static void span(char *dst, size_t cap, uint32_t secs)
{
    if (secs == 0) snprintf(dst, cap, "<1s");
    else if (secs < 60) snprintf(dst, cap, "%us", (unsigned)secs);
    else if (secs < 3600) snprintf(dst, cap, "%um %02us", (unsigned)(secs / 60), (unsigned)(secs % 60));
    else snprintf(dst, cap, "%uh %02um", (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60));
}

static void compose(const done_sum_t *d, const char *sep, bool short_tests, done_line_t *out)
{
    char body[DONE_SEGS][48] = {{0}};
    if (d->files_known && d->files) snprintf(body[DONE_FILES], sizeof body[0], "%u file%s", (unsigned)d->files, d->files == 1 ? "" : "s");
    if (d->lines_known)
        snprintf(body[DONE_LINES], sizeof body[0], "+%u \xE2\x80\x93%u", (unsigned)d->added, (unsigned)d->removed);
    if (d->tests_known)
        snprintf(body[DONE_TESTS], sizeof body[0], "%s %s%s", d->tests_pass ? ICON_DONE : ICON_DENIED, short_tests ? "" : "tests ",
                 d->tests_pass ? "passed" : "failed");
    if (d->secs_known) span(body[DONE_TIME], sizeof body[0], d->secs);
    bool first = true;
    for (int i = 0; i < DONE_SEGS; i++) {
        out->on[i] = body[i][0] && out->on[i];
        out->fail[i] = i == DONE_TESTS && out->on[i] && d->tests_known && !d->tests_pass;
        if (!out->on[i]) {
            out->txt[i][0] = 0;
            continue;
        }
        snprintf(out->txt[i], sizeof out->txt[0], "%.6s%.40s", first ? "" : sep, body[i]);
        first = false;
    }
}

void done_line_build(const done_sum_t *d, const char *sep, int max_w, done_measure_fn measure, done_line_t *out)
{
    static const done_seg_t drop_order[] = {DONE_LINES, DONE_TIME, DONE_FILES};
    bool short_tests = false;
    memset(out, 0, sizeof *out);
    for (int i = 0; i < DONE_SEGS; i++) out->on[i] = true;
    compose(d, sep, short_tests, out);
    /* step 0: as it is; step 1: "passed" for "tests passed"; then the parts give way one by one */
    for (size_t k = 0;; k++) {
        int w = 0;
        for (int i = 0; i < DONE_SEGS; i++)
            if (out->on[i]) w += measure(out->txt[i]);
        if (w <= max_w || k >= 1 + sizeof drop_order / sizeof drop_order[0]) break;
        if (k == 0) short_tests = true;
        else out->on[drop_order[k - 1]] = false;
        compose(d, sep, short_tests, out); /* the separator moves to the new first part */
    }
}

void done_line_text(const done_line_t *l, char *dst, size_t cap)
{
    size_t n = 0;
    dst[0] = 0;
    for (int i = 0; i < DONE_SEGS; i++)
        if (l->on[i] && n < cap) n += (size_t)snprintf(dst + n, cap - n, "%s", l->txt[i]);
}
