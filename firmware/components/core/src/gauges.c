#include "gauges.h"

#include <stdio.h>
#include <string.h>

static usage_t s_usage;
static uint32_t s_warned_resets; /* the 5-hour window the warning buzzed for */

void gauges_reset(void)
{
    memset(&s_usage, 0, sizeof s_usage);
    s_warned_resets = 0;
}

void gauges_set_usage(const usage_t *u)
{
    s_usage = *u;
    model_touch();
}

const usage_t *gauges_usage(void) { return &s_usage; }

bool gauges_live(const usage_win_t *w, uint32_t at, uint32_t now)
{
    if (!w->known || at == 0 || now <= 100000) return false; /* unknown, or the clock is not set */
    if (now >= w->resets) return false;                       /* the window has reset: the old figure is wrong */
    return now < at + GAUGES_STALE_S || at > now;              /* a reading from the future is the clocks disagreeing: show it */
}

static bool pick(const usage_win_t *w, uint32_t now, usage_win_t *out)
{
    if (!gauges_live(w, s_usage.at, now)) return false;
    if (out) *out = *w;
    return true;
}

bool gauges_h5(uint32_t now, usage_win_t *out) { return pick(&s_usage.h5, now, out); }
bool gauges_wk(uint32_t now, usage_win_t *out) { return pick(&s_usage.wk, now, out); }

/* Two readings of one 5-hour window: its reset time stays put (within ten minutes). */
static bool same_window(const usage_win_t *a, const usage_win_t *b)
{
    uint32_t d = a->resets > b->resets ? a->resets - b->resets : b->resets - a->resets;
    return d <= 600;
}

bool gauges_should_warn(const usage_t *prev, const usage_t *now)
{
    const usage_win_t *p = &prev->h5, *n = &now->h5;
    if (!p->known || !n->known || !same_window(p, n)) return false;
    if (p->pct >= GAUGES_WARN_PCT || n->pct < GAUGES_WARN_PCT) return false;
    if (s_warned_resets && same_window(n, &(usage_win_t){.resets = s_warned_resets})) return false;
    s_warned_resets = n->resets;
    return true;
}

void gauges_span(char *dst, size_t cap, uint32_t secs)
{
    uint32_t m = (secs + 59) / 60;
    if (m < 60) snprintf(dst, cap, "%um", (unsigned)(m ? m : 1));
    else if (m < 48 * 60) {
        unsigned h = (unsigned)(m / 60), mm = (unsigned)(m % 60);
        if (mm) snprintf(dst, cap, "%uh %um", h, mm);
        else snprintf(dst, cap, "%uh", h);
    } else {
        unsigned d = (unsigned)(m / (24 * 60)), h = (unsigned)(m / 60 % 24);
        if (h) snprintf(dst, cap, "%ud %uh", d, h);
        else snprintf(dst, cap, "%ud", d);
    }
}

void gauges_clock(char *dst, size_t cap, uint32_t at, int32_t tz_off_min)
{
    uint32_t local = at + (uint32_t)(tz_off_min * 60);
    snprintf(dst, cap, "%u:%02u", (unsigned)(local / 3600 % 24), (unsigned)(local / 60 % 60));
}

void gauges_detail(char *dst, size_t cap, const usage_win_t *w, uint32_t now, int32_t tz_off_min)
{
    char t[16];
    if (w->pct >= 100) {
        gauges_clock(t, sizeof t, w->resets, tz_off_min);
        snprintf(dst, cap, "limit until %s", t);
    } else if (w->out && w->out > now && w->out < w->resets) {
        gauges_clock(t, sizeof t, w->out, tz_off_min);
        snprintf(dst, cap, "runs out %s", t);
    } else {
        gauges_span(t, sizeof t, w->resets > now ? w->resets - now : 0);
        snprintf(dst, cap, "resets in %s", t);
    }
}

const char *gauges_mode_word(uint8_t pm)
{
    switch (pm) {
    case PM_PLAN: return "Plan";
    case PM_ACCEPT_EDITS: return "Accept edits";
    case PM_AUTO: return "Auto";
    case PM_DONT_ASK: return "Don't ask";
    case PM_BYPASS: return "Bypass";
    default: return NULL;
    }
}

void gauges_readout(char *dst, size_t cap, const session_t *s, const char *sep)
{
    static const char *const review[] = {"", " in review", " approved", " changes", " draft"};
    const char *parts[3];
    char pr[40];
    int n = 0;
    if (s->mdl[0]) parts[n++] = s->mdl;
    const char *mode = s->pm_known ? gauges_mode_word(s->pm) : NULL;
    if (mode) parts[n++] = mode;
    if (s->pr) {
        snprintf(pr, sizeof pr, "PR %u%s", (unsigned)s->pr, s->prs < 5 ? review[s->prs] : "");
        parts[n++] = pr;
    }
    dst[0] = 0;
    size_t len = 0;
    for (int i = 0; i < n && len < cap; i++)
        len += (size_t)snprintf(dst + len, cap - len, "%s%s", i ? sep : "", parts[i]);
}
