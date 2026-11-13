/* GRAPH.md on the device: the agent graph off the wire (the zcbor codec), the store for the one watched session,
 * the node bits, and the fetch schedule (on open, on a change of n or ok at most once a second, every 3 s). */
#include <stdio.h>
#include <string.h>

#include "agentlink_codec_decode.h"
#include "agentlink_codec_encode.h"
#include "graph.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

static struct envelope_r s_env;

/* Encodes a graph envelope as the daemon would, then decodes it as link_rx.c does. */
static const struct graph *round_trip(uint32_t sid, const uint32_t *node, size_t n, const uint32_t *la, size_t nla,
                                      bool has_drop, uint32_t drop)
{
    static uint8_t buf[512];
    memset(&s_env, 0, sizeof s_env);
    s_env.envelope_choice = envelope_msg_graph_m_c;
    struct msg_graph *m = &s_env.envelope_msg_graph_m;
    m->msg_graph_hdr_m.hdr_uint1uint = 7;
    struct graph *g = &m->msg_graph_graph_m;
    g->graph_uint0uint = sid;
    memcpy(g->graph_node_l_node, node, n * sizeof *node);
    g->graph_node_l_node_count = n;
    if (nla) {
        g->graph_la_l_present = true;
        memcpy(g->graph_la_l.graph_la_l_la, la, nla * sizeof *la);
        g->graph_la_l.graph_la_l_la_count = nla;
    }
    g->graph_uint3uint_present = has_drop;
    g->graph_uint3uint.graph_uint3uint = drop;
    g->graph_uint4uint_present = true;
    g->graph_uint4uint.graph_uint4uint = 3;
    size_t len = 0;
    if (cbor_encode_envelope(buf, sizeof buf, &s_env, &len) != ZCBOR_SUCCESS) return NULL;
    memset(&s_env, 0, sizeof s_env);
    if (cbor_decode_envelope(buf, len, &s_env, NULL) != ZCBOR_SUCCESS) return NULL;
    if (s_env.envelope_choice != envelope_msg_graph_m_c) return NULL;
    return &s_env.envelope_msg_graph_m.msg_graph_graph_m;
}

static void deliver(const struct graph *g)
{
    graph_on_msg(g->graph_uint0uint, g->graph_node_l_node, g->graph_node_l_node_count, g->graph_la_l.graph_la_l_la,
                 g->graph_la_l_present ? g->graph_la_l.graph_la_l_la_count : 0,
                 g->graph_uint3uint_present ? g->graph_uint3uint.graph_uint3uint : 0,
                 g->graph_uint4uint_present ? g->graph_uint4uint.graph_uint4uint : 0);
}

int main(void)
{
    /* bits: 0-3 kind, 4-5 state, 6-8 lane, 9 fork, 10 merge */
    uint16_t v = GRAPH_NODE(GK_TEST, GS_FAILED, 5, 1, 1);
    EXPECT(v == (10 | 2 << 4 | 5 << 6 | 1 << 9 | 1 << 10), "node packing follows GRAPH.md §3");
    EXPECT(GRAPH_KIND(v) == GK_TEST && GRAPH_STATE(v) == GS_FAILED && GRAPH_LANE(v) == 5 && GRAPH_FORK(v) && GRAPH_MERGE(v),
           "node accessors");

    /* main lane, a forked and merged sub-agent, a running node, a failed node */
    uint32_t nodes[] = {GRAPH_NODE(GK_READ, GS_OK, 0, 0, 0), GRAPH_NODE(GK_AGENT, GS_OK, 0, 0, 0),
                        GRAPH_NODE(GK_SEARCH, GS_OK, 1, 1, 0), GRAPH_NODE(GK_READ, GS_OK, 1, 0, 1),
                        GRAPH_NODE(GK_SHELL, GS_FAILED, 0, 0, 0), GRAPH_NODE(GK_EDIT, GS_RUNNING, 0, 0, 0)};
    uint32_t la[] = {0, 1};
    const struct graph *w = round_trip(4, nodes, 6, la, 2, true, 9);
    EXPECT(w && w->graph_node_l_node_count == 6 && w->graph_la_l_present && w->graph_la_l.graph_la_l_la_count == 2,
           "a graph survives the codec");
    w = round_trip(4, nodes, 0, NULL, 0, false, 0);
    EXPECT(w && w->graph_node_l_node_count == 0 && !w->graph_la_l_present && !w->graph_uint3uint_present,
           "an empty graph with no la or drop decodes");

    graph_reset();
    graph_watch(4, 0);
    EXPECT(graph_get(4) == NULL, "nothing before the first graph");
    w = round_trip(4, nodes, 6, la, 2, true, 9);
    deliver(w);
    const graph_t *g = graph_get(4);
    EXPECT(g && g->count == 6 && g->drop == 9 && g->gen == 3 && g->la[1] == GL_EXPLORE, "stored");
    EXPECT(g && GRAPH_FORK(g->node[2]) && GRAPH_MERGE(g->node[3]) && GRAPH_STATE(g->node[4]) == GS_FAILED,
           "fork, merge and failed bits kept");
    EXPECT(graph_any_running(g) && graph_lanes_used(g) == 2, "running node and lanes in use");
    uint32_t rev = g ? g->rev : 0;
    deliver(w);
    EXPECT(graph_get(4)->rev == rev, "the same graph again does not redraw");
    nodes[5] = GRAPH_NODE(GK_EDIT, GS_OK, 0, 0, 0);
    deliver(round_trip(4, nodes, 6, la, 2, true, 9));
    EXPECT(graph_get(4)->rev != rev && !graph_any_running(graph_get(4)), "a settled node is a new graph");
    deliver(round_trip(5, nodes, 6, la, 2, false, 0));
    EXPECT(graph_get(5) == NULL && graph_get(4) != NULL, "another session's graph is not kept");
    graph_watch(5, 0);
    EXPECT(graph_get(4) == NULL, "watching another session forgets the old graph");

    /* the decoder caps at 64 nodes; the store keeps the newest of a longer list and counts the rest */
    uint32_t many[70];
    for (int i = 0; i < 70; i++) many[i] = GRAPH_NODE(i % GK_COUNT, GS_OK, 0, 0, 0);
    graph_on_msg(5, many, 70, NULL, 0, 1, 0);
    g = graph_get(5);
    EXPECT(g && g->count == GRAPH_NODES && g->drop == 7 && GRAPH_KIND(g->node[0]) == 6 % GK_COUNT, "newest 64 kept");
    uint32_t big_la[] = {9};
    graph_on_msg(5, many, 1, big_la, 1, 0, 0);
    EXPECT(graph_get(5)->la[0] == GL_OTHER, "an unknown lane kind reads as other");

    /* fetch schedule */
    graph_watch(6, 1000);
    EXPECT(graph_due(1000, 3, 0), "asks on open");
    EXPECT(!graph_due(1100, 3, 0), "not again at once");
    EXPECT(!graph_due(1500, 4, 0), "n moved: waits for a second since the last ask");
    EXPECT(graph_due(2000, 4, 0), "then asks");
    EXPECT(!graph_due(2500, 4, 1), "ok moved half a second later: waits");
    EXPECT(graph_due(3000, 4, 1), "asks a second after the last");
    EXPECT(!graph_due(5999, 4, 1), "unchanged: nothing before 3 s");
    EXPECT(graph_due(6000, 4, 1), "every 3 s while open");
    graph_unwatch();
    EXPECT(!graph_due(20000, 9, 0), "closed: never");

    if (failures) printf("%d failure(s)\n", failures);
    else printf("graph: all passed\n");
    return failures ? 1 : 0;
}
