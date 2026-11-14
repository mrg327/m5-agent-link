/* The agent graph of one session (proto 0.22, GRAPH.md): the tool calls of the current turn as nodes on
 * lanes. The watch keeps the graph of the one session the GRAPH screen shows, and decides when to ask for
 * it again (§4 Fetching). Plain data: no LVGL. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GRAPH_MINOR 22   /* the daemon's proto minor that serves `cmd graph` */
#define GRAPH_NODES 64   /* the newest calls of the turn, oldest first */
#define GRAPH_LANES 8    /* lane 0 is the session's own agent, 1-7 its sub-agents */
#define GRAPH_POLL_MS 3000   /* asked again this often while the screen is open */
#define GRAPH_CHANGE_MS 1000 /* a change of the session's n or ok asks again, at most this often */
#define GRAPH_DECK_POLL_MS 10000 /* the deck strip (§5) leans on the changes and asks rarely otherwise */

/* A node's bits (GRAPH.md §3): 0-3 kind, 4-5 state, 6-8 lane, 9 fork, 10 merge. */
#define GRAPH_KIND(v) ((v) & 0xfu)
#define GRAPH_STATE(v) (((v) >> 4) & 0x3u)
#define GRAPH_LANE(v) (((v) >> 6) & 0x7u)
#define GRAPH_FORK(v) (((v) >> 9) & 1u)
#define GRAPH_MERGE(v) (((v) >> 10) & 1u)
#define GRAPH_NODE(kind, state, lane, fork, merge) \
    ((uint16_t)(((kind) & 0xf) | (((state) & 3) << 4) | (((lane) & 7) << 6) | (((fork) & 1) << 9) | (((merge) & 1) << 10)))

typedef enum {
    GK_OTHER = 0, GK_READ, GK_EDIT, GK_SHELL, GK_SEARCH, GK_WEB, GK_AGENT, GK_ASK, GK_PLAN, GK_GIT, GK_TEST, GK_MCP,
    GK_COUNT
} graph_kind_t;
typedef enum { GS_RUNNING = 0, GS_OK = 1, GS_FAILED = 2 } graph_state_t;
/* Lane kinds (`la`): 0 unknown, 1 explore, 2 task, 3 general-purpose, 4 code-review, 5 research, 6 plan, 7 other. */
typedef enum { GL_UNKNOWN = 0, GL_EXPLORE, GL_TASK, GL_GENERAL, GL_REVIEW, GL_RESEARCH, GL_PLAN, GL_OTHER } graph_lane_kind_t;

typedef struct {
    uint32_t sid;
    uint16_t node[GRAPH_NODES]; /* oldest first */
    uint8_t count;
    uint8_t la[GRAPH_LANES];
    uint32_t drop; /* older calls of the turn left out */
    uint32_t gen;
    uint32_t rev;  /* bumps whenever the stored graph changes: the screen redraws */
    bool have;     /* a graph for sid arrived */
} graph_t;

/* The graph for `sid`, or NULL when the store holds no graph for it. */
const graph_t *graph_get(uint32_t sid);
/* A `graph` message: kept when it is for the session being watched (or none is); a list longer than the caps
 * keeps its newest nodes and counts the rest in drop. */
void graph_on_msg(uint32_t sid, const uint32_t *node, size_t n, const uint32_t *la, size_t nla, uint32_t drop,
                  uint32_t gen);
bool graph_any_running(const graph_t *g);
/* Lanes in use: one more than the highest lane any node sits on (at least 1). */
int graph_lanes_used(const graph_t *g);

/* Fetch schedule (§4): watching a session clears the store when the session differs. graph_due() says whether to
 * ask now, given the session's n and step state; it remembers the time when it says yes. */
void graph_watch(uint32_t sid, uint32_t now_ms);
/* The same, asking again every poll_ms when nothing changes (the deck strip: GRAPH_DECK_POLL_MS). */
void graph_watch_every(uint32_t sid, uint32_t now_ms, uint32_t poll_ms);
void graph_unwatch(void);
uint32_t graph_watched(void);
bool graph_due(uint32_t now_ms, uint32_t n, uint8_t step);
void graph_reset(void);
