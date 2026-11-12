/* GAUGES.md on the device: when a usage figure may be shown (never a zero for "unknown"), the one buzz at
 * 80 %, and the words of the detail line and the AGENT readout. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "gauges.h"

void dbg_event(const char *c, const char *e, const char *f, ...) { (void)c; (void)e; (void)f; }
void dbg_event_at(dbg_level_t l, const char *c, const char *e, const char *f, ...) { (void)l; (void)c; (void)e; (void)f; }
const char *dbg_q(const char *s) { return s; }

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)
#define T0 1800000000u

static usage_t reading(uint32_t at, int h5, uint32_t resets)
{
    usage_t u = {.at = at};
    if (h5 >= 0) u.h5 = (usage_win_t){true, (uint8_t)h5, resets, 0};
    return u;
}

int main(void)
{
    model_init();
    gauges_reset();
    usage_win_t w;

    /* liveness */
    EXPECT(!gauges_h5(T0, &w), "nothing received: unknown");
    usage_t u = reading(T0, 42, T0 + 3600);
    gauges_set_usage(&u);
    EXPECT(gauges_h5(T0 + 10, &w) && w.pct == 42, "a fresh reading is shown");
    EXPECT(!gauges_wk(T0 + 10, &w), "an absent window stays unknown, not 0 %");
    EXPECT(!gauges_h5(50, &w), "the clock is not set: unknown");
    EXPECT(!gauges_h5(T0 + 3600, &w), "past its reset: unknown");
    u = reading(T0, 42, T0 + 3 * 3600);
    gauges_set_usage(&u);
    EXPECT(gauges_h5(T0 + GAUGES_STALE_S - 1, &w), "just under an hour old: shown");
    EXPECT(!gauges_h5(T0 + GAUGES_STALE_S, &w), "an hour old: stale");

    /* the 80 % buzz: once, only on a crossing seen within one window */
    usage_t a = reading(T0, 79, T0 + 3600), b = reading(T0 + 60, 80, T0 + 3600 + 30), c = reading(T0 + 120, 85, T0 + 3600);
    EXPECT(gauges_should_warn(&a, &b), "79 -> 80 in the same window buzzes");
    EXPECT(!gauges_should_warn(&b, &c), "80 -> 85: already over");
    EXPECT(!gauges_should_warn(&a, &c), "and not twice in that window, whatever the order");
    usage_t none = {0}, hi = reading(T0, 90, T0 + 3600);
    EXPECT(!gauges_should_warn(&none, &hi), "connecting at 90 %: it never crossed in view");
    usage_t next_lo = reading(T0 + 4000, 70, T0 + 4 * 3600), next_hi = reading(T0 + 5000, 81, T0 + 4 * 3600);
    EXPECT(!gauges_should_warn(&c, &next_lo), "a new window starts low");
    EXPECT(gauges_should_warn(&next_lo, &next_hi), "and buzzes again when it crosses");
    usage_t other = reading(T0, 79, T0 + 9000);
    EXPECT(!gauges_should_warn(&other, &hi), "two different windows are not a crossing");

    /* words */
    char buf[64];
    gauges_span(buf, sizeof buf, 7800);
    EXPECT(!strcmp(buf, "2h 10m"), "2h 10m");
    gauges_span(buf, sizeof buf, 61);
    EXPECT(!strcmp(buf, "2m"), "rounded up to the minute");
    gauges_span(buf, sizeof buf, 3 * 86400 + 4 * 3600);
    EXPECT(!strcmp(buf, "3d 4h"), "3d 4h");
    usage_win_t ww = {true, 42, T0 + 7800, 0};
    gauges_detail(buf, sizeof buf, &ww, T0, 0);
    EXPECT(!strcmp(buf, "resets in 2h 10m"), buf);
    ww.out = T0 + 3600; /* T0 is 08:00 UTC: 09:00, one hour east 10:00 */
    gauges_detail(buf, sizeof buf, &ww, T0, 60);
    EXPECT(!strcmp(buf, "runs out 10:00"), buf);
    ww.pct = 100;
    gauges_detail(buf, sizeof buf, &ww, T0, 0);
    EXPECT(!strcmp(buf, "limit until 10:10"), buf);

    session_t s = {0};
    gauges_readout(buf, sizeof buf, &s, " · ");
    EXPECT(buf[0] == 0, "nothing known: no line");
    snprintf(s.mdl, sizeof s.mdl, "Opus 4.7");
    s.pm_known = true;
    s.pm = PM_DEFAULT;
    gauges_readout(buf, sizeof buf, &s, " · ");
    EXPECT(!strcmp(buf, "Opus 4.7"), "the default mode is not shown");
    s.pm = PM_PLAN;
    s.pr = 128;
    s.prs = PRS_APPROVED;
    gauges_readout(buf, sizeof buf, &s, " · ");
    EXPECT(!strcmp(buf, "Opus 4.7 · Plan · PR 128 approved"), buf);
    s.mdl[0] = 0;
    s.pm = PM_ACCEPT_EDITS;
    s.prs = PRS_NONE;
    gauges_readout(buf, sizeof buf, &s, " · ");
    EXPECT(!strcmp(buf, "Accept edits · PR 128"), buf);

    printf("%s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
