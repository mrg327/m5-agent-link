/* The done summary in Sigil (docs/design/GLYPHS.md §5 DONE, docs/design/DONE-SUMMARY.md): what a
 * finished turn did, as one row of marks and not a word.
 *
 *   files   the files glyph with their count in a badge at its corner (a count: rule 4)
 *   lines   a two-colour bar, green for added and vermillion for removed, split in their
 *           proportion; its length steps with the size of the change (a digit more of lines, a step
 *           longer)
 *   tests   the flask with a tick in a green dot, or the flask in the error colour with a cross in
 *           a red one
 *   time    the timer glyph and the turn's minutes as a tally (ui_kit.h); under a minute the glyph
 *           alone
 *
 * Which parts show is still decided by done.c's line (the report prints that line, in words: rule
 * 9); the row drops the same parts in the same order when its marks do not fit: the lines, the
 * time, the files, never the tests. One row object draws itself; the tally is its child. Redraw
 * discipline (UI-V2 §8a): the row is invalidated only when what it shows changes.
 *
 * Two rows exist: the session card's (ui_home_card.c, which places it by moving the parent it gave)
 * and WATCH's. */
#include <stdio.h>
#include <string.h>

#include "done.h"
#include "ui_done.h"
#include "ui_kit.h"
#include "ui_theme.h"

/* the card's row, in its parent: the card puts the parent so this lands under the hero */
#define DONE_Y 250
#define ROW_H 56      /* the object's height: the glyphs and the badges that hang below them */
#define ROW_MAX_W 340 /* the chord at the row's lower edge, less a margin */
#define GLYPH_PX 44
#define GLYPH_W 40 /* the room a glyph takes */
/* the glyphs sit a little above the row's centre: their badges hang below */
#define GLYPH_DY (-3)
#define GAP 16     /* between two parts */
#define BADGE_H 24 /* the count badge: a pill at the glyph's lower right corner */
#define BADGE_PAD 7
#define BADGE_DX 23 /* its left edge, from the glyph's */
#define BADGE_DY 11 /* its centre, below the row's */
#define MARK_R 11   /* the tests' tick or cross: a dot at the flask's lower right */
#define MARK_DX 34
#define BAR_H 10
#define TALLY_GAP 6
#define RING 2 /* the black line that parts a badge from its glyph */

typedef struct {
    bool on[DONE_SEGS], pass, grey;
    uint32_t files, minutes;
    /* the bar's length and its green part, px; bar_add < 0: nothing changed */
    int16_t bar_w, bar_add;
    int16_t w[DONE_SEGS], total;
} view_t;

typedef struct {
    lv_obj_t *row, *tally;
    int cy, text_w;
    const lv_font_t *text_font; /* done.c measures its line of words in this font */
    view_t v;
    bool shown;
    char words[DONE_SEGS][56];
} done_row_t;

static done_row_t s_card, s_watch;
static const lv_font_t *s_measure_font;

static int measure(const char *text)
{
    lv_point_t sz;
    lv_text_get_size(&sz, text, s_measure_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

static int badge_w(uint32_t n)
{
    char t[12];
    lv_point_t sz;
    snprintf(t, sizeof t, "%u", (unsigned)n);
    lv_text_get_size(&sz, t, FONT_BODY_STRONG, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int w = sz.x + 2 * BADGE_PAD;
    return w < BADGE_H ? BADGE_H : w;
}

/* The bar: 24 px for a change of under ten lines, 12 px more for each digit, up to 72. Each side
 * that has lines keeps at least 6 px. */
static void bar_of(const done_sum_t *d, view_t *v)
{
    uint32_t total = d->added + d->removed;
    int w = 24;
    for (uint32_t t = total; t >= 10 && w < 72; t /= 10) w += 12;
    v->bar_w = (int16_t)w;
    if (!total) {
        v->bar_w = 16;
        v->bar_add = -1;
        return;
    }
    int add = (int)((uint64_t)(w - 2) * d->added / total);
    if (d->added && add < 6) add = 6;
    if (d->removed && add > w - 2 - 6) add = w - 2 - 6;
    v->bar_add = (int16_t)add;
}

static void widths(view_t *v)
{
    int tw = ui_tally_width(v->minutes);
    v->w[DONE_FILES] = (int16_t)(BADGE_DX + badge_w(v->files));
    v->w[DONE_LINES] = v->bar_w;
    v->w[DONE_TESTS] = (int16_t)(MARK_DX + MARK_R);
    v->w[DONE_TIME] = (int16_t)(GLYPH_W + (tw ? TALLY_GAP + tw : 0));
    int total = 0, n = 0;
    for (int i = 0; i < DONE_SEGS; i++)
        if (v->on[i]) {
            total += v->w[i];
            n++;
        }
    v->total = (int16_t)(total + (n > 1 ? (n - 1) * GAP : 0));
}

/* ───────── drawing ───────── */

static void fill(lv_layer_t *l, int x1, int y1, int x2, int y2, lv_color_t c)
{
    if (x2 < x1) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {x1, y1, x2, y2};
    lv_draw_rect(l, &d, &a);
}

static void row_draw(lv_event_t *e)
{
    done_row_t *d = lv_event_get_user_data(e);
    const view_t *v = &d->v;
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(d->row, &a);
    int cy = (a.y1 + a.y2) / 2, x = (a.x1 + a.x2 + 1) / 2 - v->total / 2;
    lv_color_t ink = v->grey ? COL_TEXT3 : COL_TEXT2,
               good = v->grey ? COL_TEXT3 : lv_color_hex(RGB_DONE),
               bad = v->grey ? COL_TEXT3 : lv_color_hex(RGB_ERROR);
    for (int i = 0; i < DONE_SEGS; i++) {
        if (!v->on[i]) continue;
        int gx = x + GLYPH_W / 2, gy = cy + GLYPH_DY;
        if (i == DONE_FILES) {
            char t[12];
            int bw = badge_w(v->files), bx = x + BADGE_DX, by = cy + BADGE_DY;
            snprintf(t, sizeof t, "%u", (unsigned)v->files);
            ui_glyph_draw(l, FONT_ICON_L, ICON_FILES, gx, gy, ink, LV_OPA_COVER);
            fill(l, bx - RING, by - BADGE_H / 2 - RING, bx + bw - 1 + RING,
                 by + BADGE_H / 2 - 1 + RING, COL_BG);
            fill(l, bx, by - BADGE_H / 2, bx + bw - 1, by + BADGE_H / 2 - 1, COL_FILL2);
            lv_draw_label_dsc_t td;
            lv_draw_label_dsc_init(&td);
            td.font = FONT_BODY_STRONG;
            td.color = v->grey ? COL_TEXT2 : COL_TEXT;
            td.align = LV_TEXT_ALIGN_CENTER;
            td.text = t;
            td.text_local = 1; /* `t` is on this stack: the draw task takes a copy */
            int lh = lv_font_get_line_height(FONT_BODY_STRONG);
            lv_area_t ta = {bx, by - lh / 2, bx + bw - 1, by - lh / 2 + lh - 1};
            lv_draw_label(l, &td, &ta);
        } else if (i == DONE_LINES) {
            int y1 = cy - BAR_H / 2, y2 = y1 + BAR_H - 1;
            if (v->bar_add < 0) fill(l, x, y1, x + v->bar_w - 1, y2, COL_TEXT3);
            else {
                if (v->bar_add > 0) fill(l, x, y1, x + v->bar_add - 1, y2, good);
                int rx = x + v->bar_add + (v->bar_add > 0 ? 2 : 0);
                fill(l, rx, y1, x + v->bar_w - 1, y2, bad);
            }
        } else if (i == DONE_TESTS) {
            int mx = x + MARK_DX, my = cy + BADGE_DY;
            ui_glyph_draw(l, FONT_ICON_L, ICON_G_TEST, gx, gy, v->pass ? ink : bad, LV_OPA_COVER);
            fill(l, mx - MARK_R - RING, my - MARK_R - RING, mx + MARK_R + RING, my + MARK_R + RING,
                 COL_BG);
            fill(l, mx - MARK_R, my - MARK_R, mx + MARK_R, my + MARK_R, v->pass ? good : bad);
            ui_glyph_draw(l, FONT_ICON_S, v->pass ? ICON_CHECK : ICON_X, mx, my, COL_ON_ACCENT,
                          LV_OPA_COVER);
        } else {
            ui_glyph_draw(l, FONT_ICON_L, ICON_TIMER, gx, cy, ink, LV_OPA_COVER);
        }
        x += v->w[i] + GAP;
    }
}

/* The tally is the time part's second half: it follows the row's layout. */
static void place_tally(done_row_t *d)
{
    const view_t *v = &d->v;
    int tw = ui_tally_width(v->minutes);
    bool on = d->shown && v->on[DONE_TIME] && tw > 0;
    if (lv_obj_has_flag(d->tally, LV_OBJ_FLAG_HIDDEN) == on) {
        if (on) lv_obj_remove_flag(d->tally, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(d->tally, LV_OBJ_FLAG_HIDDEN);
    }
    if (!on) return;
    int x = UI_CX - v->total / 2;
    for (int i = 0; i < DONE_TIME; i++)
        if (v->on[i]) x += v->w[i] + GAP;
    ui_tally_set_pos(d->tally, x + GLYPH_W + TALLY_GAP + tw / 2, d->cy);
    ui_tally_set(d->tally, v->minutes, v->grey ? COL_TEXT3 : COL_TEXT2);
}

static void row_create(done_row_t *d, lv_obj_t *parent, int cy, int text_w,
                       const lv_font_t *text_font)
{
    d->cy = cy;
    d->text_w = text_w;
    d->text_font = text_font;
    d->row = lv_obj_create(parent);
    lv_obj_remove_style_all(d->row);
    lv_obj_set_size(d->row, ROW_MAX_W, ROW_H);
    lv_obj_set_pos(d->row, UI_CX - ROW_MAX_W / 2, cy - ROW_H / 2);
    lv_obj_remove_flag(d->row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(d->row, row_draw, LV_EVENT_DRAW_MAIN, d);
    lv_obj_add_flag(d->row, LV_OBJ_FLAG_HIDDEN);
    d->tally = ui_tally_create(parent, UI_CX, cy);
    lv_obj_add_flag(d->tally, LV_OBJ_FLAG_HIDDEN);
}

static bool row_update(done_row_t *d, const session_t *s, bool allowed, bool grey)
{
    bool on = allowed && s && (s->state == SESS_DONE || s->state == SESS_ERROR) &&
              s->agent != AGENT_SHELL && done_known(&s->sum);
    if (!on) {
        if (d->shown) {
            d->shown = false;
            lv_obj_add_flag(d->row, LV_OBJ_FLAG_HIDDEN);
            place_tally(d);
        }
        return false;
    }
    const done_sum_t *sum = &s->sum;
    view_t v;
    memset(&v, 0, sizeof v); /* it is compared as bytes: no stray padding */
    v.pass = sum->tests_pass;
    v.grey = grey;
    v.files = sum->files;
    v.minutes = sum->secs / 60;
    bar_of(sum, &v);
    /* done.c decides which parts the line of words keeps; the marks then ask it for less, a step at
     * a time, until they fit: the parts go in the same order on glass and in the report */
    done_line_t line;
    s_measure_font = d->text_font;
    for (int max_w = d->text_w;; max_w -= 16) {
        done_line_build(sum, theme_sep(), max_w, measure, &line);
        memcpy(v.on, line.on, sizeof v.on);
        widths(&v);
        if (v.total <= ROW_MAX_W || max_w <= 0) break;
    }
    for (int i = 0; i < DONE_SEGS; i++)
        snprintf(d->words[i], sizeof d->words[i], "%.52s", line.on[i] ? line.txt[i] : "");
    if (!d->shown || memcmp(&v, &d->v, sizeof v)) {
        d->v = v;
        lv_obj_invalidate(d->row);
    }
    if (!d->shown) {
        d->shown = true;
        lv_obj_remove_flag(d->row, LV_OBJ_FLAG_HIDDEN);
    }
    place_tally(d);
    return true;
}

/* ───────── the session card's row ───────── */

void ui_done_create(lv_obj_t *centre)
{ row_create(&s_card, centre, DONE_Y, DONE_MAX_W, FONT_BODY); }

bool ui_done_update(const session_t *s, bool allowed, bool offline)
{ return row_update(&s_card, s, allowed, offline); }

/* Two entries (the report keeps few and short ones): files and lines, then tests and time. */
void ui_done_report(ui_report_t *r)
{
    char a[112] = "", b[112] = "";
    if (!s_card.shown) return;
    for (int i = 0; i < DONE_SEGS; i++) {
        char *dst = i < DONE_TESTS ? a : b;
        size_t n = strlen(dst);
        snprintf(dst + n, sizeof a - n, "%s", s_card.words[i]);
    }
    if (a[0]) ui_report_add(r, "%s", a);
    if (b[0]) ui_report_add(r, "%s", b);
}

/* ───────── WATCH's row ───────── */

void ui_done_watch_create(lv_obj_t *root, int cy)
{ row_create(&s_watch, root, cy, UI_DONE_WATCH_W, FONT_CAPTION); }

bool ui_done_watch_update(const session_t *s, bool allowed)
{ return row_update(&s_watch, s, allowed, false); }

/* One entry: the whole line, as WATCH's footer said it. */
void ui_done_watch_report(ui_report_t *r)
{
    char buf[224] = "";
    if (!s_watch.shown) return;
    for (int i = 0; i < DONE_SEGS; i++) {
        size_t n = strlen(buf);
        snprintf(buf + n, sizeof buf - n, "%s", s_watch.words[i]);
    }
    if (buf[0]) ui_report_add(r, "%s", buf);
}
