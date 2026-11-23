/* Usage and context gauges in Sigil (docs/design/GLYPHS.md §5 GAUGES, docs/design/GAUGES.md):
 * quantities are arcs just inside the bezel, and no word says what they are.
 *
 * - The session card: the context window as an arc along the bottom of the bezel, between the two
 *   key hints. It fills from the left; grey while there is room, amber from 70 %, vermillion from
 *   90 %.
 * - The empty and ambient faces, and STATUS: the plan's two windows as a pair of arcs that start at
 *   12 o'clock, each under its glyph: the 5 hours (a clock) run down the left side, the week (a
 *   calendar) down the right. An arc is two scales at once. The fill is the usage. The bead on it
 *   is the time: how far through the window the clock is, so the arc beyond the bead is the time
 *   left until the reset, and a fill that has passed the bead is ahead of the clock. When the
 *   daemon says the window runs out before it resets, a ring marks that moment on the same scale.
 *   STATUS adds the two figures under its clock (rule 4: a figure a gauge shows).
 * - AGENT: the model's name (content from the agent), the permission mode as a glyph and the pull
 *   request as a glyph with its number, in the colour of its state.
 *
 * A figure that is unknown, stale or past its reset is never drawn as 0 %: its arc is a bare track.
 * The words ("5h 42% resets in 2h 10m", "Opus 4.7 · Plan · PR 128 approved") are the report's,
 * unchanged (rule 9). Redraw discipline (UI-V2 §8a): change-only setters; an arc is redrawn only
 * when a whole percent, a whole degree of its bead or its colour changes, and then only its own
 * half of the bezel. */
#include <stdio.h>
#include <string.h>

#include "gauges.h"
#include "ui_gauges.h"
#include "ui_kit.h"
#include "ui_theme.h"

/* ── shared ── */

#define DIM_PCT 45 /* the ambient face: everything at 45 % */

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *plain(lv_obj_t *parent, lv_event_cb_t draw, void *ctx)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, draw, LV_EVENT_DRAW_MAIN, ctx);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

/* A round-capped arc `w` px wide on the circle of centre-line radius r, clockwise from a0 to a1
 * (degrees from 12 o'clock; either may be negative). */
static void arc(lv_layer_t *l, int cx, int cy, int r, int w, int a0, int a1, lv_color_t c)
{
    if (a1 <= a0) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = (uint16_t)(r + w / 2);
    d.width = (uint16_t)w;
    d.color = c;
    d.rounded = 1;
    d.start_angle = (lv_value_precise_t)((a0 % 360 + 360 + 270) % 360);
    d.end_angle = (lv_value_precise_t)((a1 % 360 + 360 + 270) % 360);
    lv_draw_arc(l, &d);
}

static void disc(lv_layer_t *l, int x, int y, int r, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {x - r, y - r, x + r, y + r};
    lv_draw_rect(l, &d, &a);
}

/* The point at `deg` on the circle of radius r, in 1/1024: lv_trigo_sin() is a table, no libm. */
static void polar(int cx, int cy, int r, int deg, int *x, int *y)
{
    *x = cx + (int)(((int32_t)r * lv_trigo_sin((int16_t)deg) + (1 << 14)) >> 15);
    *y = cy - (int)(((int32_t)r * lv_trigo_cos((int16_t)deg) + (1 << 14)) >> 15);
}

/* Plan usage: chrome (white) while there is room, so the arcs never read as session segments on the
 * ring; amber from the warning level, vermillion near the end. */
#define RGB_USAGE_OK 0xffffff
static uint32_t usage_rgb(int pct)
{ return pct >= GAUGES_USAGE_HOT ? RGB_ERROR : pct >= GAUGES_WARN_PCT ? RGB_AMBER : RGB_USAGE_OK; }

/* ── session card: the context arc ── */

/* About the screen centre, whatever parent the card gives: the bottom of the bezel, between the key
 * hints (their rings begin 39 degrees either side of 6 o'clock). Its centre line is r 196: inside
 * the content circle and the ring's pips (r 200), and below the graph strip (its discs end at y
 * 401) and the book disc (y 404): the arc's upper edge is at y 400 at its ends and y 404 under the
 * strip's outermost disc. */
#define CTX_R 196
#define CTX_W 6
#define CTX_HALF 30 /* degrees either side of 6 o'clock */
#define CTX_X1 (UI_CX - 106)
#define CTX_X2 (UI_CX + 106)
#define CTX_Y1 (UI_CX + 163)
#define CTX_Y2 (UI_CX + CTX_R + CTX_W)

static lv_obj_t *s_ctx;
static int s_ctx_pct = -1, s_ctx_drawn = -1;
static lv_color_t s_ctx_col;

static void ctx_draw(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(s_ctx, &a);
    int cx = a.x1 + UI_CX - CTX_X1, cy = a.y1 + UI_CX - CTX_Y1, left = 180 + CTX_HALF;
    arc(l, cx, cy, CTX_R, CTX_W, 180 - CTX_HALF, left, COL_FILL2);
    if (s_ctx_drawn < 0) return;
    int span = 2 * CTX_HALF * s_ctx_drawn / 100;
    if (span < 1) span = 1; /* a round dot at 0-1 %: the gauge is there, the figure is known */
    arc(l, cx, cy, CTX_R, CTX_W, left - span, left, s_ctx_col);
}

/* The card moves the parent it gave to place the old capsule (under the text, or lower when the
 * graph or the book disc holds the slot). The arc belongs to the bezel, so it undoes that move. */
static void ctx_place(void)
{
    lv_obj_t *p = lv_obj_get_parent(s_ctx);
    int x = CTX_X1 - lv_obj_get_style_x(p, 0), y = CTX_Y1 - lv_obj_get_style_y(p, 0);
    if (lv_obj_get_style_x(s_ctx, 0) != x || lv_obj_get_style_y(s_ctx, 0) != y)
        lv_obj_set_pos(s_ctx, x, y);
}

void ui_gauges_card_create(lv_obj_t *centre)
{
    s_ctx = plain(centre, ctx_draw, NULL);
    lv_obj_set_size(s_ctx, CTX_X2 - CTX_X1 + 1, CTX_Y2 - CTX_Y1 + 1);
    ctx_place();
}

void ui_gauges_card_update(const session_t *s, bool shown, bool offline)
{
    bool on = shown && s && s->ctx_known && s->agent != AGENT_SHELL;
    s_ctx_pct = on ? s->ctx : -1;
    if (on) {
        ctx_place();
        /* quiet grey while there is room; amber from 70 %, vermillion from 90 %: status by
         * exception */
        uint32_t rgb = s->ctx >= GAUGES_CTX_HOT    ? RGB_ERROR
                       : s->ctx >= GAUGES_CTX_WARN ? RGB_AMBER
                                                   : 0;
        lv_color_t c = offline || !rgb ? COL_TEXT2 : lv_color_hex(rgb);
        if (s_ctx_drawn != s->ctx || !lv_color_eq(c, s_ctx_col)) {
            s_ctx_drawn = s->ctx;
            s_ctx_col = c;
            lv_obj_invalidate(s_ctx);
        }
    }
    show(s_ctx, on);
}

void ui_gauges_card_report(ui_report_t *r)
{
    if (s_ctx_pct >= 0) ui_report_add(r, "context %d%%", s_ctx_pct);
}

/* ── the plan's two windows: a pair of arcs from 12 o'clock ── */

/* Centre line r 194, 6 px wide: inside the content circle and clear of the ring's pips (r 200-210).
 * Each arc runs from 11 to 118 degrees from 12 o'clock on its side, so its end (y 324) stays above
 * the key hints. */
#define USE_R 194
#define USE_W 6
#define USE_A0 11
#define USE_A1 118
#define USE_SPAN (USE_A1 - USE_A0)
/* the two glyphs sit side by side at 12 o'clock, each at the start of its arc */
#define USE_GLYPH_DX 13
#define USE_BEAD_R 5
#define USE_HALF (USE_R + USE_W + USE_BEAD_R + 2) /* the object's half side */

typedef struct {
    int16_t pct; /* -1: unknown now, the track alone */
    int16_t now; /* the bead: degrees along the arc; -1 none */
    int16_t out; /* the ring where it runs out; -1 none */
} win_view_t;

typedef struct {
    lv_obj_t *obj;
    win_view_t w[2]; /* [0] 5 hours (left), [1] the week (right) */
    bool dim;
} usage_arcs_t;

static usage_arcs_t s_face_arcs, s_status_arcs;

static lv_color_t tone(uint32_t rgb, bool dim)
{ return dim ? theme_dim(rgb, DIM_PCT) : lv_color_hex(rgb); }

static void usage_draw(lv_event_t *e)
{
    usage_arcs_t *u = lv_event_get_user_data(e);
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(u->obj, &a);
    int cx = a.x1 + USE_HALF, cy = a.y1 + USE_HALF;
    static const char *const glyph[2] = {ICON_CLOCK, ICON_WEEK};
    for (int i = 0; i < 2; i++) {
        const win_view_t *w = &u->w[i];
        int side = i ? 1 : -1;
        bool live = w->pct >= 0;
        uint32_t rgb = live ? usage_rgb(w->pct) : 0;
        ui_glyph_draw(l, FONT_ICON_S, glyph[i], cx + side * USE_GLYPH_DX, cy - USE_R,
                      live ? tone(0xa0a0a8, u->dim) : tone(0x6c6c73, u->dim), LV_OPA_COVER);
        /* the left arc runs anticlockwise: its angles are the right one's, negated */
        if (i) arc(l, cx, cy, USE_R, USE_W, USE_A0, USE_A1, tone(0x2c2c2e, u->dim));
        else arc(l, cx, cy, USE_R, USE_W, -USE_A1, -USE_A0, tone(0x2c2c2e, u->dim));
        if (!live) continue;
        int span = USE_SPAN * w->pct / 100;
        /* 0 % known: a round dot at the start, so known reads unlike unknown */
        if (span < 1) span = 1;
        if (i) arc(l, cx, cy, USE_R, USE_W, USE_A0, USE_A0 + span, tone(rgb, u->dim));
        else arc(l, cx, cy, USE_R, USE_W, -USE_A0 - span, -USE_A0, tone(rgb, u->dim));
        int x, y;
        if (w->out >= 0) { /* where it runs out: a ring in the warning colour */
            polar(cx, cy, USE_R, side * (USE_A0 + w->out), &x, &y);
            disc(l, x, y, USE_BEAD_R + 2, COL_BG);
            disc(l, x, y, USE_BEAD_R, tone(rgb == RGB_USAGE_OK ? RGB_AMBER : rgb, u->dim));
            disc(l, x, y, USE_BEAD_R - 2, COL_BG);
        }
        if (w->now >= 0) {
            /* the clock: a bead with a black rim, so it reads on the fill and on the track */
            polar(cx, cy, USE_R, side * (USE_A0 + w->now), &x, &y);
            disc(l, x, y, USE_BEAD_R + 2, COL_BG);
            disc(l, x, y, USE_BEAD_R, tone(0xffffff, u->dim));
        }
    }
}

static void usage_create(usage_arcs_t *u, lv_obj_t *parent)
{
    u->obj = plain(parent, usage_draw, u);
    lv_obj_set_size(u->obj, 2 * USE_HALF + 1, 2 * USE_HALF + 1);
    lv_obj_set_pos(u->obj, UI_CX - USE_HALF, UI_CX - USE_HALF);
    for (int i = 0; i < 2; i++) u->w[i] = (win_view_t){-2, -1, -1};
}

/* One window as it is drawn at `now`. */
static win_view_t usage_view(bool live, const usage_win_t *w, uint32_t now, uint32_t len)
{
    win_view_t v = {-1, -1, -1};
    if (!live) return v;
    v.pct = w->pct;
    v.now = (int16_t)(USE_SPAN * gauges_window_pm(w, now, len) / 1000);
    if (w->pct < 100 && w->out && w->out > now && w->out < w->resets)
        v.out = (int16_t)(USE_SPAN * gauges_window_pm(w, w->out, len) / 1000);
    return v;
}

/* Sets what the pair shows; redraws only the side that changed. */
static void usage_set(usage_arcs_t *u, const win_view_t v[2], bool dim)
{
    bool all = dim != u->dim;
    u->dim = dim;
    for (int i = 0; i < 2; i++) {
        if (!all && !memcmp(&v[i], &u->w[i], sizeof v[i])) continue;
        u->w[i] = v[i];
        /* its half of the bezel, down to the arc's end (118 degrees: 0.47 of the radius below the
         * centre) */
        lv_area_t a, box;
        lv_obj_get_coords(u->obj, &a);
        int cx = a.x1 + USE_HALF, cy = a.y1 + USE_HALF;
        box.x1 = i ? cx : a.x1;
        box.x2 = i ? a.x2 : cx;
        box.y1 = a.y1;
        box.y2 = cy + USE_HALF / 2;
        lv_obj_invalidate_area(u->obj, &box);
    }
}

/* ── empty and ambient faces: the pair, alone ── */

static char s_face_txt[24];

void ui_gauges_face_create(lv_obj_t *centre) { usage_create(&s_face_arcs, centre); }

void ui_gauges_face_update(bool shown, bool dim)
{
    usage_win_t h5 = {0}, wk = {0};
    uint32_t now = theme_now_unix();
    /* the 5-hour window decides, as its figure did: without it the face stays bare */
    bool on = shown && gauges_h5(now, &h5);
    show(s_face_arcs.obj, on);
    s_face_txt[0] = 0;
    if (!on) return;
    bool lk = gauges_wk(now, &wk);
    win_view_t v[2] = {usage_view(true, &h5, now, GAUGES_H5_S),
                       usage_view(lk, &wk, now, GAUGES_WK_S)};
    usage_set(&s_face_arcs, v, dim);
    snprintf(s_face_txt, sizeof s_face_txt, "5h %u%%", (unsigned)h5.pct);
}

void ui_gauges_face_report(ui_report_t *r)
{
    if (s_face_txt[0]) ui_report_add(r, "%s", s_face_txt);
}

/* ── STATUS: the pair, and the two figures under the clock ── */

/* The figures stand where the two rows of words stood (y 152 and 180): one row between them, the 5
 * hours left of the centre and the week right of it, each on the side of its arc. */
#define FIG_Y 166
#define FIG_GAP 14 /* half the gap between the two figures */

typedef struct {
    lv_obj_t *fig;
    bool shown;
    char fig_txt[16];           /* on glass: "42%", or a dash */
    char words[24], detail[40]; /* the report: "5h 42%", "resets in 2h 10m" */
    int pct;
} gauge_t;

static gauge_t s_g[2]; /* [0] 5 hours, [1] the week */

void ui_gauges_status_create(lv_obj_t *root)
{
    usage_create(&s_status_arcs, root);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = s_g[i].fig = label(root, FONT_HEAD, COL_TEXT);
        int lh = lv_font_get_line_height(FONT_HEAD);
        if (i) lv_obj_set_pos(l, UI_CX + FIG_GAP, FIG_Y - lh / 2);
        else lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -(UI_SIZE - UI_CX + FIG_GAP), FIG_Y - lh / 2);
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        s_g[i].pct = -1;
    }
}

static void gauge_render(gauge_t *g, const char *name, bool live, const usage_win_t *w,
                         uint32_t now)
{
    char fig[8];
    g->pct = live ? w->pct : -1;
    g->detail[0] = 0;
    if (live) {
        snprintf(fig, sizeof fig, "%u%%", (unsigned)w->pct);
        snprintf(g->words, sizeof g->words, "%s %u%%", name, (unsigned)w->pct);
        gauges_detail(g->detail, sizeof g->detail, w, now, model_settings()->tz_off_min);
    } else {
        snprintf(fig, sizeof fig, "\xe2\x80\x93"); /* an en dash: not known now, never 0 % */
        snprintf(g->words, sizeof g->words, "%s \xe2\x80\x93", name);
    }
    theme_set_text(g->fig, g->fig_txt, sizeof g->fig_txt, fig);
    uint32_t rgb = live ? usage_rgb(w->pct) : 0;
    /* on pace to run out before the reset: the figure warns in amber below the warning level too */
    bool pace = live && w->pct < 100 && w->out && w->out > now && w->out < w->resets;
    if (pace && rgb == RGB_USAGE_OK) rgb = RGB_AMBER;
    theme_set_text_color(g->fig, !live ? COL_TEXT3 : lv_color_hex(rgb));
}

bool ui_gauges_status_render(uint32_t now)
{
    const usage_t *u = gauges_usage();
    /* never heard of (no subscription data, or an older daemon): STATUS stays as it was */
    bool any = u->at != 0 && (u->h5.known || u->wk.known);
    show(s_status_arcs.obj, any);
    for (int i = 0; i < 2; i++) {
        show(s_g[i].fig, any);
        s_g[i].shown = any;
    }
    if (!any) return false;
    usage_win_t w5 = {0}, wk = {0};
    bool l5 = gauges_h5(now, &w5), lk = gauges_wk(now, &wk);
    gauge_render(&s_g[0], "5h", l5, &w5, now);
    gauge_render(&s_g[1], "Week", lk, &wk, now);
    win_view_t v[2] = {usage_view(l5, &w5, now, GAUGES_H5_S),
                       usage_view(lk, &wk, now, GAUGES_WK_S)};
    usage_set(&s_status_arcs, v, false);
    return true;
}

void ui_gauges_status_report(ui_report_t *r)
{
    for (int i = 0; i < 2; i++) {
        if (!s_g[i].shown) continue;
        if (s_g[i].detail[0]) ui_report_add(r, "%s %s", s_g[i].words, s_g[i].detail);
        else ui_report_add(r, "%s", s_g[i].words);
        if (s_g[i].pct >= 0) ui_report_add(r, "arc%d %d%%", i, s_g[i].pct);
    }
}

/* ── AGENT: the model, the mode and the pull request ── */

#define READOUT_Y 108 /* the row's centre in the parent AGENT gives */
#define READOUT_H 32
#define READOUT_W 208 /* the window AGENT gives the row (ui_agent.c READOUT_W) */
#define READOUT_GAP 10
#define PR_GAP 3

static lv_obj_t *s_readout, *s_model, *s_mode, *s_pr, *s_pr_n;
static char s_readout_txt[64], s_model_txt[20], s_mdl[20], s_pr_txt[16];
static const char *s_mode_glyph = "";

/* The permission mode as a glyph, only when it is not the default (status by exception): plan is
 * the map, in white; the modes that let more through are amber; bypass, which asks nothing, is the
 * slashed shield in red. */
static const char *mode_glyph(const session_t *s, uint32_t *rgb)
{
    if (!s->pm_known) return "";
    switch (s->pm) {
    case PM_PLAN: *rgb = 0xffffff; return ICON_G_MAP;
    case PM_ACCEPT_EDITS: *rgb = RGB_AMBER; return ICON_G_EDIT;
    case PM_AUTO: *rgb = RGB_AMBER; return ICON_SKIP;
    case PM_DONT_ASK: *rgb = RGB_AMBER; return ICON_DONT_ASK;
    case PM_BYPASS: *rgb = RGB_ERROR; return ICON_BYPASS;
    default: return "";
    }
}

/* A pull request's colour is its review state: waiting for review asks (yellow), approved is done
 * (green), changes requested is the error colour, a draft is idle grey. */
static uint32_t pr_rgb(uint8_t prs)
{
    switch (prs) {
    case PRS_PENDING: return RGB_ASK;
    case PRS_APPROVED: return RGB_DONE;
    case PRS_CHANGES: return RGB_ERROR;
    case PRS_DRAFT: return RGB_IDLE;
    default: return 0xa0a0a8;
    }
}

void ui_gauges_agent_create(lv_obj_t *root)
{
    s_readout = lv_obj_create(root);
    lv_obj_remove_style_all(s_readout);
    lv_obj_set_size(s_readout, LV_SIZE_CONTENT, READOUT_H);
    lv_obj_set_flex_flow(s_readout, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_readout, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_readout, READOUT_GAP, 0);
    lv_obj_remove_flag(s_readout, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_readout, LV_ALIGN_TOP_MID, 0, READOUT_Y - READOUT_H / 2);
    s_model = label(s_readout, FONT_CAPTION, COL_TEXT2);
    s_mode = label(s_readout, FONT_ICON_M, COL_TEXT);
    lv_obj_t *pr = lv_obj_create(s_readout); /* the pull request's glyph and number stay together */
    lv_obj_remove_style_all(pr);
    lv_obj_set_size(pr, LV_SIZE_CONTENT, READOUT_H);
    lv_obj_set_flex_flow(pr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pr, PR_GAP, 0);
    lv_obj_remove_flag(pr, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_pr = pr;
    lv_obj_t *g = label(pr, FONT_ICON_M, COL_TEXT);
    lv_label_set_text_static(g, ICON_PR);
    s_pr_n = label(pr, FONT_BODY_STRONG, COL_TEXT);
    lv_obj_add_flag(s_readout, LV_OBJ_FLAG_HIDDEN);
}

static int32_t text_w(const char *t, const lv_font_t *f)
{
    lv_point_t sz;
    lv_text_get_size(&sz, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

/* The row fits its window: the mode glyph and the pull request's number are never cut, the model's
 * name gives way and ends in an ellipsis (`out` gets what is drawn). */
static void fit_model(char *out, size_t cap, const char *mdl, const char *mode, const char *pr)
{
    int32_t room = READOUT_W;
    if (mode[0]) room -= text_w(mode, FONT_ICON_M) + READOUT_GAP;
    if (pr[0]) {
        room -= text_w(ICON_PR, FONT_ICON_M) + PR_GAP + text_w(pr, FONT_BODY_STRONG) + READOUT_GAP;
    }
    snprintf(out, cap, "%s", mdl);
    size_t n = strlen(out);
    while (n > 0 && text_w(out, FONT_CAPTION) > room) {
        do { /* a character at a time */
            n--;
        } while (n > 0 && ((uint8_t)mdl[n] & 0xc0) == 0x80);
        while (n > 0 && mdl[n - 1] == ' ') n--;
        snprintf(out, cap, "%.*s\xE2\x80\xA6", (int)n, mdl);
    }
    if (n == 0) out[0] = 0;
}

void ui_gauges_agent_update(const session_t *s, bool shown)
{
    char buf[64] = "";
    bool on = shown && s && s->agent != AGENT_SHELL;
    if (on) gauges_readout(buf, sizeof buf, s, theme_sep());
    on = on && buf[0];
    snprintf(s_readout_txt, sizeof s_readout_txt, "%s", buf);
    show(s_readout, on);
    if (!on) return;
    uint32_t rgb = 0xffffff;
    const char *mode = mode_glyph(s, &rgb);
    char n[12] = "", drawn[sizeof s_model_txt];
    if (s->pr) snprintf(n, sizeof n, "%u", (unsigned)s->pr);
    snprintf(s_mdl, sizeof s_mdl, "%s", s->mdl);
    fit_model(drawn, sizeof drawn, s->mdl, mode, n);
    theme_set_text(s_model, s_model_txt, sizeof s_model_txt, drawn);
    show(s_model, drawn[0] != 0);
    if (mode != s_mode_glyph) {
        s_mode_glyph = mode;
        lv_label_set_text_static(s_mode, mode);
    }
    theme_set_text_color(s_mode, lv_color_hex(rgb));
    show(s_mode, mode[0] != 0);
    theme_set_text(s_pr_n, s_pr_txt, sizeof s_pr_txt, n);
    lv_color_t c = lv_color_hex(pr_rgb(s->prs));
    theme_set_text_color(lv_obj_get_child(s_pr, 0), c);
    theme_set_text_color(s_pr_n, c);
    show(s_pr, s->pr != 0);
}

void ui_gauges_agent_report(ui_report_t *r)
{
    if (!s_readout_txt[0] || lv_obj_has_flag(s_readout, LV_OBJ_FLAG_HIDDEN)) return;
    /* a name cut to fit is reported as drawn */
    const char *at = s_mdl[0] ? strstr(s_readout_txt, s_mdl) : NULL;
    if (at && strcmp(s_model_txt, s_mdl) != 0) {
        ui_report_add(r, "%.*s%s%s", (int)(at - s_readout_txt), s_readout_txt, s_model_txt,
                      at + strlen(s_mdl));
    } else {
        ui_report_add(r, "%s", s_readout_txt);
    }
}
