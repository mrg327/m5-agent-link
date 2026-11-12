/* Usage and context gauges on glass (docs/design/GAUGES.md). Four small pieces, each owned by a screen that
 * calls in here so the screens' own files change by a few lines:
 *
 * - the session card: a context capsule under the state row (the job bar's slot; a job has no context);
 * - the empty and ambient faces: one figure, "5h 42%";
 * - STATUS: two thin concentric arcs inside the bezel (outer: 5 hours, inner: the week) and two rows of
 *   figures under the clock;
 * - AGENT: one readout line under the caption, "Opus 4.7 · Plan · PR 128 approved".
 *
 * A figure that is unknown, stale or past its reset is never drawn as 0 %: the face shows nothing and STATUS a
 * dash. Redraw discipline (UI-V2 §8a): change-only setters; an arc moves only when its whole percent does. */
#include <stdio.h>
#include <string.h>

#include "gauges.h"
#include "ui_gauges.h"
#include "ui_theme.h"

/* ── shared ── */

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
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *capsule(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* Plan usage: chrome (white) while there is room, so the arcs never read as session segments on the ring;
 * amber from the warning level, vermillion near the end. */
#define RGB_USAGE_OK 0xffffff
static uint32_t usage_rgb(int pct)
{
    return pct >= GAUGES_USAGE_HOT ? RGB_ERROR : pct >= GAUGES_WARN_PCT ? RGB_AMBER : RGB_USAGE_OK;
}

/* ── session card: the context capsule ── */

#define CTX_Y 219 /* the job bar's row (JOBS.md §5): between the state row and the summary */
#define CTX_W 160
#define CTX_H 4

static lv_obj_t *s_ctx, *s_ctx_fill;
static int s_ctx_w = -1, s_ctx_pct = -1;

void ui_gauges_card_create(lv_obj_t *centre)
{
    s_ctx = capsule(centre, CTX_W, CTX_H);
    lv_obj_set_pos(s_ctx, UI_CX - CTX_W / 2, CTX_Y);
    theme_set_bg_color(s_ctx, COL_FILL2);
    s_ctx_fill = capsule(s_ctx, CTX_H, CTX_H);
    lv_obj_add_flag(s_ctx, LV_OBJ_FLAG_HIDDEN);
}

void ui_gauges_card_update(const session_t *s, bool shown, bool offline)
{
    bool on = shown && s && s->ctx_known && s->agent != AGENT_SHELL;
    show(s_ctx, on);
    s_ctx_pct = on ? s->ctx : -1;
    if (!on) return;
    int w = CTX_W * s->ctx / 100;
    if (w < CTX_H) w = CTX_H; /* a round dot at 0-2 %: the capsule is there, the figure is known */
    if (w != s_ctx_w) {
        s_ctx_w = w;
        lv_obj_set_width(s_ctx_fill, w);
    }
    /* quiet grey while there is room; amber from 70 %, vermillion from 90 %: status by exception */
    uint32_t rgb = s->ctx >= GAUGES_CTX_HOT ? RGB_ERROR : s->ctx >= GAUGES_CTX_WARN ? RGB_AMBER : 0;
    theme_set_bg_color(s_ctx_fill, offline || !rgb ? COL_TEXT2 : lv_color_hex(rgb));
}

void ui_gauges_card_report(ui_report_t *r)
{
    if (s_ctx_pct >= 0) ui_report_add(r, "context %d%%", s_ctx_pct);
}

/* ── empty and ambient faces: one figure ── */

#define FACE_Y 304

static lv_obj_t *s_face;
static char s_face_txt[24];

void ui_gauges_face_create(lv_obj_t *centre)
{
    s_face = label(centre, FONT_BODY, COL_TEXT2);
    int w = theme_chord(FACE_Y + 12, 30);
    lv_obj_set_size(s_face, w, lv_font_get_line_height(FONT_BODY));
    lv_obj_set_style_text_align(s_face, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_face, UI_CX - w / 2, FACE_Y - lv_font_get_line_height(FONT_BODY) / 2);
    lv_obj_add_flag(s_face, LV_OBJ_FLAG_HIDDEN);
}

void ui_gauges_face_update(bool shown, bool dim)
{
    usage_win_t w;
    bool on = shown && gauges_h5(theme_now_unix(), &w);
    show(s_face, on);
    if (!on) {
        s_face_txt[0] = 0;
        return;
    }
    char buf[24];
    snprintf(buf, sizeof buf, "5h %u%%", (unsigned)w.pct);
    theme_set_text(s_face, s_face_txt, sizeof s_face_txt, buf);
    /* grey like the face's caption; amber once the warning level is reached (dimmed on the ambient face) */
    uint32_t rgb = w.pct >= GAUGES_WARN_PCT ? usage_rgb(w.pct) : 0;
    lv_color_t c = rgb ? (dim ? theme_dim(rgb, 45) : lv_color_hex(rgb)) : dim ? theme_dim(0xffffff, 45) : COL_TEXT2;
    theme_set_text_color(s_face, c);
}

void ui_gauges_face_report(ui_report_t *r)
{
    if (s_face_txt[0] && !lv_obj_has_flag(s_face, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_face_txt);
}

/* ── STATUS: two arcs and two rows ── */

/* Centre lines r 192 and 182, 4 px wide: inside the content circle and clear of the ring's pips (r 200-210).
 * They span 240 degrees through 12 o'clock, so their ends (y 329) stay above the legends. */
#define ARC_R_OUT 192
#define ARC_R_IN 182
#define ARC_W 4
#define ARC_SPAN 240 /* degrees, centred on 12 o'clock */
#define ROW1_Y 152
#define ROW2_Y 180
#define ROW_H 26

typedef struct {
    lv_obj_t *row, *fig, *detail;
    int pct; /* the arc's value: -1 = unknown (track only) */
    uint32_t rgb;
    char fig_txt[24], detail_txt[40];
} gauge_t;

static gauge_t s_g[2]; /* [0] 5 hours (outer), [1] the week (inner) */
static lv_obj_t *s_arcs; /* one object draws both arcs (LV_USE_ARC is off: lv_draw_arc, as the ring does) */

/* An arc of width ARC_W centred on radius r, from a0 to a1 degrees clockwise from 12 o'clock, round caps. */
static void draw_arc(lv_layer_t *l, int cx, int cy, int r, int a0, int a1, lv_color_t c)
{
    if (a1 <= a0) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = (uint16_t)(r + ARC_W / 2);
    d.width = ARC_W;
    d.color = c;
    d.rounded = 1;
    d.start_angle = (lv_value_precise_t)((a0 + 360 + 270) % 360);
    d.end_angle = (lv_value_precise_t)((a1 + 360 + 270) % 360);
    lv_draw_arc(l, &d);
}

static void on_draw_arcs(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    int cx = a.x1 + lv_area_get_width(&a) / 2, cy = a.y1 + lv_area_get_height(&a) / 2;
    static const int radius[2] = {ARC_R_OUT, ARC_R_IN};
    for (int i = 0; i < 2; i++) {
        draw_arc(l, cx, cy, radius[i], -ARC_SPAN / 2, ARC_SPAN / 2, COL_FILL2);
        if (s_g[i].pct > 0)
            draw_arc(l, cx, cy, radius[i], -ARC_SPAN / 2, -ARC_SPAN / 2 + ARC_SPAN * s_g[i].pct / 100,
                     lv_color_hex(s_g[i].rgb));
        else if (s_g[i].pct == 0) /* 0 % known: a round dot at the start, so known reads unlike unknown */
            draw_arc(l, cx, cy, radius[i], -ARC_SPAN / 2, -ARC_SPAN / 2 + 1, lv_color_hex(s_g[i].rgb));
    }
}

static void row_create(gauge_t *g, lv_obj_t *parent, int cy)
{
    g->row = lv_obj_create(parent);
    lv_obj_remove_style_all(g->row);
    lv_obj_set_size(g->row, LV_SIZE_CONTENT, ROW_H);
    lv_obj_set_flex_flow(g->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g->row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g->row, 8, 0);
    lv_obj_remove_flag(g->row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(g->row, LV_ALIGN_TOP_MID, 0, cy - ROW_H / 2);
    g->fig = label(g->row, FONT_BODY_STRONG, COL_TEXT);
    g->detail = label(g->row, FONT_BODY, COL_TEXT2);
    lv_label_set_long_mode(g->fig, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(g->fig, LV_SIZE_CONTENT);
    lv_label_set_long_mode(g->detail, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(g->detail, LV_SIZE_CONTENT);
    g->pct = -2;
    g->rgb = 0;
}

void ui_gauges_status_create(lv_obj_t *root)
{
    int d = 2 * (ARC_R_OUT + ARC_W);
    s_arcs = lv_obj_create(root);
    lv_obj_remove_style_all(s_arcs);
    lv_obj_set_size(s_arcs, d, d);
    lv_obj_set_pos(s_arcs, UI_CX - d / 2, UI_CX - d / 2);
    lv_obj_remove_flag(s_arcs, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_arcs, on_draw_arcs, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_arcs, LV_OBJ_FLAG_HIDDEN);
    row_create(&s_g[0], root, ROW1_Y);
    row_create(&s_g[1], root, ROW2_Y);
    for (int i = 0; i < 2; i++) lv_obj_add_flag(s_g[i].row, LV_OBJ_FLAG_HIDDEN);
}

static void gauge_render(gauge_t *g, const char *name, bool live, const usage_win_t *w, uint32_t now)
{
    char fig[24], detail[40] = "";
    int pct = live ? w->pct : -1;
    if (live) {
        snprintf(fig, sizeof fig, "%s %u%%", name, (unsigned)w->pct);
        gauges_detail(detail, sizeof detail, w, now, model_settings()->tz_off_min);
    } else {
        snprintf(fig, sizeof fig, "%s \xe2\x80\x93", name); /* an en dash: not known now, never 0 % */
    }
    uint32_t rgb = live ? usage_rgb(w->pct) : 0;
    if (pct != g->pct || rgb != g->rgb) {
        g->pct = pct;
        g->rgb = rgb;
        lv_obj_invalidate(s_arcs); /* a whole percent or a colour step: rare, so the full redraw is fine */
    }
    theme_set_text(g->fig, g->fig_txt, sizeof g->fig_txt, fig);
    theme_set_text_color(g->fig, !live ? COL_TEXT3 : w->pct >= GAUGES_WARN_PCT ? lv_color_hex(rgb) : COL_TEXT);
    /* "runs out 14:30" is the warning: in the figure's colour (amber below the warning level too); a reset
     * time is plain grey */
    bool warn = live && (w->pct >= 100 || (w->out && w->out > now && w->out < w->resets));
    theme_set_text(g->detail, g->detail_txt, sizeof g->detail_txt, detail);
    theme_set_text_color(g->detail, !warn ? COL_TEXT2 : lv_color_hex(rgb == RGB_USAGE_OK ? RGB_AMBER : rgb));
    show(g->detail, detail[0] != 0);
}

bool ui_gauges_status_render(uint32_t now)
{
    const usage_t *u = gauges_usage();
    /* never heard of (no subscription data, or an older daemon): STATUS stays as it was */
    bool any = u->at != 0 && (u->h5.known || u->wk.known);
    show(s_arcs, any);
    for (int i = 0; i < 2; i++) show(s_g[i].row, any);
    if (!any) return false;
    usage_win_t w5 = {0}, wk = {0};
    bool l5 = gauges_h5(now, &w5), lk = gauges_wk(now, &wk);
    gauge_render(&s_g[0], "5h", l5, &w5, now);
    gauge_render(&s_g[1], "Week", lk, &wk, now);
    return true;
}

void ui_gauges_status_report(ui_report_t *r)
{
    for (int i = 0; i < 2; i++) {
        if (lv_obj_has_flag(s_g[i].row, LV_OBJ_FLAG_HIDDEN)) continue;
        if (s_g[i].detail_txt[0] && !lv_obj_has_flag(s_g[i].detail, LV_OBJ_FLAG_HIDDEN))
            ui_report_add(r, "%s %s", s_g[i].fig_txt, s_g[i].detail_txt);
        else ui_report_add(r, "%s", s_g[i].fig_txt);
        if (s_g[i].pct >= 0) ui_report_add(r, "arc%d %d%%", i, s_g[i].pct);
    }
}

/* ── AGENT: one readout line under the caption ── */

#define READOUT_Y 108 /* between the caption (y 72) and the first row (y 164) */

static lv_obj_t *s_readout;
static char s_readout_txt[64];

void ui_gauges_agent_create(lv_obj_t *root)
{
    s_readout = label(root, FONT_BODY, COL_TEXT2);
    int lh = lv_font_get_line_height(FONT_BODY), w = theme_chord(READOUT_Y - lh / 2, 24);
    lv_obj_set_size(s_readout, w, lh);
    lv_obj_set_style_text_align(s_readout, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_readout, UI_CX - w / 2, READOUT_Y - lh / 2);
    lv_obj_add_flag(s_readout, LV_OBJ_FLAG_HIDDEN);
}

void ui_gauges_agent_update(const session_t *s, bool shown)
{
    char buf[64] = "";
    if (shown && s && s->agent != AGENT_SHELL) gauges_readout(buf, sizeof buf, s, theme_sep());
    show(s_readout, buf[0] != 0);
    theme_set_text(s_readout, s_readout_txt, sizeof s_readout_txt, buf);
}

void ui_gauges_agent_report(ui_report_t *r)
{
    if (s_readout_txt[0] && !lv_obj_has_flag(s_readout, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_readout_txt);
}
