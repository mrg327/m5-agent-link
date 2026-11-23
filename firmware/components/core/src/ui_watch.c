/* The WATCH screen in Sigil (docs/design/GLYPHS.md §5 WATCH, UI-V2 §4.7): a live monitor for one
 * session, drawn as a lane of glyph nodes like GRAPH (GRAPH.md §4), turned on its side and blown
 * up. Time runs left to right.
 *
 *   - Who: the project's sigil at the top and the agent's mark under it, as on the deck.
 *   - While the session works, the lane crosses the screen through its centre. The current step is
 *     the hero node in the middle: its tool as a 96 px glyph in a ring (a running step, with a
 *     comet going round), a filled disc once it has returned, a red one when it failed. The steps
 *     before it are smaller discs to its left, shrinking and dimming towards the bezel; where older
 *     steps were dropped the lane fades out. To the right the lane runs on into the step count in a
 *     badge (a count: rule 4) and a few dashes: what is to come.
 *   - Under the lane: the time in this state as a tally of minutes, and the selected step's
 *     argument, which is the one piece of text (rule 1: the agent's step summary). A tool the watch
 *     has a glyph for is not spelled out; any other step keeps its whole text. A tap on an earlier
 *     node selects it; a new step, or a tap on the hero, selects the current one again.
 *   - When the session is not working the lane gives way to its state as a 128 px glyph with the
 *     time in that state as dots round it, and the done row (ui_done.c) under it. A session that
 *     waits for you is opened by a tap on the glyph, as before (nav.c): the report still says
 *     `Answer`.
 *
 * The screen follows the deck's selection: the rim and B move the deck's selection through
 * ui_home_act(), and the watched session follows it. The index ring keeps showing the sessions. The
 * step timeline itself is built by the model (steps.c); this file only draws it. Nothing here is
 * destructive and nothing times out.
 *
 * What every mark means is said in words in the report (rule 9), in the lines the text screen had:
 * the header, `[ok] Read · src/main.c` for each earlier step, the tool, the argument, the state
 * word and its caption, `Answer`, and `Step 3 · 1:35` or the done line. Redraw discipline (UI-V2
 * §8a): the lane is one object; a delta invalidates only the part of it that changed (the earlier
 * nodes, the hero, the badge), and a tick only the two ends of the comet. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_done.h"
#include "ui_home_internal.h"
#include "ui_kit.h"
#include "ui_theme.h"

/* Geometry: centres. The content circle is r 205; the key hints sit at (101, 365) and (365,
 * 365). */
#define SIGIL_Y 50
#define SIGIL_R 22
#define MARK_Y 84
#define LANE_Y 202   /* the lane */
#define STATE_Y 206  /* not working: the state glyph, where the deck has it */
#define LANE_HALF 80 /* the lane object is this high either side of its line */
#define LANE_W 4     /* the line (GRAPH.md: 4 px) */
#define HERO_R 72    /* the current step: a ring of this outer radius, a 96 px glyph inside */
#define HERO_RING 6
#define COMET_DEG 100 /* the bright part of a running ring */
#define COMET_STEP_MS 66
#define COMET_STEP_DEG 12 /* one turn in two seconds */
#define BADGE_X 319       /* the step count: a pill that starts here, on the lane */
#define BADGE_H 30
#define BADGE_PAD 9
#define DASH_W 8 /* what is to come: three dashes that fade */
#define DASH_GAP 8
#define FADE_STEPS 4 /* dropped steps: the lane fades out towards the bezel */
#define FADE_DX 7
#define SEL_GAP 4 /* the selection ring round an earlier node */
#define TALLY_Y 290
#define LINE1_Y 316 /* the argument: two rows, as the deck's summary */
#define LINE2_Y 340
#define LINE1_W 300
#define LINE2_W 220 /* its ends stop short of the key hints' rings */
#define DOTS_R 88   /* not working: the time in state round the glyph */
#define DONE_ROW_Y 320

/* The steps before the current one, by how far back they are (1 = the one before): where, how
 * large, in which icon font and how bright. The furthest one ends 4 px inside the content
 * circle. */
static const struct {
    int16_t x, r;
    uint8_t px, bright;
} k_back[STEPS_MAX] = {
    {UI_CX, HERO_R, 96, 100}, {129, 22, 28, 85}, {83, 17, 20, 70}, {47, 13, 20, 55}};

typedef struct {
    const char *glyph; /* an ICON_* literal */
    uint8_t state;     /* step_state_t */
} node_t;

/* What the lane shows: compared with what it showed, so a delta redraws only what moved. */
typedef struct {
    node_t node[STEPS_MAX]; /* oldest first; the last one is the hero */
    int count;              /* nodes, the hero among them; 0 = no lane (not working) */
    bool older;             /* steps before the first node were dropped */
    uint32_t step;          /* the badge: steps so far this turn; 0 = none */
    uint32_t rgb;
    int sel; /* the selected node (its index); count - 1 = the hero */
} lane_t;

static lv_obj_t *s_root, *s_sigil, *s_marks, *s_lane, *s_tally, *s_line1, *s_line2, *s_hero,
    *s_dots;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static uint32_t s_sid, s_now, s_sel_n, s_last_n, s_comet_ms;
static int s_comet_deg;
static lane_t s_v;
static bool s_working, s_needs, s_done_row, s_comet_on;

/* The words (rule 9): what the report says, as the text screen did. */
static char s_header_txt[64], s_marks_txt[24], s_tool_txt[48], s_arg_txt[48], s_word_txt[24];
MODEL_EXT_BSS static char s_row_txt[STEPS_MAX][STEP_TEXT_MAX + 8];
static const char *s_mark_txt[STEPS_MAX];
static int s_rows;
/* The text on glass and the caches of its two rows (in PSRAM, with the rows above: the screen's
 * larger statics). */
MODEL_EXT_BSS static char s_text[96], s_line1_txt[96], s_line2_txt[96];

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static bool working(const session_t *s) { return s->state == SESS_WORK || s->state == SESS_START; }

/* ───────── words ───────── */

/* Copy at most cap - 1 bytes of src[0..len). */
static void copy_cut(char *dst, size_t cap, const char *src, size_t len)
{
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

/* "Tool · argument" (the daemon's sum) split at the first separator; a bare tool name has no
 * argument. */
static void split_step(const char *text, char *tool, size_t tcap, char *arg, size_t acap)
{
    const char *sep = strstr(text, " \xC2\xB7 ");
    if (sep) {
        copy_cut(tool, tcap, text, (size_t)(sep - text));
        copy_cut(arg, acap, sep + 4, strlen(sep + 4));
    } else {
        copy_cut(tool, tcap, text, strlen(text));
        arg[0] = 0;
    }
}

static void clock_text(char *dst, size_t cap, uint32_t secs)
{
    if (secs < 3600) snprintf(dst, cap, "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
    else
        snprintf(dst, cap, "%u:%02u:%02u", (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60),
                 (unsigned)(secs % 60));
}

/* Time in the current state, frozen at the moment the link dropped, like the deck's card. */
static bool time_in_state(const session_t *s, uint32_t *secs)
{
    uint32_t now = theme_now_unix(), from = s->tst ? s->tst : s->t0;
    if (!now || !from) return false;
    now -= ui_link_down_ms() / 1000;
    *secs = now >= from ? now - from : 0;
    return true;
}

/* The caption under the state word, for the report: how long ago it happened, in the words of the
 * state. */
static void state_caption(const session_t *s, char *dst, size_t cap)
{
    uint32_t secs;
    char t[16];
    dst[0] = 0;
    const char *lead = s->state == SESS_DONE    ? "Finished"
                       : s->state == SESS_ERROR ? "Stopped"
                       : s->state == SESS_INPUT ? "Waiting"
                       : s->state == SESS_IDLE  ? "No activity for"
                                                : "";
    if (!lead[0]) return;
    if (!time_in_state(s, &secs)) {
        if (s->state == SESS_DONE || s->state == SESS_ERROR) snprintf(dst, cap, "%s", lead);
        return;
    }
    theme_elapsed(t, sizeof t, secs);
    if (s->state == SESS_DONE || s->state == SESS_ERROR) snprintf(dst, cap, "%s %s ago", lead, t);
    else snprintf(dst, cap, "%s %s", lead, t);
}

/* The footer, for the report: the step and the time while it works. */
static void footer_words(const session_t *s, char *buf, size_t cap)
{
    char t[16];
    uint32_t secs;
    unsigned n = (unsigned)s->n;
    buf[0] = 0;
    if (!working(s)) return;
    if (time_in_state(s, &secs)) {
        clock_text(t, sizeof t, secs);
        if (n) snprintf(buf, cap, "Step %u%s%s", n, theme_sep(), t);
        else snprintf(buf, cap, "%s", t);
    } else if (n) {
        snprintf(buf, cap, "Step %u", n);
    }
}

/* ───────── a step as a glyph ───────── */

static bool same_word(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a,
             y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) return false;
    }
    return !*a && !*b;
}

static bool one_of(const char *tool, const char *const *names)
{
    for (; *names; names++)
        if (same_word(tool, *names)) return true;
    return false;
}

/* A shell command that is a test run or git, by its first words (the daemon's turn.rs and graph.rs
 * know more; this only picks the glyph). */
static const char *shell_glyph(const char *cmd)
{
    static const char *const runners[] = {"pytest", "ctest", "jest",    "vitest",
                                          "tox",    "rspec", "phpunit", NULL};
    char first[16];
    size_t n = strcspn(cmd, " ");
    copy_cut(first, sizeof first, cmd, n);
    if (same_word(first, "git") || same_word(first, "gh")) return ICON_G_GIT;
    if (one_of(first, runners) || strstr(cmd, " test")) return ICON_G_TEST;
    return ICON_G_SHELL;
}

/* The tool of a step as a glyph: the kinds of the agent graph (graph.rs kind_of), from the tool's
 * name. NULL when the watch has no glyph that says it (the step then keeps its text). A sub-agent's
 * step leads with the agent ("explore: bash"): the tool is what follows. */
static const char *tool_glyph(const char *tool, const char *arg)
{
    static const char *const read[] = {"view", "read", "read_file", "notebookread", NULL};
    static const char *const edit[] = {"edit",         "create",      "write", "multiedit",
                                       "notebookedit", "apply_patch", NULL};
    static const char *const shell[] = {"bash",       "shell",     "powershell",
                                        "write_bash", "read_bash", "stop_bash",
                                        "bashoutput", "killshell", NULL};
    static const char *const search[] = {"grep", "glob", "ls", "lsp", "code_search", NULL};
    static const char *const web[] = {"web_fetch", "webfetch", "websearch", "web_search", NULL};
    static const char *const agent[] = {"task",        "agent",       "read_agent",
                                        "write_agent", "list_agents", NULL};
    static const char *const ask[] = {"ask_user", "askuserquestion", NULL};
    static const char *const plan[] = {
        "todowrite", "sql", "update_todo", "report_intent", "exitplanmode", "enterplanmode", NULL};
    const char *sub = strstr(tool, ": ");
    while (sub && strstr(sub + 2, ": ")) sub = strstr(sub + 2, ": ");
    if (sub) tool = sub + 2;
    if (one_of(tool, read)) return ICON_G_READ;
    if (one_of(tool, edit)) return ICON_G_EDIT;
    if (one_of(tool, shell)) return shell_glyph(arg);
    if (one_of(tool, search)) return ICON_G_SEARCH;
    if (one_of(tool, web)) return ICON_G_WEB;
    if (one_of(tool, agent)) return ICON_G_ROBOT;
    if (one_of(tool, ask)) return ICON_QUESTION;
    if (one_of(tool, plan)) return ICON_G_PLAN;
    return NULL;
}

/* A step's text as a node: its glyph, and what is left to say in text (`shown`, cap >= 2). A named
 * tool leaves its argument; an MCP tool (mcp__server__tool, server-tool) is the plug and keeps its
 * name; anything else is prose or a tool the watch does not know: `fallback` and the whole text. */
static const char *step_node(const char *text, const char *fallback, char *shown, size_t cap)
{
    char tool[48], arg[STEP_TEXT_MAX + 48];
    split_step(text, tool, sizeof tool, arg, sizeof arg);
    const char *g = tool[0] ? tool_glyph(tool, arg) : NULL;
    if (g) {
        snprintf(shown, cap, "%s", arg);
        return g;
    }
    theme_display_text(shown, cap, text);
    bool word = tool[0] && !strchr(tool, ' ');
    return word && (!strncmp(tool, "mcp__", 5) || strchr(tool, '-')) ? ICON_G_MCP : fallback;
}

/* ───────── the lane ───────── */

static void circle(lv_layer_t *l, int x, int y, int r, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {x - r, y - r, x + r, y + r};
    lv_draw_rect(l, &d, &a);
}

/* A ring of outer radius r, or (a0 < a1) the arc of it from a0 to a1 degrees clockwise from 12
 * o'clock. */
static void ring(lv_layer_t *l, int x, int y, int r, int w, int a0, int a1, lv_color_t c)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = x;
    d.center.y = y;
    d.radius = (uint16_t)r;
    d.width = (uint16_t)w;
    d.color = c;
    if (a0 >= a1) { /* closed: two overlapping halves, one 0..360 arc leaves a seam */
        d.start_angle = 0;
        d.end_angle = 181;
        lv_draw_arc(l, &d);
        d.start_angle = 180;
        d.end_angle = 1;
    } else {
        d.rounded = 1;
        d.start_angle = (lv_value_precise_t)((a0 % 360 + 360 + 270) % 360);
        d.end_angle = (lv_value_precise_t)((a1 % 360 + 360 + 270) % 360);
    }
    lv_draw_arc(l, &d);
}

static void hline(lv_layer_t *l, int x1, int x2, int y, lv_color_t c, lv_opa_t opa)
{
    if (x2 <= x1) return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1;
    d.p1.y = y;
    d.p2.x = x2;
    d.p2.y = y;
    d.color = c;
    d.width = LANE_W;
    d.opa = opa;
    d.round_start = d.round_end = 1;
    lv_draw_line(l, &d);
}

/* The hero's glyph is 96 px where the font has it (the node kinds, the working glyph, the
 * question), 64 px otherwise. */
static const lv_font_t *icon_font(const char *glyph, int px)
{ return px > 28 ? ui_glyph_font(glyph, px) : px == 28 ? FONT_ICON_M : FONT_ICON_S; }

static int badge_w(uint32_t n)
{
    char t[12];
    lv_point_t sz;
    snprintf(t, sizeof t, "%u", (unsigned)n);
    lv_text_get_size(&sz, t, FONT_BODY_STRONG, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int w = sz.x + 2 * BADGE_PAD;
    return w < BADGE_H ? BADGE_H : w;
}

/* A node as GRAPH draws one: a disc in the lane's colour with the glyph in black once it has
 * returned, red when it failed, a ring with the glyph in colour while it runs (the hero's ring is
 * drawn by the caller). */
static void node(lv_layer_t *l, const node_t *nd, int x, int y, int back, uint32_t rgb)
{
    int r = k_back[back].r;
    /* a failed step keeps its red: dimmed it would read as amber */
    lv_color_t c =
        nd->state == STEP_FAIL ? lv_color_hex(RGB_DANGER) : theme_dim(rgb, k_back[back].bright);
    const lv_font_t *f = icon_font(nd->glyph, k_back[back].px);
    if (nd->state == STEP_RUNNING) {
        circle(l, x, y, r, COL_BG); /* the lane does not show through the ring */
        if (back) ring(l, x, y, r, 3, 0, 0, c);
        ui_glyph_draw(l, f, nd->glyph, x, y, c, LV_OPA_COVER);
        return;
    }
    circle(l, x, y, r, c);
    ui_glyph_draw(l, f, nd->glyph, x, y, COL_ON_ACCENT, LV_OPA_COVER);
}

static void lane_draw(lv_event_t *e)
{
    const lane_t *v = &s_v;
    if (v->count <= 0) return;
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(s_lane, &a);
    int ox = a.x1, y = (a.y1 + a.y2) / 2, hero = ox + UI_CX, prior = v->count - 1;
    lv_color_t col = lv_color_hex(v->rgb), line = theme_dim(v->rgb, 70);

    /* the past: from the oldest node shown to the hero, fading out beyond it where steps were
     * dropped */
    int x_old = ox + k_back[prior].x;
    if (v->older) {
        int x = x_old - k_back[prior].r - 2;
        for (int f = 0; f < FADE_STEPS && x - FADE_DX >= ox + 26; f++, x -= FADE_DX)
            hline(l, x - FADE_DX, x, y, line,
                  (lv_opa_t)(LV_OPA_COVER * (FADE_STEPS - f) / (FADE_STEPS + 1)));
    }
    hline(l, x_old, hero, y, line, LV_OPA_COVER);
    /* what is to come: on to the step count, then three dashes that fade */
    int x = hero + HERO_R;
    if (v->step) {
        char t[12];
        int bw = badge_w(v->step), bx = ox + BADGE_X;
        snprintf(t, sizeof t, "%u", (unsigned)v->step);
        hline(l, x, bx, y, line, LV_OPA_COVER);
        lv_draw_rect_dsc_t bd;
        lv_draw_rect_dsc_init(&bd);
        bd.radius = LV_RADIUS_CIRCLE;
        bd.bg_color = COL_BG;
        bd.bg_opa = LV_OPA_COVER;
        bd.border_color = line;
        bd.border_width = 2;
        lv_area_t ba = {bx, y - BADGE_H / 2, bx + bw - 1, y - BADGE_H / 2 + BADGE_H - 1};
        lv_draw_rect(l, &bd, &ba);
        lv_draw_label_dsc_t td;
        lv_draw_label_dsc_init(&td);
        td.font = FONT_BODY_STRONG;
        td.color = col;
        td.align = LV_TEXT_ALIGN_CENTER;
        td.text = t;
        td.text_local = 1; /* `t` is on this stack: the draw task takes a copy */
        int lh = lv_font_get_line_height(FONT_BODY_STRONG);
        lv_area_t ta = {bx, y - lh / 2, bx + bw - 1, y - lh / 2 + lh - 1};
        lv_draw_label(l, &td, &ta);
        x = bx + bw;
    }
    for (int k = 0; k < 3; k++) {
        int x1 = x + DASH_GAP + k * (DASH_W + DASH_GAP);
        hline(l, x1, x1 + DASH_W, y, line, (lv_opa_t)(LV_OPA_COVER * (3 - k) / 4));
    }

    for (int i = 0; i < prior; i++) {
        int back = prior - i, nx = ox + k_back[back].x;
        node(l, &v->node[i], nx, y, back, v->rgb);
        if (v->sel == i) ring(l, nx, y, k_back[back].r + SEL_GAP + 2, 2, 0, 0, COL_TEXT);
    }
    /* the hero: a running step is a dim ring with a comet going round it */
    const node_t *h = &v->node[prior];
    node(l, h, hero, y, 0, v->rgb);
    if (h->state == STEP_RUNNING) {
        if (s_comet_on) {
            ring(l, hero, y, HERO_R, HERO_RING, 0, 0, theme_dim(v->rgb, 35));
            ring(l, hero, y, HERO_R, HERO_RING, s_comet_deg, s_comet_deg + COMET_DEG, col);
        } else {
            ring(l, hero, y, HERO_R, HERO_RING, 0, 0, col);
        }
    }
}

/* Invalidates a box of the lane given in the lane's own coordinates about its line (x, and y from
 * the line). */
static void lane_inv(int x1, int y1, int x2, int y2)
{
    lv_area_t a;
    lv_obj_get_coords(s_lane, &a);
    int y = (a.y1 + a.y2) / 2;
    lv_area_t box = {a.x1 + x1, y + y1, a.x1 + x2, y + y2};
    lv_obj_invalidate_area(s_lane, &box);
}

/* Takes what the lane is to show and redraws what differs: the earlier nodes, the hero, the
 * badge. */
static void lane_set(const lane_t *v)
{
    const lane_t *o = &s_v;
    bool all = (v->count > 0) != (o->count > 0) || v->rgb != o->rgb;
    bool past = all || v->count != o->count || v->older != o->older || v->sel != o->sel;
    bool hero = all, badge = all || v->step != o->step;
    for (int i = 0; i + 1 < v->count && !past; i++)
        past = v->node[i].glyph != o->node[i].glyph || v->node[i].state != o->node[i].state;
    if (!hero && v->count > 0)
        hero = v->node[v->count - 1].glyph != o->node[o->count - 1].glyph ||
               v->node[v->count - 1].state != o->node[o->count - 1].state;
    s_v = *v;
    int r1 = k_back[1].r + SEL_GAP + 3;
    if (past) lane_inv(24, -r1, UI_CX - HERO_R - 1, r1);
    if (hero) lane_inv(UI_CX - HERO_R - 2, -HERO_R - 2, UI_CX + HERO_R + 2, HERO_R + 2);
    if (badge) lane_inv(UI_CX + HERO_R + 1, -BADGE_H / 2 - 1, UI_SIZE - 24, BADGE_H / 2 + 1);
}

/* The comet moved: only its two ends changed (the head went on, the tail let go). */
static void comet_step(void)
{
    int old = s_comet_deg;
    s_comet_deg = (s_comet_deg + COMET_STEP_DEG) % 360;
    static const int ends[2] = {0, COMET_DEG};
    int rm = HERO_R - HERO_RING / 2, pad = HERO_RING / 2 + 3;
    for (int k = 0; k < 2; k++) {
        int a0 = old + ends[k], a1 = s_comet_deg + ends[k];
        int x0 = (int)(((int32_t)rm * lv_trigo_sin((int16_t)(a0 % 360))) >> 15),
            y0 = -(int)(((int32_t)rm * lv_trigo_cos((int16_t)(a0 % 360))) >> 15);
        int x1 = (int)(((int32_t)rm * lv_trigo_sin((int16_t)(a1 % 360))) >> 15),
            y1 = -(int)(((int32_t)rm * lv_trigo_cos((int16_t)(a1 % 360))) >> 15);
        lane_inv(UI_CX + LV_MIN(x0, x1) - pad, LV_MIN(y0, y1) - pad, UI_CX + LV_MAX(x0, x1) + pad,
                 LV_MAX(y0, y1) + pad);
    }
}

/* The earlier node under a screen point (its index), the hero (count - 1), or -1. */
static int lane_hit(int x, int y)
{
    const lane_t *v = &s_v;
    for (int i = 0; i < v->count; i++) {
        int back = v->count - 1 - i, dx = x - k_back[back].x, dy = y - LANE_Y,
            r = k_back[back].r + UI_DISC_SLACK;
        if (dx * dx + dy * dy <= r * r) return i;
    }
    return -1;
}

/* ───────── text ───────── */

/* Text for two rows of different widths: `first` gets the words of `text` that fit `w` px (one word
 * at least; its row cuts a longer one with dots), and the rest is returned. */
static const char *split_rows(const char *text, int w, char *first, size_t cap)
{
    size_t n = strlen(text), best = 0;
    if (n >= cap) n = cap - 1;
    memcpy(first, text, n);
    first[n] = 0;
    for (size_t i = 1; i <= n; i++) {
        if (i < n && first[i] != ' ') continue;
        if (first[i - 1] == ' ') continue;
        char keep = first[i];
        lv_point_t sz;
        first[i] = 0;
        lv_text_get_size(&sz, first, FONT_BODY, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        first[i] = keep;
        if (sz.x > w && best) break;
        best = i;
        if (sz.x > w) break;
    }
    first[best] = 0;
    const char *rest = text + best;
    while (*rest == ' ') rest++;
    return rest;
}

static void set_text(const char *text)
{
    if (strcmp(s_text, text)) {
        char first[sizeof s_text];
        snprintf(s_text, sizeof s_text, "%s", text);
        const char *rest = split_rows(s_text, LINE1_W, first, sizeof first);
        theme_set_text(s_line1, s_line1_txt, sizeof s_line1_txt, first);
        theme_set_text(s_line2, s_line2_txt, sizeof s_line2_txt, rest);
    }
    show(s_line1, s_text[0] != 0);
    show(s_line2, s_line2_txt[0] != 0);
}

/* ───────── render ───────── */

/* The time in this state: a tally under the lane while it works, dots round the state glyph
 * otherwise. */
static void render_time(const session_t *s)
{
    uint32_t secs = 0, rgb = theme_session_rgb(s);
    bool timed = time_in_state(s, &secs);
    show(s_tally, s_working && timed && secs >= 60);
    show(s_dots, !s_working && timed);
    if (!timed) return;
    if (s_working) ui_tally_set(s_tally, secs / 60, theme_dim(rgb, 85));
    else ui_dots_set(s_dots, secs / 60, theme_dim(rgb, 85));
}

static void render(void)
{
    const session_t *s = model_find(s_sid);
    if (!s) return;
    char host[24], marks[24];
    theme_host_prefix(host, sizeof host, s);
    const char *base = theme_basename(s->repo);
    snprintf(s_header_txt, sizeof s_header_txt, "%s%s%s%s", host, theme_agent_name(s->agent),
             theme_sep(), base[0] ? base : "(no folder)");
    ui_sigil_set_session(s_sigil, base, s->sid);
    snprintf(marks, sizeof marks, "%s%s", s->host ? ICON_G_WEB " " : "",
             theme_agent_glyph(s->agent));
    theme_set_text(s_marks, s_marks_txt, sizeof s_marks_txt, marks);

    const steps_t *t = &s->steps;
    uint32_t rgb = theme_session_rgb(s);
    bool work = working(s);
    s_working = work;
    if (t->last_n != s_last_n) { /* a new step (or a new turn): the current one is selected again */
        s_last_n = t->last_n;
        s_sel_n = 0;
    }
    lane_t v;
    memset(&v, 0, sizeof v);
    char text[sizeof s_text] = "";
    s_tool_txt[0] = s_arg_txt[0] = 0;
    s_rows = 0;
    if (work) {
        /* the hero is the current step: what the session says it is doing, else the newest step of
         * the timeline */
        const char *cur = s->summary[0] ? s->summary
                          : s->tool[0]  ? s->tool
                          : t->count    ? t->s[t->count - 1].text
                                        : "";
        int prior = t->count > 0 ? t->count - 1 : 0;
        if (cur[0]) split_step(cur, s_tool_txt, sizeof s_tool_txt, s_arg_txt, sizeof s_arg_txt);
        else snprintf(s_tool_txt, sizeof s_tool_txt, "%s", theme_state_word(s));
        v.count = prior + 1;
        v.older = t->count > 0 && t->s[0].n > 1;
        v.step = s->n;
        v.rgb = rgb;
        v.sel = prior;
        for (int i = 0; i < prior; i++) {
            const step_t *st = &t->s[i];
            char shown[sizeof s_text];
            v.node[i].glyph = step_node(st->text, ICON_G_DOT, shown, sizeof shown);
            v.node[i].state = st->state;
            theme_display_text(s_row_txt[i], sizeof s_row_txt[i], st->text);
            s_mark_txt[i] = st->state == STEP_OK ? "ok" : st->state == STEP_FAIL ? "fail" : "run";
            if (s_sel_n && st->n == s_sel_n) {
                v.sel = i;
                snprintf(text, sizeof text, "%s", shown);
            }
        }
        s_rows = prior;
        if (v.sel == prior) s_sel_n = 0; /* the selected step left the timeline */
        char shown[sizeof s_text];
        v.node[prior].glyph = step_node(cur, ICON_WORKING, shown, sizeof shown);
        v.node[prior].state = t->count ? t->s[t->count - 1].state : STEP_RUNNING;
        if (v.sel == prior) snprintf(text, sizeof text, "%s", shown);
    }
    lane_set(&v);
    show(s_lane, work);
    set_text(text);

    /* not working: the state as one glyph (a tap on it answers, when it waits for you), and what
     * the turn did */
    s_needs = !work && s->state == SESS_INPUT;
    show(s_hero, !work);
    s_word_txt[0] = 0;
    if (!work) {
        ui_glyph_set(s_hero, theme_state_glyph(s), lv_color_hex(rgb));
        snprintf(s_word_txt, sizeof s_word_txt, "%s", theme_state_word(s));
    }
    s_done_row = ui_done_watch_update(s, !work);
    render_time(s);
    show(s_badge[1], ui_home_slot_count() > 1);
}

uint32_t ui_watch_sid(void) { return s_sid; }

/* The deck's selection moved (rim, B): the watch follows it. */
static void follow_deck(void)
{
    uint32_t sel = ui_home_sel_sid();
    if (sel && sel != s_sid) {
        s_sid = sel;
        s_sel_n = s_last_n = 0;
        dbg_event("ui", "watch", "\"sid\":%u,\"via\":\"deck\"", (unsigned)sel);
        render();
    }
}

void ui_watch_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_now = s_comet_ms = now;
    s_comet_on = ui_animations_allowed();
    s_sel_n = s_last_n = 0;
    s_header_txt[0] = s_tool_txt[0] = s_arg_txt[0] = s_word_txt[0] = 0;
    s_rows = 0;
    ui_home_refresh(now); /* the index ring shows the sessions again, the watched one selected */
    ui_home_rim_mode(false);
    render();
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    dbg_event("ui", "watch", "\"sid\":%u,\"via\":\"open\"", (unsigned)sid);
}

void ui_watch_close(void)
{
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_sid = 0;
}

/* Returns false when the watched session is gone (or ended): the caller leaves the screen. */
bool ui_watch_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = model_find(s_sid);
    if (!s || s->state == SESS_ENDED) return false;
    /* the deck re-selected on its own */
    if (ui_home_sel_sid() != s_sid) ui_home_select_sid(s_sid, "watch");
    render();
    return true;
}

/* A tap on a node of the lane selects that step (its argument is the text below); a tap on the one
 * that is selected falls through to what a tap does anyway. */
static bool tap_node(const gesture_evt_t *g)
{
    if (!g || g->kind != G_TAP || !s_working) return false;
    const session_t *s = model_find(s_sid);
    int i = lane_hit(g->x, g->y);
    if (!s || i < 0 || i == s_v.sel) return false;
    int prior = s_v.count - 1;
    s_sel_n = i < prior && i < s->steps.count ? s->steps.s[i].n : 0;
    if (g_ui_actions->tick) g_ui_actions->tick();
    dbg_event("ui", "watch_step", "\"n\":%u", (unsigned)s_sel_n);
    render();
    return true;
}

bool ui_watch_act(nav_action_t a, const gesture_evt_t *g)
{
    switch (a) {
    case NAV_FOCUS: /* the deck's default action: Done also acknowledges */
    case NAV_PICK_RIM:
        /* the furthest node lies beyond the centre zone: its tap comes as a rim tap */
        if (tap_node(g)) return true;
        /* fall through */
    case NAV_SCRUB:
    case NAV_NEXT:
    case NAV_PREV: {
        bool used = ui_home_act(a, g);
        follow_deck();
        return used;
    }
    default: return false;
    }
}

void ui_watch_tick(uint32_t now)
{
    s_now = now;
    const session_t *s = model_find(s_sid);
    if (!s) return;
    render_time(s);
    if (s_v.count <= 0 || s_v.node[s_v.count - 1].state != STEP_RUNNING) return;
    bool anim = ui_animations_allowed();
    if (anim != s_comet_on) {
        /* animations were switched: the ring is whole when still, a comet when not */
        s_comet_on = anim;
        lane_inv(UI_CX - HERO_R - 2, -HERO_R - 2, UI_CX + HERO_R + 2, HERO_R + 2);
    }
    /* the comet: only while the current step runs, and only its two ends are redrawn */
    if (anim && now - s_comet_ms >= COMET_STEP_MS) {
        s_comet_ms = now - (now - s_comet_ms) % COMET_STEP_MS;
        comet_step();
    }
}

void ui_watch_report(ui_report_t *r)
{
    const session_t *s = model_find(s_sid);
    r->has_sel_sid = s != NULL;
    r->sel_sid = s_sid;
    snprintf(r->screen, sizeof r->screen, "watch");
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    for (int i = 0; i < s_rows; i++)
        ui_report_add(r, "%s[%s] %s", s_v.sel == i ? "> " : "", s_mark_txt[i], s_row_txt[i]);
    if (s_working && s_tool_txt[0]) ui_report_add(r, "%s", s_tool_txt);
    if (s_working && s_arg_txt[0]) ui_report_add(r, "%s", s_arg_txt);
    if (!s_working && s_word_txt[0]) ui_report_add(r, "%s", s_word_txt);
    if (s && !s_working) {
        char sub[48];
        state_caption(s, sub, sizeof sub);
        if (sub[0]) ui_report_add(r, "%s", sub);
    }
    if (s_needs) ui_report_add(r, "Answer");
    if (s_done_row) ui_done_watch_report(r);
    else if (s) {
        char foot[96];
        footer_words(s, foot, sizeof foot);
        if (foot[0]) ui_report_add(r, "%s", foot);
    }
}

/* One row of text at a fixed place; long text ends in dots, never on the ring. */
static lv_obj_t *text_row(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, int cy, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    int lh = lv_font_get_line_height(f);
    lv_obj_set_size(l, w, lh);
    lv_obj_set_pos(l, UI_CX - w / 2, cy - lh / 2);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ui_watch_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* who */
    s_sigil = ui_sigil_create(s_root, UI_CX, SIGIL_Y, SIGIL_R);
    s_marks = text_row(s_root, FONT_CAPTION, COL_TEXT3, MARK_Y, 200);

    /* what it is doing: the lane, the time in this state, the selected step's argument */
    s_lane = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_lane);
    lv_obj_set_size(s_lane, UI_SIZE, 2 * LANE_HALF + 1);
    lv_obj_set_pos(s_lane, 0, LANE_Y - LANE_HALF);
    lv_obj_remove_flag(s_lane, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lane, lane_draw, LV_EVENT_DRAW_MAIN, NULL);
    s_tally = ui_tally_create(s_root, UI_CX, TALLY_Y);
    s_line1 = text_row(s_root, FONT_BODY, COL_TEXT, LINE1_Y, LINE1_W);
    s_line2 = text_row(s_root, FONT_BODY, COL_TEXT, LINE2_Y, LINE2_W);

    /* or how it is: the state glyph, the time round it, what the turn did */
    s_dots = ui_dots_create(s_root, UI_CX, STATE_Y, DOTS_R);
    s_hero = ui_glyph_create(s_root, FONT_ICON_HERO, UI_CX, STATE_Y);
    ui_done_watch_create(s_root, DONE_ROW_Y);

    /* rule 6: A brings the session's terminal to the front (the desktop, as on the deck), B is the
     * next session */
    static const char *const words[2] = {"Open", "Next"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, 0, 0, &s_key[i],
                                     &s_word[i]);
    ui_key_hint_set(s_badge[0], ICON_COMPUTER);
    lv_obj_t *hide[] = {s_lane, s_tally, s_line1, s_line2, s_dots, s_hero};
    for (size_t i = 0; i < sizeof hide / sizeof hide[0]; i++)
        lv_obj_add_flag(hide[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
