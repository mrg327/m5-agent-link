/* The GRAPH screen (GRAPH.md §4): a `git log --graph` of one agent session's current turn. Newest call at the
 * top; lane 0 (the session's own agent) and its sub-agents' lanes as columns centred on the display; a node is
 * a disc in its lane's colour with the kind's glyph inside, a running one a pulsing ring, a failed one red.
 * Sub-agent lanes fork off lane 0 and merge back in short curves; the fork carries the lane kind's glyph.
 * No text at all: the header is the agent's state glyph.
 *
 * Drawing is one object with a draw callback that paints the visible rows: no per-node objects, no canvas.
 * The store and the fetch schedule are graph.c; this file asks for the graph (g_ui_actions->graph) when
 * graph_due() says so and redraws when the store's rev moves. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "graph.h"
#include "ui_home_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define HEAD_Y 44     /* the state glyph's top */
#define AREA_Y 100    /* the rows' area starts below the header */
#define ROW0_Y 34     /* the top row's centre, in the area */
#define ROW_DY 44
#define LANE_DX 38
#define NODE_R 15     /* disc radius: the 20 px glyph inside */
#define LINE_W 4
#define VIS_ROWS 7    /* rows that fit the area; scrolling stops with the oldest row at the bottom */
#define PULSE_MS 1200 /* one pulse of a running node */
#define PULSE_STEP_MS 66
#define FADE_STEPS 4  /* the fading line below the oldest row when older calls were dropped */

static lv_obj_t *s_root, *s_head, *s_draw;
static uint32_t s_sid, s_now, s_rev, s_pulse_ms;
static int s_top;     /* the row at the top: 0 = the newest node */
static int s_count;   /* nodes when s_top was last set: keeps an older view anchored as new nodes arrive */
static int s_scrub_last = -1, s_drag_acc;
static const char *s_head_glyph;

static const char *const s_kind_glyph[GK_COUNT] = {
    [GK_OTHER] = ICON_G_DOT,     [GK_READ] = ICON_G_READ,  [GK_EDIT] = ICON_G_EDIT,     [GK_SHELL] = ICON_G_SHELL,
    [GK_SEARCH] = ICON_G_SEARCH, [GK_WEB] = ICON_G_WEB,    [GK_AGENT] = ICON_G_ROBOT,   [GK_ASK] = ICON_QUESTION,
    [GK_PLAN] = ICON_G_PLAN,     [GK_GIT] = ICON_G_GIT,    [GK_TEST] = ICON_G_TEST,     [GK_MCP] = ICON_G_MCP,
};
static const char *const s_lane_glyph[8] = {
    [GL_UNKNOWN] = ICON_G_ROBOT, [GL_EXPLORE] = ICON_G_EXPLORE, [GL_TASK] = ICON_G_TASK, [GL_GENERAL] = ICON_G_ROBOT,
    [GL_REVIEW] = ICON_G_REVIEW, [GL_RESEARCH] = ICON_G_RESEARCH, [GL_PLAN] = ICON_G_MAP, [GL_OTHER] = ICON_G_ROBOT,
};

static const char *kind_glyph(uint16_t v) { return GRAPH_KIND(v) < GK_COUNT ? s_kind_glyph[GRAPH_KIND(v)] : ICON_G_DOT; }

static const session_t *session(void) { return model_find(s_sid); }

static int max_top(const graph_t *g) { return g && g->count > VIS_ROWS ? g->count - VIS_ROWS : 0; }

/* Geometry, in the draw object's coordinates offset by its origin (ox, oy). */
typedef struct {
    int ox, oy, lanes;
} geo_t;

static int lane_x(const geo_t *q, int lane) { return q->ox + UI_CX - (q->lanes - 1) * LANE_DX / 2 + lane * LANE_DX; }
/* node i (0 oldest) sits on row count-1-i */
static int node_y(const geo_t *q, const graph_t *g, int i) { return q->oy + ROW0_Y + (g->count - 1 - i - s_top) * ROW_DY; }

static void line_w(lv_layer_t *l, int x1, int y1, int x2, int y2, uint32_t rgb, lv_opa_t opa, int w)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    d.color = lv_color_hex(rgb);
    d.width = w;
    d.opa = opa;
    d.round_start = d.round_end = 1;
    lv_draw_line(l, &d);
}

static void line(lv_layer_t *l, int x1, int y1, int x2, int y2, uint32_t rgb, lv_opa_t opa) { line_w(l, x1, y1, x2, y2, rgb, opa, LINE_W); }

/* A short S curve from (x1, y1) to (x2, y2): a smoothstep in x over the height (or, `across`, in y over the
 * width), in a few segments. */
static void curve_w(lv_layer_t *l, int x1, int y1, int x2, int y2, uint32_t rgb, int w, bool across)
{
    enum { SEG = 6 };
    int px = x1, py = y1;
    for (int k = 1; k <= SEG; k++) {
        int t = k * 256 / SEG; /* 0..256 */
        int s = t * t * (768 - 2 * t) / 65536; /* 3t^2 - 2t^3, scaled to 0..256 */
        int x = across ? x1 + (x2 - x1) * k / SEG : x1 + (x2 - x1) * s / 256;
        int y = across ? y1 + (y2 - y1) * s / 256 : y1 + (y2 - y1) * k / SEG;
        line_w(l, px, py, x, y, rgb, LV_OPA_COVER, w);
        px = x;
        py = y;
    }
}

static void curve(lv_layer_t *l, int x1, int y1, int x2, int y2, uint32_t rgb) { curve_w(l, x1, y1, x2, y2, rgb, LINE_W, false); }

static void glyph(lv_layer_t *l, const char *g, int cx, int cy, lv_color_t col, lv_opa_t opa)
{
    lv_draw_label_dsc_t t;
    lv_draw_label_dsc_init(&t);
    t.font = FONT_ICON_S;
    t.color = col;
    t.opa = opa;
    t.align = LV_TEXT_ALIGN_CENTER;
    t.text = g;
    int lh = lv_font_get_line_height(FONT_ICON_S);
    lv_area_t a = {cx - 16, cy - lh / 2, cx + 15, cy - lh / 2 + lh - 1};
    lv_draw_label(l, &t, &a);
}

/* 0..255 and back over PULSE_MS: the running ring's opacity rides between 40 % and full. */
static lv_opa_t pulse_opa(void)
{
    if (!ui_animations_allowed()) return LV_OPA_COVER;
    uint32_t p = s_pulse_ms % PULSE_MS;
    int tri = (int)(p < PULSE_MS / 2 ? p : PULSE_MS - p) * 255 / (PULSE_MS / 2);
    return (lv_opa_t)(102 + tri * 153 / 255);
}

static void node(lv_layer_t *l, int x, int y, uint16_t v, uint32_t rgb, int r)
{
    int st = (int)GRAPH_STATE(v);
    if (st == GS_RUNNING) {
        lv_draw_rect_dsc_t bg; /* the lane line does not show through the ring */
        lv_draw_rect_dsc_init(&bg);
        bg.radius = LV_RADIUS_CIRCLE;
        bg.bg_color = COL_BG;
        bg.bg_opa = LV_OPA_COVER;
        lv_area_t a = {x - r, y - r, x + r, y + r};
        lv_draw_rect(l, &bg, &a);
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = x;
        d.center.y = y;
        d.radius = (uint16_t)r;
        d.width = r > 8 ? 3 : 2;
        d.start_angle = 0;
        d.end_angle = 360;
        d.color = lv_color_hex(rgb);
        d.opa = pulse_opa();
        lv_draw_arc(l, &d);
        if (r > 8) glyph(l, kind_glyph(v), x, y, lv_color_hex(rgb), d.opa);
        return;
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = lv_color_hex(st == GS_FAILED ? RGB_DANGER : rgb);
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {x - r, y - r, x + r, y + r};
    lv_draw_rect(l, &d, &a);
    if (r > 8) glyph(l, kind_glyph(v), x, y, COL_ON_ACCENT, LV_OPA_COVER); /* a small dot has no room for one */
}

/* Older calls were dropped: the lane fades out below the oldest row. */
static void fade(lv_layer_t *l, int x, int y, uint32_t rgb)
{
    int step = ROW_DY / FADE_STEPS;
    for (int f = 0; f < FADE_STEPS; f++)
        line(l, x, y + f * step, x, y + (f + 1) * step, rgb, (lv_opa_t)(LV_OPA_COVER * (FADE_STEPS - f) / (FADE_STEPS + 1)));
}

/* Lane 0 runs the whole height; a sub-agent's lane from its fork (or from the dropped calls below) to its last
 * node, with a curve off lane 0 at the fork and back into it at the merge. */
static void draw_lanes(lv_layer_t *l, const geo_t *q, const graph_t *g, const session_t *s)
{
    int n = g->count, bottom = node_y(q, g, 0), top = node_y(q, g, n - 1);
    int start[GRAPH_LANES], last[GRAPH_LANES];
    for (int k = 0; k < GRAPH_LANES; k++) start[k] = last[k] = -1;
    for (int i = 0; i < n; i++)
        if (GRAPH_LANE(g->node[i]) != 0 && GRAPH_MERGE(g->node[i]) && node_y(q, g, i) - ROW_DY < top) top = node_y(q, g, i) - ROW_DY;
    uint32_t rgb0 = theme_lane_rgb(s, 0);
    int x0 = lane_x(q, 0);
    if (g->drop) fade(l, x0, bottom, rgb0);
    if (top != bottom) line(l, x0, bottom, x0, top, rgb0, LV_OPA_COVER);
    for (int i = 0; i <= n; i++) {
        int lane = i < n ? (int)GRAPH_LANE(g->node[i]) : 0;
        bool fork = i < n && GRAPH_FORK(g->node[i]), merge = i < n && GRAPH_MERGE(g->node[i]);
        for (int k = 1; k < GRAPH_LANES; k++) {
            /* a run ends at the end of the list or where a new fork takes its lane */
            if (start[k] < 0 || !(i == n || (k == lane && fork))) continue;
            int x = lane_x(q, k);
            if (start[k] != last[k]) line(l, x, node_y(q, g, start[k]), x, node_y(q, g, last[k]), theme_lane_rgb(s, k), LV_OPA_COVER);
            start[k] = -1;
        }
        if (i == n || lane == 0) continue;
        uint32_t rgb = theme_lane_rgb(s, lane);
        int x = lane_x(q, lane), y = node_y(q, g, i);
        if (start[lane] < 0) {
            start[lane] = i;
            if (fork) /* off lane 0 one row older (the parent's agent call), up into the new lane */
                curve(l, x0, i > 0 ? node_y(q, g, i - 1) : y + ROW_DY, x, y, rgb);
            else { /* it began among the dropped calls */
                if (g->drop) fade(l, x, bottom, rgb);
                if (i > 0) line(l, x, bottom, x, y, rgb, LV_OPA_COVER);
            }
        }
        last[lane] = i;
        if (merge) { /* back into lane 0 one row newer */
            if (start[lane] != i) line(l, x, node_y(q, g, start[lane]), x, y, rgb, LV_OPA_COVER);
            curve(l, x, y, x0, y - ROW_DY, rgb);
            start[lane] = -1;
        }
    }
}

static void on_draw(lv_event_t *e)
{
    const session_t *s = session();
    const graph_t *g = graph_get(s_sid);
    if (!s || !g || !g->count) return;
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(s_draw, &c);
    geo_t q = {.ox = c.x1, .oy = c.y1, .lanes = graph_lanes_used(g)};
    draw_lanes(l, &q, g, s);
    for (int i = 0; i < g->count; i++) {
        int y = node_y(&q, g, i);
        if (y < c.y1 - NODE_R || y > c.y2 + NODE_R) continue;
        uint16_t v = g->node[i];
        int lane = (int)GRAPH_LANE(v), x = lane_x(&q, lane);
        uint32_t rgb = theme_lane_rgb(s, lane);
        node(l, x, y, v, rgb, NODE_R);
        if (lane != 0 && GRAPH_FORK(v)) glyph(l, s_lane_glyph[g->la[lane] & 7], x + NODE_R + 14, y, lv_color_hex(rgb), LV_OPA_COVER);
    }
}

/* Only the running nodes change between redraws of the same graph: their rings pulse. */
static void invalidate_running(void)
{
    const graph_t *g = graph_get(s_sid);
    if (!g || !graph_any_running(g)) return;
    lv_area_t c;
    lv_obj_get_coords(s_draw, &c);
    geo_t q = {.ox = c.x1, .oy = c.y1, .lanes = graph_lanes_used(g)};
    for (int i = 0; i < g->count; i++) {
        if (GRAPH_STATE(g->node[i]) != GS_RUNNING) continue;
        int x = lane_x(&q, (int)GRAPH_LANE(g->node[i])), y = node_y(&q, g, i);
        if (y < c.y1 - NODE_R || y > c.y2 + NODE_R) continue;
        lv_area_t a = {x - NODE_R - 2, y - NODE_R - 2, x + NODE_R + 2, y + NODE_R + 2};
        lv_obj_invalidate_area(s_draw, &a);
    }
}

static void render_head(const session_t *s)
{
    const char *gl = theme_state_glyph(s);
    if (gl != s_head_glyph) {
        s_head_glyph = gl;
        lv_label_set_text_static(s_head, gl);
    }
    theme_set_text_color(s_head, lv_color_hex(theme_session_rgb(s)));
}

/* A new graph: an older view stays on the same node as newer ones arrive above it. */
static void take_graph(void)
{
    const graph_t *g = graph_get(s_sid);
    int n = g ? g->count : 0;
    if (s_top > 0 && n > s_count) s_top += n - s_count;
    s_count = n;
    if (s_top > max_top(g)) s_top = max_top(g);
    if (s_top < 0) s_top = 0;
    s_rev = g ? g->rev : 0;
    lv_obj_invalidate(s_draw);
}

static void fetch(uint32_t now)
{
    const session_t *s = session();
    if (s && graph_due(now, s->n, s->step) && g_ui_actions->graph) g_ui_actions->graph(s_sid);
}

void ui_graph_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_now = s_pulse_ms = now;
    s_top = s_count = 0;
    s_scrub_last = -1;
    s_drag_acc = 0;
    s_head_glyph = NULL;
    graph_watch(sid, now);
    const session_t *s = session();
    if (s) render_head(s);
    take_graph();
    fetch(now);
    ui_home_rim_mode(true); /* the rim scrubs the rows */
    ring_set_still(ui_home_ring(), true);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    dbg_event("ui", "graph", "\"sid\":%u", (unsigned)sid);
}

void ui_graph_close(void)
{
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    ring_set_still(ui_home_ring(), !ui_animations_allowed());
    graph_unwatch();
    s_sid = 0;
}

bool ui_graph_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = session();
    if (!s || s->state == SESS_ENDED) return false;
    render_head(s);
    fetch(now); /* n or ok moved: ask again (at most once a second) */
    return true;
}

static void move(int d, const char *via, void (*haptic)(void))
{
    const graph_t *g = graph_get(s_sid);
    int t = s_top + d;
    if (t < 0) t = 0;
    if (t > max_top(g)) t = max_top(g);
    if (t == s_top) return;
    s_top = t;
    lv_obj_invalidate(s_draw);
    if (haptic) haptic();
    dbg_event("ui", "graph_row", "\"top\":%d,\"via\":\"%s\"", s_top, via);
}

bool ui_graph_act(nav_action_t a, const gesture_evt_t *g)
{
    switch (a) {
    case NAV_SCRUB: {
        int d = nav_list_scrub(&s_scrub_last, g->kind, g->x);
        if (d) move(d, "scrub", g_ui_actions->detent);
        return true;
    }
    case NAV_SCROLL_DRAG: { /* the finger pulls the graph: dragging up brings older rows in */
        int d = nav_list_drag(&s_drag_acc, g->dy);
        if (d) move(d, "drag", g_ui_actions->detent);
        return true;
    }
    case NAV_NEXT: move(1, "btn", g_ui_actions->tick); return true;
    default: return false;
    }
}

void ui_graph_tick(uint32_t now)
{
    s_now = now;
    if (!s_sid) return;
    fetch(now);
    const graph_t *g = graph_get(s_sid);
    if ((g ? g->rev : 0) != s_rev) take_graph();
    if (ui_animations_allowed() && now - s_pulse_ms >= PULSE_STEP_MS) {
        s_pulse_ms = now - (now - s_pulse_ms) % PULSE_STEP_MS;
        invalidate_running();
    }
}

void ui_graph_report(ui_report_t *r)
{
    const graph_t *g = graph_get(s_sid);
    r->has_sel_sid = session() != NULL;
    r->sel_sid = s_sid;
    snprintf(r->screen, sizeof r->screen, "graph");
    /* for tests only: the screen itself has no text */
    int run = 0, fail = 0, forks = 0, merges = 0;
    for (int i = 0; g && i < g->count; i++) {
        run += GRAPH_STATE(g->node[i]) == GS_RUNNING;
        fail += GRAPH_STATE(g->node[i]) == GS_FAILED;
        forks += (int)GRAPH_FORK(g->node[i]);
        merges += (int)GRAPH_MERGE(g->node[i]);
    }
    ui_report_add(r, "nodes %d lanes %d top %d", g ? g->count : 0, graph_lanes_used(g), s_top);
    ui_report_add(r, "running %d failed %d forks %d merges %d drop %u", run, fail, forks, merges, g ? (unsigned)g->drop : 0u);
}

lv_obj_t *ui_graph_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_head = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_head, FONT_ICON_M, 0);
    lv_obj_set_style_text_align(s_head, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_head, 60);
    lv_obj_set_pos(s_head, UI_CX - 30, HEAD_Y);
    lv_label_set_text_static(s_head, "");

    /* the rows' area: LVGL clips the drawing to it, so rows scroll under the header and never over it */
    s_draw = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_draw);
    lv_obj_set_size(s_draw, UI_SIZE, UI_SIZE - AREA_Y - 24);
    lv_obj_set_pos(s_draw, 0, AREA_Y);
    lv_obj_remove_flag(s_draw, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_draw, on_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}

/* ───────── the deck strip (GRAPH.md §5) ─────────
 * The same graph on the deck card, in its free button slot, turned on its side: time runs left to right, the
 * newest call at the right edge. Lane 0's calls are discs with their glyph; a sub-agent's calls are dots on a
 * row above (lane 1 the first row, the rest the second), forking off lane 0 and merging back in short curves.
 * Whatever does not fit fades out at the left. A tap on it opens GRAPH. */
#define STRIP_W 300
#define STRIP_H 56
#define STRIP_R 13     /* a lane 0 disc: the 20 px glyph inside */
#define STRIP_DOT_R 5  /* a sub-agent's call */
#define STRIP_GAP 4    /* between two neighbours' edges */
#define STRIP_ROW_DY 12
#define STRIP_LINE_W 3

static lv_obj_t *s_strip;
static uint32_t s_strip_sid, s_strip_rev, s_strip_pulse;
static bool s_strip_shown;
static int16_t s_sx[GRAPH_NODES], s_sy[GRAPH_NODES]; /* where the last draw put each node; 0 = not drawn */

static int strip_r(uint16_t v) { return GRAPH_LANE(v) ? STRIP_DOT_R : STRIP_R; }

static void strip_draw(lv_event_t *e)
{
    const session_t *s = model_find(s_strip_sid);
    const graph_t *g = graph_get(s_strip_sid);
    memset(s_sx, 0, sizeof s_sx);
    if (!s || !g || !g->count) return;
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(s_strip, &c);
    int n = g->count, first = n, left = c.x1 + 2;
    bool sub = graph_lanes_used(g) > 1;
    int y0 = sub ? c.y2 - STRIP_R - 1 : (c.y1 + c.y2) / 2, row1 = y0 - STRIP_R - 8;
    /* newest first, from the right edge, until the next one would not fit */
    for (int i = n - 1, x = c.x2 - STRIP_R - 1; i >= 0; i--) {
        if (i < n - 1) x -= strip_r(g->node[i + 1]) + strip_r(g->node[i]) + STRIP_GAP;
        if (x - strip_r(g->node[i]) < left) break;
        int lane = (int)GRAPH_LANE(g->node[i]);
        s_sx[i] = (int16_t)x;
        s_sy[i] = (int16_t)(lane == 0 ? y0 : row1 - (lane > 1 ? STRIP_ROW_DY : 0));
        first = i;
    }
    uint32_t rgb0 = theme_lane_rgb(s, 0);
    bool cut = first > 0 || g->drop; /* older calls than the leftmost one: lane 0 fades in from the edge */
    int x_old = cut ? left + 24 : s_sx[first];
    if (cut)
        for (int f = 0; f < FADE_STEPS; f++)
            line_w(l, left + f * 6, y0, left + (f + 1) * 6, y0, rgb0, (lv_opa_t)(LV_OPA_COVER * (f + 1) / (FADE_STEPS + 1)), STRIP_LINE_W);
    if (s_sx[n - 1] > x_old) line_w(l, x_old, y0, s_sx[n - 1], y0, rgb0, LV_OPA_COVER, STRIP_LINE_W);
    int start[GRAPH_LANES];
    for (int k = 0; k < GRAPH_LANES; k++) start[k] = -1;
    for (int i = first; i <= n; i++) {
        int lane = i < n ? (int)GRAPH_LANE(g->node[i]) : 0;
        bool fork = i < n && GRAPH_FORK(g->node[i]), merge = i < n && GRAPH_MERGE(g->node[i]);
        for (int k = 1; k < GRAPH_LANES; k++) /* a run ends at the newest call or where a new fork takes its lane */
            if (start[k] >= 0 && (i == n || (k == lane && fork))) {
                int last = start[k];
                for (int j = start[k]; j < i; j++)
                    if ((int)GRAPH_LANE(g->node[j]) == k) last = j;
                if (last != start[k]) line_w(l, s_sx[start[k]], s_sy[start[k]], s_sx[last], s_sy[last], theme_lane_rgb(s, k), LV_OPA_COVER, STRIP_LINE_W);
                start[k] = -1;
            }
        if (i == n || lane == 0) continue;
        uint32_t rgb = theme_lane_rgb(s, lane);
        if (start[lane] < 0) {
            start[lane] = i;
            if (fork) curve_w(l, i > first ? s_sx[i - 1] : s_sx[i] - 12, y0, s_sx[i], s_sy[i], rgb, STRIP_LINE_W, true);
            else line_w(l, left, s_sy[i], s_sx[i], s_sy[i], rgb, LV_OPA_50, STRIP_LINE_W); /* it began further back */
        }
        if (merge) { /* back into lane 0 at the next call */
            if (start[lane] != i) line_w(l, s_sx[start[lane]], s_sy[i], s_sx[i], s_sy[i], rgb, LV_OPA_COVER, STRIP_LINE_W);
            curve_w(l, s_sx[i], s_sy[i], i + 1 < n ? s_sx[i + 1] : s_sx[i] + 12, y0, rgb, STRIP_LINE_W, true);
            start[lane] = -1;
        }
    }
    for (int i = first; i < n; i++) {
        uint16_t v = g->node[i];
        node(l, s_sx[i], s_sy[i], v, theme_lane_rgb(s, (int)GRAPH_LANE(v)), strip_r(v));
    }
}

void ui_graph_strip_create(lv_obj_t *parent, int cy)
{
    s_strip = lv_obj_create(parent);
    lv_obj_remove_style_all(s_strip);
    lv_obj_set_size(s_strip, STRIP_W, STRIP_H);
    lv_obj_set_pos(s_strip, UI_CX - STRIP_W / 2, cy - STRIP_H / 2);
    lv_obj_remove_flag(s_strip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_strip, strip_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
}

bool ui_graph_strip_update(const session_t *s, bool room)
{
    if (ui_mode() != MODE_HOME) return s_strip_shown; /* the card is redrawn behind another screen: leave it be */
    uint32_t sid = s && room && !theme_is_job(s) && s->state != SESS_ENDED && model_peer_minor() >= GRAPH_MINOR ? s->sid : 0;
    if (sid != s_strip_sid) {
        s_strip_sid = sid;
        s_strip_rev = 0;
    }
    if (sid && graph_watched() != sid) graph_watch_every(sid, lv_tick_get(), GRAPH_DECK_POLL_MS);
    else if (!sid && graph_watched()) graph_unwatch(); /* ambient, a request, another kind of card: stop asking */
    const graph_t *g = graph_get(sid);
    bool shown = g && g->count;
    if (shown != s_strip_shown) {
        s_strip_shown = shown;
        if (shown) lv_obj_remove_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
    }
    if (shown && g->rev != s_strip_rev) {
        s_strip_rev = g->rev;
        lv_obj_invalidate(s_strip);
    }
    return shown;
}

void ui_graph_strip_tick(uint32_t now)
{
    if (!s_strip_sid) return;
    const session_t *s = model_find(s_strip_sid);
    if (s && graph_due(now, s->n, s->step) && g_ui_actions->graph) g_ui_actions->graph(s_strip_sid);
    const graph_t *g = graph_get(s_strip_sid);
    if ((g && g->count) != s_strip_shown) ui_home_card_update(); /* the slot changes hands */
    else if (g && g->rev != s_strip_rev) {
        s_strip_rev = g->rev;
        lv_obj_invalidate(s_strip);
    }
    if (!s_strip_shown || !ui_animations_allowed() || !graph_any_running(g) || now - s_strip_pulse < PULSE_STEP_MS) return;
    s_strip_pulse = s_pulse_ms = now;
    for (int i = 0; i < g->count; i++) {
        if (GRAPH_STATE(g->node[i]) != GS_RUNNING || !s_sx[i]) continue;
        int r = strip_r(g->node[i]) + 2;
        lv_area_t a = {s_sx[i] - r, s_sy[i] - r, s_sx[i] + r, s_sy[i] + r};
        lv_obj_invalidate_area(s_strip, &a);
    }
}

bool ui_graph_strip_hit(int x, int y)
{
    if (!s_strip_shown) return false;
    lv_area_t c;
    lv_obj_get_coords(s_strip, &c);
    return x >= c.x1 - 8 && x <= c.x2 + 8 && y >= c.y1 - 10 && y <= c.y2 + 10; /* a fingertip's slack */
}

void ui_graph_strip_report(ui_report_t *r)
{
    if (!s_strip_shown) return;
    const graph_t *g = graph_get(s_strip_sid);
    int drawn = 0;
    for (int i = 0; g && i < g->count; i++) drawn += s_sx[i] != 0;
    ui_report_add(r, "strip %d of %d", drawn, g ? g->count : 0); /* for tests only: the strip has no text */
}
