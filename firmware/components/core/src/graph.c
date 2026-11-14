/* The agent graph store and its fetch schedule (graph.h, GRAPH.md). */
#include "graph.h"

#include <string.h>

static graph_t s_g;
static struct {
    uint32_t sid, n, last_ms, poll_ms;
    uint8_t step;
    bool sent, changed;
} s_w;

void graph_reset(void)
{
    uint32_t rev = s_g.rev;
    memset(&s_g, 0, sizeof s_g);
    s_g.rev = rev + 1;
    memset(&s_w, 0, sizeof s_w);
}

const graph_t *graph_get(uint32_t sid) { return sid && s_g.have && s_g.sid == sid ? &s_g : NULL; }

void graph_on_msg(uint32_t sid, const uint32_t *node, size_t n, const uint32_t *la, size_t nla, uint32_t drop,
                  uint32_t gen)
{
    if (!sid || (s_w.sid && sid != s_w.sid)) return;
    size_t skip = n > GRAPH_NODES ? n - GRAPH_NODES : 0;
    graph_t g = {.sid = sid, .count = (uint8_t)(n - skip), .drop = drop + (uint32_t)skip, .gen = gen, .have = true};
    for (size_t i = 0; i < g.count; i++) g.node[i] = (uint16_t)(node[skip + i] & 0x7ffu);
    for (size_t i = 0; i < nla && i < GRAPH_LANES; i++) g.la[i] = (uint8_t)(la[i] > GL_OTHER ? GL_OTHER : la[i]);
    if (s_g.have && s_g.sid == g.sid && s_g.count == g.count && s_g.drop == g.drop && s_g.gen == g.gen &&
        !memcmp(s_g.node, g.node, sizeof g.node) && !memcmp(s_g.la, g.la, sizeof g.la))
        return;
    g.rev = s_g.rev + 1;
    s_g = g;
}

bool graph_any_running(const graph_t *g)
{
    for (int i = 0; g && i < g->count; i++)
        if (GRAPH_STATE(g->node[i]) == GS_RUNNING) return true;
    return false;
}

int graph_lanes_used(const graph_t *g)
{
    int m = 0;
    for (int i = 0; g && i < g->count; i++)
        if ((int)GRAPH_LANE(g->node[i]) > m) m = (int)GRAPH_LANE(g->node[i]);
    return m + 1;
}

void graph_watch(uint32_t sid, uint32_t now_ms) { graph_watch_every(sid, now_ms, GRAPH_POLL_MS); }

void graph_watch_every(uint32_t sid, uint32_t now_ms, uint32_t poll_ms)
{
    (void)now_ms;
    if (sid != s_g.sid) {
        uint32_t rev = s_g.rev;
        memset(&s_g, 0, sizeof s_g);
        s_g.rev = rev + 1;
    }
    memset(&s_w, 0, sizeof s_w);
    s_w.sid = sid;
    s_w.poll_ms = poll_ms;
}

void graph_unwatch(void) { memset(&s_w, 0, sizeof s_w); }

uint32_t graph_watched(void) { return s_w.sid; }

bool graph_due(uint32_t now_ms, uint32_t n, uint8_t step)
{
    if (!s_w.sid) return false;
    if (s_w.sent && (n != s_w.n || step != s_w.step)) s_w.changed = true;
    s_w.n = n;
    s_w.step = step;
    uint32_t since = now_ms - s_w.last_ms;
    bool due = !s_w.sent || since >= s_w.poll_ms || (s_w.changed && since >= GRAPH_CHANGE_MS);
    if (!due) return false;
    s_w.sent = true;
    s_w.changed = false;
    s_w.last_ms = now_ms;
    return true;
}
