/* Sigil primitives (docs/design/GLYPHS.md §3); the API and what each one is for are in ui_kit.h.
 *
 * Each primitive is a plain lv_obj with a draw callback and a small heap struct in its user data (freed with
 * the object). Redraw discipline (UI-V2 §8a): a setter compares before it writes, and what changes is
 * invalidated as the smallest box that holds it: one dot, or an arc cut in 30 degree pieces so a long arc is
 * never one near full-screen box. The draw callbacks skip what lies outside the layer's clip area. */
#include "ui_kit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "sha256.h"
#include "ui_theme.h"

#define DEG2RAD 0.017453292f

/* ───────── shared ───────── */

static void on_delete(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    void *d = lv_obj_get_user_data(o);
    if (d) lv_free(d);
    lv_obj_set_user_data(o, NULL);
}

static lv_obj_t *blank(lv_obj_t *parent, size_t data_size, lv_event_cb_t draw)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    if (data_size) {
        void *d = lv_malloc_zeroed(data_size);
        LV_ASSERT_MALLOC(d);
        lv_obj_set_user_data(o, d);
        lv_obj_add_event_cb(o, on_delete, LV_EVENT_DELETE, NULL);
    }
    if (draw) lv_obj_add_event_cb(o, draw, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

/* A square object of half-side `half` centred on (cx, cy); moves only when the position differs. */
static void place(lv_obj_t *o, int cx, int cy, int half)
{
    int side = 2 * half + 1;
    if (lv_obj_get_style_width(o, 0) != side) lv_obj_set_size(o, side, side);
    if (lv_obj_get_style_x(o, 0) != cx - half || lv_obj_get_style_y(o, 0) != cy - half) lv_obj_set_pos(o, cx - half, cy - half);
}

/* The object's centre on the screen. */
static void centre_of(lv_obj_t *o, int *cx, int *cy)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    *cx = (a.x1 + a.x2) / 2;
    *cy = (a.y1 + a.y2) / 2;
}

static void polar(int cx, int cy, float r, float deg, int *x, int *y)
{
    *x = (int)lroundf((float)cx + r * sinf(deg * DEG2RAD));
    *y = (int)lroundf((float)cy - r * cosf(deg * DEG2RAD));
}

/* Something of radius `r` at (x, y) may touch the layer's clip area. */
static bool in_clip(const lv_layer_t *l, int x, int y, int r)
{
    const lv_area_t *c = &l->_clip_area;
    return x + r >= c->x1 && x - r <= c->x2 && y + r >= c->y1 && y - r <= c->y2;
}

static void dot(lv_layer_t *l, int x, int y, int r, lv_color_t c)
{
    if (!in_clip(l, x, y, r + 1)) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {x - r, y - r, x + r, y + r};
    lv_draw_rect(l, &d, &a);
}

/* A round-capped arc whose outer edge is at r_out, a0..a1 degrees clockwise from 12 o'clock. 360 degrees or
 * more is a closed ring without caps. */
static void arc(lv_layer_t *l, int cx, int cy, int r_out, int w, int a0, int a1, lv_color_t c)
{
    if (a1 <= a0 || w <= 0 || r_out <= 0 || !in_clip(l, cx, cy, r_out + w)) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = (uint16_t)r_out;
    d.width = (uint16_t)w;
    d.color = c;
    if (a1 - a0 >= 360) { /* two overlapping halves: one 0..359 arc leaves a seam (as the index ring does) */
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

/* Invalidates the band r_in..r_out between a0 and a1 degrees about (cx, cy), `pad` px round it (a round cap). */
static void inv_arc(lv_obj_t *o, int cx, int cy, int r_in, int r_out, int a0, int a1, int pad)
{
    if (a0 > a1) {
        int t = a0;
        a0 = a1;
        a1 = t;
    }
    for (int a = a0;; a += 30) {
        int b = a + 30 < a1 ? a + 30 : a1;
        lv_area_t box = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
        for (int k = a;; k += 5) {
            if (k > b) k = b;
            for (int ri = 0; ri < 2; ri++) {
                int x, y;
                polar(cx, cy, (float)(ri ? r_out : r_in), (float)k, &x, &y);
                if (x < box.x1) box.x1 = x;
                if (y < box.y1) box.y1 = y;
                if (x > box.x2) box.x2 = x;
                if (y > box.y2) box.y2 = y;
            }
            if (k >= b) break;
        }
        box.x1 -= pad;
        box.y1 -= pad;
        box.x2 += pad;
        box.y2 += pad;
        lv_obj_invalidate_area(o, &box);
        if (b >= a1) break;
    }
}

/* ───────── glyphs ───────── */

/* The next code point of a UTF-8 string, 0 at its end. */
static uint32_t next_cp(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    if (!s[0]) return 0;
    uint32_t c;
    int n;
    if (s[0] < 0x80) c = s[0], n = 1;
    else if ((s[0] & 0xE0) == 0xC0) c = s[0] & 0x1F, n = 2;
    else if ((s[0] & 0xF0) == 0xE0) c = s[0] & 0x0F, n = 3;
    else c = s[0] & 0x07, n = 4;
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) { /* cut short: stop here */
            *p += i;
            return 0;
        }
        c = c << 6 | (s[i] & 0x3F);
    }
    *p += n;
    return c;
}

#define GLYPH_ROW 4 /* icons in one sign */

/* Width and height of the ink of a row of icons; returns how many there are. */
static int glyph_measure(const lv_font_t *font, const char *icons, int *w, int *h)
{
    int n = 0, tw = 0, th = 0, gap = lv_font_get_line_height(font) / 6;
    for (uint32_t cp; n < GLYPH_ROW && (cp = next_cp(&icons)) != 0;) {
        lv_font_glyph_dsc_t g;
        if (cp == ' ' || !lv_font_get_glyph_dsc(font, &g, cp, 0)) continue;
        tw += g.box_w + (n ? gap : 0);
        if (g.box_h > th) th = g.box_h;
        n++;
    }
    *w = tw;
    *h = th;
    return n;
}

void ui_glyph_draw(lv_layer_t *l, const lv_font_t *font, const char *icons, int cx, int cy, lv_color_t c, lv_opa_t opa)
{
    if (!icons || !icons[0]) return;
    int tw, th, lh = lv_font_get_line_height(font), gap = lh / 6;
    if (!glyph_measure(font, icons, &tw, &th)) return;
    if (!in_clip(l, cx, cy, (tw > th ? tw : th) / 2 + 2)) return;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = font;
    d.color = c;
    d.opa = opa;
    int x = cx - tw / 2, n = 0;
    for (uint32_t cp; n < GLYPH_ROW && (cp = next_cp(&icons)) != 0;) {
        lv_font_glyph_dsc_t g;
        if (cp == ' ' || !lv_font_get_glyph_dsc(font, &g, cp, 0)) continue;
        /* a label puts the ink at x + ofs_x, y + line_height - base_line - box_h - ofs_y: solve for the ink's centre */
        lv_point_t pt = {x - g.ofs_x, cy - g.box_h / 2 - (lh - font->base_line - g.box_h - g.ofs_y)};
        lv_draw_character(l, &d, &pt, cp);
        x += g.box_w + gap;
        n++;
    }
}

const lv_font_t *ui_glyph_font(const char *icons, int px)
{
    static const lv_font_t *const fonts[] = {FONT_ICON_HERO, FONT_ICON_XXL, FONT_ICON_XL, FONT_ICON_L, FONT_ICON_M, FONT_ICON_S};
    static const int size[] = {128, 96, 64, 44, 28, 20};
    for (size_t i = 0; i < sizeof size / sizeof size[0]; i++) {
        if (size[i] > px && i + 1 < sizeof size / sizeof size[0]) continue;
        bool all = true;
        const char *p = icons ? icons : "";
        for (uint32_t cp; all && (cp = next_cp(&p)) != 0;) {
            lv_font_glyph_dsc_t g;
            if (cp != ' ' && !lv_font_get_glyph_dsc(fonts[i], &g, cp, 0)) all = false;
        }
        if (all) return fonts[i];
    }
    return FONT_ICON_S;
}

typedef struct {
    const lv_font_t *font;
    const char *icons;
    lv_color_t col;
    int16_t cx, cy;
} glyph_t;

static void glyph_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    glyph_t *g = lv_obj_get_user_data(o);
    int cx, cy;
    centre_of(o, &cx, &cy);
    ui_glyph_draw(lv_event_get_layer(e), g->font, g->icons, cx, cy, g->col, LV_OPA_COVER);
}

/* The object is as big as its ink: nothing round it is redrawn when the glyph changes. */
static void glyph_fit(lv_obj_t *o)
{
    glyph_t *g = lv_obj_get_user_data(o);
    int w = 0, h = 0;
    if (g->icons) glyph_measure(g->font, g->icons, &w, &h);
    int hw = w / 2 + 2, hh = h / 2 + 2;
    lv_obj_set_size(o, 2 * hw + 1, 2 * hh + 1);
    lv_obj_set_pos(o, g->cx - hw, g->cy - hh);
}

lv_obj_t *ui_glyph_create(lv_obj_t *parent, const lv_font_t *font, int cx, int cy)
{
    lv_obj_t *o = blank(parent, sizeof(glyph_t), glyph_draw);
    glyph_t *g = lv_obj_get_user_data(o);
    g->font = font;
    g->icons = "";
    g->col = COL_TEXT;
    g->cx = (int16_t)cx;
    g->cy = (int16_t)cy;
    glyph_fit(o);
    return o;
}

void ui_glyph_set(lv_obj_t *o, const char *icons, lv_color_t c)
{
    glyph_t *g = lv_obj_get_user_data(o);
    if (!icons) icons = "";
    bool same_icons = g->icons == icons || !strcmp(g->icons, icons);
    if (same_icons && lv_color_eq(g->col, c)) return;
    g->icons = icons;
    g->col = c;
    if (!same_icons) glyph_fit(o);
    lv_obj_invalidate(o);
}

void ui_glyph_set_pos(lv_obj_t *o, int cx, int cy)
{
    glyph_t *g = lv_obj_get_user_data(o);
    if (g->cx == cx && g->cy == cy) return;
    g->cx = (int16_t)cx;
    g->cy = (int16_t)cy;
    glyph_fit(o);
}

/* ───────── §3.1 sigil ───────── */

typedef struct {
    uint8_t h[UI_SIGIL_HASH];
    bool named;
    int16_t r;
    lv_color_t col;
} sigil_t;

static int sigil_w(int r) { return r <= 24 ? 3 : r <= 40 ? 4 : (r + 5) / 10; }

void ui_sigil_hash(const char *name, uint8_t h[UI_SIGIL_HASH])
{
    sha256_t c;
    uint8_t out[32];
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *)name, strlen(name));
    sha256_final(&c, out);
    memcpy(h, out, UI_SIGIL_HASH);
}

void ui_sigil_paint(lv_layer_t *l, const uint8_t h[UI_SIGIL_HASH], int cx, int cy, int r, int w, lv_color_t c)
{
    if (w <= 0) w = sigil_w(r);
    if (!in_clip(l, cx, cy, r + 1)) return;
    for (int i = 0; i < 3; i++) {
        /* one byte picks the sectors, two more force at least two; neighbours merge into one arc */
        unsigned bits = h[i] | 1u << (h[3 + i] % 8) | 1u << (h[6 + i] % 8);
        int rr = (int)lroundf((float)r * (1.0f - (float)i * 0.3f));
        for (int a = 0; a < 8;) {
            if (!(bits >> a & 1)) {
                a++;
                continue;
            }
            int b = a;
            while (b + 1 < 8 && (bits >> (b + 1) & 1)) b++;
            arc(l, cx, cy, rr, w, a * 45 + 9, (b + 1) * 45 - 9, c);
            a = b + 1;
        }
    }
    if (h[9] & 1) { /* the core: a dot, or a bar */
        int cr = (int)lroundf((float)r * 0.12f);
        dot(l, cx, cy, cr < 2 ? 2 : cr, c);
    } else {
        int half = (int)lroundf((float)r * 0.2f);
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_color = c;
        d.bg_opa = LV_OPA_COVER;
        lv_area_t a = {cx - half - w / 2, cy - w / 2, cx + half + w / 2, cy - w / 2 + w - 1};
        lv_draw_rect(l, &d, &a);
    }
}

static void sigil_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    sigil_t *s = lv_obj_get_user_data(o);
    int cx, cy;
    centre_of(o, &cx, &cy);
    if (s->named) ui_sigil_paint(lv_event_get_layer(e), s->h, cx, cy, s->r, 0, s->col);
    else ui_glyph_draw(lv_event_get_layer(e), ui_glyph_font(ICON_NO_FOLDER, s->r * 2), ICON_NO_FOLDER, cx, cy, s->col, LV_OPA_COVER);
}

lv_obj_t *ui_sigil_create(lv_obj_t *parent, int cx, int cy, int r)
{
    lv_obj_t *o = blank(parent, sizeof(sigil_t), sigil_draw);
    sigil_t *s = lv_obj_get_user_data(o);
    s->r = (int16_t)r;
    s->col = COL_TEXT;
    place(o, cx, cy, r + 2);
    return o;
}

void ui_sigil_set_name(lv_obj_t *o, const char *name)
{
    sigil_t *s = lv_obj_get_user_data(o);
    uint8_t h[UI_SIGIL_HASH] = {0};
    bool named = name && name[0];
    if (named) ui_sigil_hash(name, h);
    if (named == s->named && !memcmp(h, s->h, sizeof h)) return;
    s->named = named;
    memcpy(s->h, h, sizeof h);
    lv_obj_invalidate(o);
}

void ui_sigil_set_color(lv_obj_t *o, lv_color_t c)
{
    sigil_t *s = lv_obj_get_user_data(o);
    if (lv_color_eq(s->col, c)) return;
    s->col = c;
    lv_obj_invalidate(o);
}

void ui_sigil_set_pos(lv_obj_t *o, int cx, int cy)
{
    sigil_t *s = lv_obj_get_user_data(o);
    place(o, cx, cy, s->r + 2);
}

/* ───────── §3.3 time dots ───────── */

#define DOT_R 2      /* a minute */
#define DOT_R_TEN 4  /* every tenth */
#define DOT_PAD 6    /* the object reaches this far past the circle */
#define DOT_DIM 35   /* % brightness of the minutes still to come in a running hour */
#define PIP_IN 12    /* hour pips: this far inside the circle */
#define PIP_R 3
#define PIP_STEP 12  /* degrees between hour pips */
#define PIP_MAX 12

typedef struct {
    int16_t r;
    uint32_t min;
    lv_color_t col;
} dots_t;

static void dots_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    dots_t *d = lv_obj_get_user_data(o);
    lv_layer_t *l = lv_event_get_layer(e);
    int cx, cy, x, y;
    centre_of(o, &cx, &cy);
    uint32_t n = d->min >= 60 ? 60 : d->min, lit = d->min >= 60 ? d->min % 60 : d->min;
    lv_color_t dim = lv_color_mix(d->col, lv_color_black(), (uint8_t)(255 * DOT_DIM / 100));
    for (uint32_t i = 0; i < n; i++) {
        polar(cx, cy, (float)d->r, (float)(i * 6), &x, &y);
        dot(l, x, y, (i + 1) % 10 == 0 ? DOT_R_TEN : DOT_R, i < lit ? d->col : dim);
    }
    uint32_t hours = d->min / 60;
    for (uint32_t i = 0; i < hours && i < PIP_MAX; i++) {
        polar(cx, cy, (float)(d->r - PIP_IN), (float)(i * PIP_STEP), &x, &y);
        dot(l, x, y, PIP_R, d->col);
    }
}

lv_obj_t *ui_dots_create(lv_obj_t *parent, int cx, int cy, int r)
{
    lv_obj_t *o = blank(parent, sizeof(dots_t), dots_draw);
    dots_t *d = lv_obj_get_user_data(o);
    d->r = (int16_t)r;
    d->col = COL_TEXT;
    place(o, cx, cy, r + DOT_PAD);
    return o;
}

void ui_dots_set(lv_obj_t *o, uint32_t minutes, lv_color_t c)
{
    dots_t *d = lv_obj_get_user_data(o);
    bool same_col = lv_color_eq(d->col, c);
    if (same_col && d->min == minutes) return;
    /* the next minute of the same hour: one dot appears (or turns bright) */
    bool one = same_col && minutes == d->min + 1 && minutes / 60 == d->min / 60;
    d->min = minutes;
    d->col = c;
    if (!one) {
        lv_obj_invalidate(o);
        return;
    }
    int cx, cy, x, y;
    centre_of(o, &cx, &cy);
    polar(cx, cy, (float)d->r, (float)((minutes % 60 == 0 ? 59 : minutes % 60 - 1) * 6), &x, &y);
    lv_area_t a = {x - DOT_R_TEN - 1, y - DOT_R_TEN - 1, x + DOT_R_TEN + 1, y + DOT_R_TEN + 1};
    lv_obj_invalidate_area(o, &a);
}

/* The tally: bars for hours, large dots for tens of minutes, small dots for minutes. */
#define TALLY_H 13
#define TALLY_BAR_W 3
#define TALLY_BAR_PITCH 7
#define TALLY_TEN_R 3
#define TALLY_TEN_PITCH 10
#define TALLY_ONE_R 2
#define TALLY_ONE_PITCH 8
#define TALLY_GROUP 4 /* extra gap between two kinds */
#define TALLY_MAX_W 190

typedef struct {
    uint32_t min;
    lv_color_t col;
    int16_t cx, cy;
} tally_t;

static void tally_parts(uint32_t minutes, int *h, int *t, int *u)
{
    *h = (int)(minutes / 60 > 9 ? 9 : minutes / 60);
    *t = (int)(minutes % 60 / 10);
    *u = (int)(minutes % 10);
}

int ui_tally_width(uint32_t minutes)
{
    int h, t, u;
    tally_parts(minutes, &h, &t, &u);
    int kinds = (h > 0) + (t > 0) + (u > 0);
    if (!kinds) return 0;
    /* each mark takes its pitch; the last one ends at its own edge, not a pitch later */
    int w = h * TALLY_BAR_PITCH + t * TALLY_TEN_PITCH + u * TALLY_ONE_PITCH + (kinds - 1) * TALLY_GROUP;
    int last = u ? TALLY_ONE_PITCH - (2 * TALLY_ONE_R + 1) : t ? TALLY_TEN_PITCH - (2 * TALLY_TEN_R + 1) : TALLY_BAR_PITCH - TALLY_BAR_W;
    return w - last;
}

static void tally_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    tally_t *d = lv_obj_get_user_data(o);
    lv_layer_t *l = lv_event_get_layer(e);
    int cx, cy, h, t, u;
    centre_of(o, &cx, &cy);
    tally_parts(d->min, &h, &t, &u);
    int x = cx - ui_tally_width(d->min) / 2;
    for (int i = 0; i < h; i++, x += TALLY_BAR_PITCH) {
        lv_draw_rect_dsc_t r;
        lv_draw_rect_dsc_init(&r);
        r.radius = LV_RADIUS_CIRCLE;
        r.bg_color = d->col;
        r.bg_opa = LV_OPA_COVER;
        lv_area_t a = {x, cy - TALLY_H / 2, x + TALLY_BAR_W - 1, cy - TALLY_H / 2 + TALLY_H - 1};
        lv_draw_rect(l, &r, &a);
    }
    if (h && (t || u)) x += TALLY_GROUP;
    for (int i = 0; i < t; i++, x += TALLY_TEN_PITCH) dot(l, x + TALLY_TEN_R, cy, TALLY_TEN_R, d->col);
    if (t && u) x += TALLY_GROUP;
    for (int i = 0; i < u; i++, x += TALLY_ONE_PITCH) dot(l, x + TALLY_ONE_R, cy, TALLY_ONE_R, d->col);
}

static void tally_place(lv_obj_t *o)
{
    tally_t *d = lv_obj_get_user_data(o);
    lv_obj_set_pos(o, d->cx - TALLY_MAX_W / 2, d->cy - TALLY_H / 2 - 1);
}

lv_obj_t *ui_tally_create(lv_obj_t *parent, int cx, int cy)
{
    lv_obj_t *o = blank(parent, sizeof(tally_t), tally_draw);
    tally_t *d = lv_obj_get_user_data(o);
    d->col = COL_TEXT;
    d->cx = (int16_t)cx;
    d->cy = (int16_t)cy;
    lv_obj_set_size(o, TALLY_MAX_W + 1, TALLY_H + 2);
    tally_place(o);
    return o;
}

void ui_tally_set(lv_obj_t *o, uint32_t minutes, lv_color_t c)
{
    tally_t *d = lv_obj_get_user_data(o);
    if (d->min == minutes && lv_color_eq(d->col, c)) return;
    d->min = minutes;
    d->col = c;
    lv_obj_invalidate(o); /* a small strip, once a minute */
}

void ui_tally_set_pos(lv_obj_t *o, int cx, int cy)
{
    tally_t *d = lv_obj_get_user_data(o);
    if (d->cx == cx && d->cy == cy) return;
    d->cx = (int16_t)cx;
    d->cy = (int16_t)cy;
    tally_place(o);
}

/* ───────── arcs ───────── */

typedef struct {
    int16_t r, w, start, span, pm, steps;
    lv_color_t fill, track;
} arc_t;

static int arc_end(const arc_t *a, int pm) { return a->start + (int)((int32_t)a->span * pm / 1000); }

static void arc_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    arc_t *a = lv_obj_get_user_data(o);
    lv_layer_t *l = lv_event_get_layer(e);
    int cx, cy, r_out = a->r + a->w / 2;
    centre_of(o, &cx, &cy);
    if (a->steps > 1) { /* a level: whole pieces, the gap between two is the caps and 4 px */
        bool track = !lv_color_eq(a->track, lv_color_black());
        int gap = (a->w + 4) * 57 / (a->r > 0 ? a->r : 1) + 1, lit = (a->pm * a->steps + 500) / 1000;
        for (int i = 0; i < a->steps; i++) {
            if (i >= lit && !track) break;
            arc(l, cx, cy, r_out, a->w, a->start + a->span * i / a->steps + gap / 2, a->start + a->span * (i + 1) / a->steps - gap / 2,
                i < lit ? a->fill : a->track);
        }
        return;
    }
    if (!lv_color_eq(a->track, lv_color_black())) arc(l, cx, cy, r_out, a->w, a->start, a->start + a->span, a->track);
    if (a->pm <= 0) return;
    int end = arc_end(a, a->pm);
    if (end <= a->start) end = a->start + 1; /* known and above nothing: at least the round cap */
    arc(l, cx, cy, r_out, a->w, a->start, end, a->fill);
}

lv_obj_t *ui_arc_create(lv_obj_t *parent, int cx, int cy, int r, int w)
{
    lv_obj_t *o = blank(parent, sizeof(arc_t), arc_draw);
    arc_t *a = lv_obj_get_user_data(o);
    a->r = (int16_t)r;
    a->w = (int16_t)w;
    a->span = 360;
    a->fill = COL_TEXT;
    a->track = lv_color_black();
    place(o, cx, cy, r + w + 2);
    return o;
}

void ui_arc_set_span(lv_obj_t *o, int start_deg, int span_deg)
{
    arc_t *a = lv_obj_get_user_data(o);
    if (a->start == start_deg && a->span == span_deg) return;
    a->start = (int16_t)start_deg;
    a->span = (int16_t)span_deg;
    lv_obj_invalidate(o);
}

void ui_arc_set(lv_obj_t *o, int permille, lv_color_t fill, lv_color_t track)
{
    arc_t *a = lv_obj_get_user_data(o);
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    bool same_col = lv_color_eq(a->fill, fill) && lv_color_eq(a->track, track);
    if (same_col && a->pm == permille) return;
    int old = a->pm;
    a->pm = (int16_t)permille;
    a->fill = fill;
    a->track = track;
    /* a closed ring is drawn without caps: going to or from it redraws the lot */
    if (!same_col || (a->span >= 360 && (old >= 1000 || permille >= 1000))) {
        lv_obj_invalidate(o);
        return;
    }
    int e0 = arc_end(a, old), e1 = arc_end(a, permille);
    if (e0 == e1 && (old > 0) == (permille > 0)) return; /* less than a degree: nothing moved */
    int cx, cy;
    centre_of(o, &cx, &cy);
    inv_arc(o, cx, cy, a->r - a->w / 2 - 1, a->r + a->w / 2 + 1, e0, e1, a->w / 2 + 2);
}

void ui_arc_set_steps(lv_obj_t *o, int n)
{
    arc_t *a = lv_obj_get_user_data(o);
    if (n < 0) n = 0;
    if (a->steps == n) return;
    a->steps = (int16_t)n;
    lv_obj_invalidate(o);
}

void ui_arc_set_geometry(lv_obj_t *o, int cx, int cy, int r)
{
    arc_t *a = lv_obj_get_user_data(o);
    a->r = (int16_t)r; /* a new radius is a new size: place() resizes, which redraws */
    place(o, cx, cy, r + a->w + 2);
}

/* ───────── track ───────── */

typedef struct {
    int16_t r, w, start, span, pos, len;
    lv_color_t thumb, track;
} track_t;

/* Where the thumb lies, in degrees (a0 < a1 whichever way the track runs). */
static void track_thumb(const track_t *t, int *a0, int *a1)
{
    int len = (int)((int32_t)t->span * t->len / 1000);
    int from = t->start + (int)((int32_t)(t->span - len) * t->pos / 1000);
    *a0 = len < 0 ? from + len : from;
    *a1 = len < 0 ? from : from + len;
    if (*a1 <= *a0) *a1 = *a0 + 1;
}

static void track_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    track_t *t = lv_obj_get_user_data(o);
    lv_layer_t *l = lv_event_get_layer(e);
    int cx, cy, a0 = t->span < 0 ? t->start + t->span : t->start, a1 = t->span < 0 ? t->start : t->start + t->span;
    int tw = t->w / 3 < 2 ? 2 : t->w / 3;
    centre_of(o, &cx, &cy);
    if (!lv_color_eq(t->track, lv_color_black())) arc(l, cx, cy, t->r + tw / 2, tw, a0, a1, t->track);
    if (t->len <= 0) return;
    track_thumb(t, &a0, &a1);
    arc(l, cx, cy, t->r + t->w / 2, t->w, a0, a1, t->thumb);
}

lv_obj_t *ui_track_create(lv_obj_t *parent, int cx, int cy, int r, int w, int start_deg, int span_deg)
{
    lv_obj_t *o = blank(parent, sizeof(track_t), track_draw);
    track_t *t = lv_obj_get_user_data(o);
    t->r = (int16_t)r;
    t->w = (int16_t)w;
    t->start = (int16_t)start_deg;
    t->span = (int16_t)span_deg;
    t->thumb = COL_TEXT;
    t->track = COL_FILL2;
    place(o, cx, cy, r + w + 2);
    return o;
}

void ui_track_set(lv_obj_t *o, int pos, int len, lv_color_t thumb, lv_color_t track)
{
    track_t *t = lv_obj_get_user_data(o);
    pos = pos < 0 ? 0 : pos > 1000 ? 1000 : pos;
    len = len < 0 ? 0 : len > 1000 ? 1000 : len;
    bool same_col = lv_color_eq(t->thumb, thumb) && lv_color_eq(t->track, track);
    if (same_col && t->pos == pos && t->len == len) return;
    int cx, cy, o0, o1, n0, n1, in = t->r - t->w / 2 - 1, out = t->r + t->w / 2 + 1, pad = t->w / 2 + 2;
    bool had = t->len > 0;
    centre_of(o, &cx, &cy);
    track_thumb(t, &o0, &o1);
    t->pos = (int16_t)pos;
    t->len = (int16_t)len;
    t->thumb = thumb;
    t->track = track;
    if (!lv_color_eq(track, lv_color_black()) && !same_col) { /* the whole arc, not the square it lies in */
        inv_arc(o, cx, cy, in, out, t->start, t->start + t->span, pad);
        return;
    }
    track_thumb(t, &n0, &n1);
    if (same_col && had && len > 0 && o0 == n0 && o1 == n1) return; /* less than a degree: nothing moved */
    if (had) inv_arc(o, cx, cy, in, out, o0, o1, pad);
    if (len > 0) inv_arc(o, cx, cy, in, out, n0, n1, pad);
}

/* ───────── waves ───────── */

#define WAVES_HALF 26 /* each arc: this many degrees either side of 3 and of 9 o'clock */
#define WAVES_W 6

typedef struct {
    int16_t r0, pitch, n;
    uint16_t mask;
    lv_color_t on, off;
} waves_t;

static void waves_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    waves_t *d = lv_obj_get_user_data(o);
    lv_layer_t *l = lv_event_get_layer(e);
    int cx, cy;
    centre_of(o, &cx, &cy);
    for (int i = 0; i < d->n; i++) {
        bool lit = d->mask >> i & 1;
        if (!lit && lv_color_eq(d->off, lv_color_black())) continue;
        int r_out = d->r0 + i * d->pitch + WAVES_W / 2;
        arc(l, cx, cy, r_out, WAVES_W, 90 - WAVES_HALF, 90 + WAVES_HALF, lit ? d->on : d->off);
        arc(l, cx, cy, r_out, WAVES_W, 270 - WAVES_HALF, 270 + WAVES_HALF, lit ? d->on : d->off);
    }
}

lv_obj_t *ui_waves_create(lv_obj_t *parent, int cx, int cy, int r0, int pitch, int n)
{
    lv_obj_t *o = blank(parent, sizeof(waves_t), waves_draw);
    waves_t *d = lv_obj_get_user_data(o);
    d->r0 = (int16_t)r0;
    d->pitch = (int16_t)pitch;
    d->n = (int16_t)(n > UI_WAVES_MAX ? UI_WAVES_MAX : n);
    d->on = COL_TEXT;
    d->off = COL_FILL2;
    place(o, cx, cy, r0 + (d->n - 1) * pitch + WAVES_W + 2);
    return o;
}

void ui_waves_set(lv_obj_t *o, unsigned mask, lv_color_t on, lv_color_t off)
{
    waves_t *d = lv_obj_get_user_data(o);
    mask &= (1u << d->n) - 1;
    unsigned changed = mask ^ d->mask;
    if (!lv_color_eq(d->on, on)) changed |= mask | d->mask;              /* the lit ones change colour */
    if (!lv_color_eq(d->off, off)) changed |= ~(mask & d->mask) & ((1u << d->n) - 1); /* and the dark ones */
    d->mask = (uint16_t)mask;
    d->on = on;
    d->off = off;
    if (!changed) return;
    int cx, cy;
    centre_of(o, &cx, &cy);
    for (int i = 0; i < d->n; i++) {
        if (!(changed >> i & 1)) continue;
        int r = d->r0 + i * d->pitch;
        inv_arc(o, cx, cy, r - WAVES_W / 2 - 1, r + WAVES_W / 2 + 1, 90 - WAVES_HALF, 90 + WAVES_HALF, WAVES_W / 2 + 2);
        inv_arc(o, cx, cy, r - WAVES_W / 2 - 1, r + WAVES_W / 2 + 1, 270 - WAVES_HALF, 270 + WAVES_HALF, WAVES_W / 2 + 2);
    }
}

/* ───────── pips ───────── */

typedef struct {
    int16_t n, cur;
} pips_t;

/* `n` pips in a row centred on (cx, cy), pip `cur` the current one. */
static void pips_paint(lv_layer_t *l, int cx, int cy, int n, int cur)
{
    if (n < 2) return;
    if (n > UI_PIPS_MAX) { /* too many to count: the same row, the position in proportion */
        cur = cur * (UI_PIPS_MAX - 1) / (n - 1);
        n = UI_PIPS_MAX;
    }
    int x = cx - (n - 1) * UI_PIPS_PITCH / 2;
    for (int i = 0; i < n; i++, x += UI_PIPS_PITCH) dot(l, x, cy, i == cur ? 4 : 3, i == cur ? COL_TEXT : COL_TEXT3);
}

static void pips_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    pips_t *d = lv_obj_get_user_data(o);
    int cx, cy;
    centre_of(o, &cx, &cy);
    pips_paint(lv_event_get_layer(e), cx, cy, d->n, d->cur);
}

lv_obj_t *ui_pips_create(lv_obj_t *parent, int cx, int cy)
{
    lv_obj_t *o = blank(parent, sizeof(pips_t), pips_draw);
    int hw = (UI_PIPS_MAX - 1) * UI_PIPS_PITCH / 2 + 5;
    lv_obj_set_size(o, 2 * hw + 1, 11);
    lv_obj_set_pos(o, cx - hw, cy - 5);
    return o;
}

void ui_pips_set(lv_obj_t *o, int n, int current)
{
    pips_t *d = lv_obj_get_user_data(o);
    if (d->n == n && d->cur == current) return;
    d->n = (int16_t)n;
    d->cur = (int16_t)current;
    lv_obj_invalidate(o); /* a strip 11 px high */
}

/* ───────── §3.4 disc ───────── */

#define HOLD_GAP 8
#define HOLD_W 6
#define KEY_HOLD_GAP 3
#define KEY_HOLD_W 3
#define KEY_RING_W 2

typedef struct {
    int16_t r, cx, cy, hold;
    uint32_t rgb;
    uint8_t state;
    bool track; /* the hold ring's path is drawn, dim, under the ring: this disc is held, not tapped */
    const char *glyph;
    const lv_font_t *font;
} disc_t;

static bool disc_is_key(const disc_t *d) { return d->r <= UI_DISC_R_KEY; }
static int disc_hold_gap(const disc_t *d) { return disc_is_key(d) ? KEY_HOLD_GAP : HOLD_GAP; }
static int disc_hold_w(const disc_t *d) { return disc_is_key(d) ? KEY_HOLD_W : HOLD_W; }
static int disc_half(const disc_t *d) { return d->r + disc_hold_gap(d) + disc_hold_w(d) + 2; }

static void disc_font(disc_t *d)
{
    int px = d->r >= 70 ? 96 : d->r >= 46 ? 64 : d->r >= 36 ? 44 : d->r >= 26 ? 28 : 20;
    d->font = ui_glyph_font(d->glyph, px);
}

static void disc_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    disc_t *d = lv_obj_get_user_data(o);
    lv_layer_t *l = lv_event_get_layer(e);
    int cx, cy;
    centre_of(o, &cx, &cy);
    lv_color_t accent = lv_color_hex(d->rgb ? d->rgb : RGB_ACCENT), fill, ink;
    if (d->state == UI_DISC_DISABLED) fill = COL_FILL1, ink = COL_TEXT3;
    else if (d->state == UI_DISC_SELECTED || d->rgb) fill = accent, ink = COL_ON_ACCENT;
    else fill = COL_FILL2, ink = d->r >= UI_DISC_R_ORBIT_SEL ? COL_TEXT : COL_TEXT2;
    if (disc_is_key(d)) { /* a hint, not an action: a thin ring and the glyph in the button's colour */
        ink = d->state == UI_DISC_DISABLED ? COL_TEXT3 : accent;
        arc(l, cx, cy, d->r, KEY_RING_W, 0, 360, ink);
    } else if (in_clip(l, cx, cy, d->r + 1)) {
        lv_draw_rect_dsc_t r;
        lv_draw_rect_dsc_init(&r);
        r.radius = LV_RADIUS_CIRCLE;
        r.bg_color = fill;
        r.bg_opa = LV_OPA_COVER;
        lv_area_t a = {cx - d->r, cy - d->r, cx + d->r, cy + d->r};
        lv_draw_rect(l, &r, &a);
    }
    ui_glyph_draw(l, d->font, d->glyph, cx, cy, ink, LV_OPA_COVER);
    if (d->track && d->hold < 1000) arc(l, cx, cy, d->r + disc_hold_gap(d) + disc_hold_w(d), disc_hold_w(d), 0, 360, COL_FILL2);
    if (d->hold > 0) arc(l, cx, cy, d->r + disc_hold_gap(d) + disc_hold_w(d), disc_hold_w(d), 0, d->hold * 360 / 1000, COL_TEXT);
}

lv_obj_t *ui_disc_create(lv_obj_t *parent, int cx, int cy, int r)
{
    lv_obj_t *o = blank(parent, sizeof(disc_t), disc_draw);
    disc_t *d = lv_obj_get_user_data(o);
    d->r = (int16_t)r;
    d->cx = (int16_t)cx;
    d->cy = (int16_t)cy;
    d->glyph = "";
    disc_font(d);
    place(o, cx, cy, disc_half(d));
    return o;
}

void ui_disc_set_glyph(lv_obj_t *o, const char *icon)
{
    disc_t *d = lv_obj_get_user_data(o);
    if (!icon) icon = "";
    if (d->glyph == icon || !strcmp(d->glyph, icon)) return;
    d->glyph = icon;
    disc_font(d);
    lv_obj_invalidate(o);
}

void ui_disc_set_rgb(lv_obj_t *o, uint32_t rgb)
{
    disc_t *d = lv_obj_get_user_data(o);
    if (d->rgb == rgb) return;
    d->rgb = rgb;
    lv_obj_invalidate(o);
}

void ui_disc_set_state(lv_obj_t *o, ui_disc_state_t st)
{
    disc_t *d = lv_obj_get_user_data(o);
    if (d->state == (uint8_t)st) return;
    d->state = (uint8_t)st;
    lv_obj_invalidate(o);
}

void ui_disc_set_track(lv_obj_t *o, bool on)
{
    disc_t *d = lv_obj_get_user_data(o);
    if (d->track == on) return;
    d->track = on;
    lv_obj_invalidate(o);
}

void ui_disc_set_pos(lv_obj_t *o, int cx, int cy)
{
    disc_t *d = lv_obj_get_user_data(o);
    if (d->cx == cx && d->cy == cy) return;
    d->cx = (int16_t)cx;
    d->cy = (int16_t)cy;
    place(o, cx, cy, disc_half(d));
}

void ui_disc_set_r(lv_obj_t *o, int r)
{
    disc_t *d = lv_obj_get_user_data(o);
    if (d->r == r) return;
    d->r = (int16_t)r;
    disc_font(d);
    place(o, d->cx, d->cy, disc_half(d));
    lv_obj_invalidate(o);
}

void ui_disc_set_hold(lv_obj_t *o, int permille)
{
    disc_t *d = lv_obj_get_user_data(o);
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    if (d->hold == permille) return;
    int old = d->hold;
    d->hold = (int16_t)permille;
    int a0 = old * 360 / 1000, a1 = permille * 360 / 1000;
    if (a0 == a1 && (old > 0) == (permille > 0)) return;
    if (old >= 1000 || permille >= 1000 || old == 0 || permille == 0) { /* the ring closes, opens, appears or goes */
        lv_obj_invalidate(o);
        return;
    }
    int cx, cy, in = d->r + disc_hold_gap(d);
    centre_of(o, &cx, &cy);
    inv_arc(o, cx, cy, in - 1, in + disc_hold_w(d) + 1, a0, a1, disc_hold_w(d) / 2 + 2);
}

bool ui_disc_hit(lv_obj_t *o, int x, int y)
{
    if (!o || !lv_obj_is_visible(o)) return false;
    disc_t *d = lv_obj_get_user_data(o);
    int cx, cy;
    centre_of(o, &cx, &cy);
    int dx = x - cx, dy = y - cy, r = d->r + UI_DISC_SLACK;
    return dx * dx + dy * dy <= r * r;
}

/* ───────── §3.5 orbit ───────── */

#define ORBIT_PIP_DY 98 /* page pips: under the subject, inside the discs */

typedef struct {
    ui_orbit_item_t it[UI_ORBIT_ITEMS];
    lv_obj_t *disc[UI_ORBIT_SLOTS];
    int16_t n, sel, radius, phase, hold;
    int16_t span, pitch; /* an arch (ui_orbit_set_arch): the arc it covers and the widest step; span 0 = the whole circle */
    uint32_t accent;
} orbit_t;

static int orbit_pages(const orbit_t *d) { return (d->n + UI_ORBIT_SLOTS - 1) / UI_ORBIT_SLOTS; }

static void orbit_pips_area(lv_obj_t *o, lv_area_t *a)
{
    int cx, cy;
    centre_of(o, &cx, &cy);
    *a = (lv_area_t){cx - 2 * UI_PIPS_PITCH, cy + ORBIT_PIP_DY - 5, cx + 2 * UI_PIPS_PITCH, cy + ORBIT_PIP_DY + 5};
}

static void orbit_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    orbit_t *d = lv_obj_get_user_data(o);
    int cx, cy;
    centre_of(o, &cx, &cy);
    pips_paint(lv_event_get_layer(e), cx, cy + ORBIT_PIP_DY, orbit_pages(d), d->sel / UI_ORBIT_SLOTS);
}

/* Puts the page of the selection on the discs. Every disc setter is change-only, so a step repaints two discs. */
static void orbit_apply(lv_obj_t *o)
{
    orbit_t *d = lv_obj_get_user_data(o);
    int first = d->n ? d->sel / UI_ORBIT_SLOTS * UI_ORBIT_SLOTS : 0, cnt = d->n - first;
    if (cnt > UI_ORBIT_SLOTS) cnt = UI_ORBIT_SLOTS;
    float step = d->n > UI_ORBIT_SLOTS ? 45.0f : d->n ? 360.0f / (float)d->n : 0, first_deg = (float)d->phase;
    if (d->span > 0) { /* an arch: the page's discs centred on the phase, left to right */
        step = cnt > 1 ? (float)d->span / (float)(cnt - 1) : 0;
        if (step > (float)d->pitch) step = (float)d->pitch;
        first_deg -= step * (float)(cnt - 1) / 2.0f;
    }
    int ccx = lv_obj_get_width(o) / 2, ccy = lv_obj_get_height(o) / 2, x, y;
    if (!ccx) ccx = ccy = UI_CX; /* before the first layout */
    for (int k = 0; k < UI_ORBIT_SLOTS; k++) {
        lv_obj_t *disc = d->disc[k];
        bool on = k < cnt;
        if (lv_obj_has_flag(disc, LV_OBJ_FLAG_HIDDEN) == on) {
            if (on) lv_obj_remove_flag(disc, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(disc, LV_OBJ_FLAG_HIDDEN);
        }
        if (!on) continue;
        const ui_orbit_item_t *it = &d->it[first + k];
        bool sel = first + k == d->sel;
        polar(ccx, ccy, (float)d->radius, first_deg + (float)k * step, &x, &y);
        ui_disc_set_pos(disc, x, y);
        ui_disc_set_r(disc, sel ? UI_DISC_R_ORBIT_SEL : UI_DISC_R_SMALL);
        ui_disc_set_glyph(disc, it->glyph);
        ui_disc_set_rgb(disc, sel ? (it->rgb ? it->rgb : d->accent) : 0);
        ui_disc_set_state(disc, it->disabled ? UI_DISC_DISABLED : sel ? UI_DISC_SELECTED : UI_DISC_NORMAL);
        ui_disc_set_hold(disc, sel ? d->hold : 0);
    }
}

lv_obj_t *ui_orbit_create(lv_obj_t *parent)
{
    lv_obj_t *o = blank(parent, sizeof(orbit_t), orbit_draw);
    orbit_t *d = lv_obj_get_user_data(o);
    lv_obj_set_size(o, UI_SIZE, UI_SIZE);
    d->radius = UI_ORBIT_R;
    d->accent = RGB_ACCENT;
    for (int k = 0; k < UI_ORBIT_SLOTS; k++) {
        d->disc[k] = ui_disc_create(o, UI_CX, UI_CX, UI_DISC_R_SMALL);
        lv_obj_add_flag(d->disc[k], LV_OBJ_FLAG_HIDDEN);
    }
    return o;
}

void ui_orbit_set_items(lv_obj_t *o, const ui_orbit_item_t *items, int n)
{
    orbit_t *d = lv_obj_get_user_data(o);
    if (n > UI_ORBIT_ITEMS) n = UI_ORBIT_ITEMS;
    if (n < 0) n = 0;
    int pages = orbit_pages(d);
    d->n = (int16_t)n;
    memcpy(d->it, items, (size_t)n * sizeof d->it[0]);
    if (d->sel >= n) d->sel = (int16_t)(n ? n - 1 : 0);
    if (pages != orbit_pages(d)) {
        lv_area_t a;
        orbit_pips_area(o, &a);
        lv_obj_invalidate_area(o, &a);
    }
    orbit_apply(o);
}

void ui_orbit_set_accent(lv_obj_t *o, uint32_t rgb)
{
    orbit_t *d = lv_obj_get_user_data(o);
    if (d->accent == rgb) return;
    d->accent = rgb;
    orbit_apply(o);
}

void ui_orbit_set_geometry(lv_obj_t *o, int radius, int phase_deg)
{
    orbit_t *d = lv_obj_get_user_data(o);
    if (d->radius == radius && d->phase == phase_deg) return;
    d->radius = (int16_t)radius;
    d->phase = (int16_t)phase_deg;
    orbit_apply(o);
}

void ui_orbit_set_arch(lv_obj_t *o, int span_deg, int max_pitch_deg)
{
    orbit_t *d = lv_obj_get_user_data(o);
    if (d->span == span_deg && d->pitch == max_pitch_deg) return;
    d->span = (int16_t)span_deg;
    d->pitch = (int16_t)max_pitch_deg;
    orbit_apply(o);
}

void ui_orbit_select(lv_obj_t *o, int i)
{
    orbit_t *d = lv_obj_get_user_data(o);
    if (i < 0 || i >= d->n || i == d->sel) return;
    bool turned = i / UI_ORBIT_SLOTS != d->sel / UI_ORBIT_SLOTS;
    d->sel = (int16_t)i;
    d->hold = 0; /* a hold belongs to the disc it began on */
    if (turned) {
        lv_area_t a;
        orbit_pips_area(o, &a);
        lv_obj_invalidate_area(o, &a);
    }
    orbit_apply(o);
}

int ui_orbit_step(lv_obj_t *o, int dir)
{
    orbit_t *d = lv_obj_get_user_data(o);
    if (d->n > 1) ui_orbit_select(o, ((d->sel + dir) % d->n + d->n) % d->n);
    return d->sel;
}

int ui_orbit_selected(lv_obj_t *o) { return ((orbit_t *)lv_obj_get_user_data(o))->sel; }
int ui_orbit_count(lv_obj_t *o) { return ((orbit_t *)lv_obj_get_user_data(o))->n; }

int ui_orbit_hit(lv_obj_t *o, int x, int y)
{
    orbit_t *d = lv_obj_get_user_data(o);
    int first = d->n ? d->sel / UI_ORBIT_SLOTS * UI_ORBIT_SLOTS : 0;
    /* the selected disc first: it is the larger one, and its slack may reach a neighbour's */
    if (d->n && ui_disc_hit(d->disc[d->sel - first], x, y)) return d->sel;
    for (int k = 0; k < UI_ORBIT_SLOTS && first + k < d->n; k++)
        if (ui_disc_hit(d->disc[k], x, y)) return first + k;
    return -1;
}

void ui_orbit_set_hold(lv_obj_t *o, int permille)
{
    orbit_t *d = lv_obj_get_user_data(o);
    if (!d->n) return;
    d->hold = (int16_t)permille;
    ui_disc_set_hold(d->disc[d->sel % UI_ORBIT_SLOTS], permille);
}

/* ───────── §3.6 key hints ───────── */

lv_obj_t *ui_key_hint_create(lv_obj_t *parent, ui_key_t key)
{
    lv_obj_t *o = ui_disc_create(parent, key == UI_KEY_A ? UI_KEY_A_X : UI_KEY_B_X, UI_KEY_Y, UI_DISC_R_KEY);
    ui_disc_set_rgb(o, key == UI_KEY_A ? RGB_KEY_A : RGB_KEY_B);
    return o;
}

void ui_key_hint_set(lv_obj_t *hint, const char *icon) { ui_disc_set_glyph(hint, icon); }

/* ───────── §3.7 burst ───────── */

#define BURST_R 208 /* the black centre: inside the countdown (r 213) and the index ring */

static lv_obj_t *s_burst;
static char s_burst_words[40], s_burst_icons[16];
static lv_color_t s_burst_col;
static uint32_t s_burst_until;
static bool s_burst_new;

static void burst_draw(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    dot(l, UI_CX, UI_CX, BURST_R, COL_BG);
    ui_glyph_draw(l, ui_glyph_font(s_burst_icons, 128), s_burst_icons, UI_CX, UI_CX, s_burst_col, LV_OPA_COVER);
}

void ui_burst_init(lv_obj_t *top)
{
    s_burst = blank(top, 0, burst_draw);
    lv_obj_set_size(s_burst, 2 * BURST_R + 1, 2 * BURST_R + 1);
    lv_obj_set_pos(s_burst, UI_CX - BURST_R, UI_CX - BURST_R);
    lv_obj_add_flag(s_burst, LV_OBJ_FLAG_HIDDEN);
}

void ui_burst(const char *words, const char *icons, uint32_t rgb)
{
    if (!s_burst) return;
    if (!icons) icons = theme_glyph(words);
    if (!icons) icons = ICON_DONE;
    snprintf(s_burst_words, sizeof s_burst_words, "%s", words);
    snprintf(s_burst_icons, sizeof s_burst_icons, "%s", icons);
    s_burst_col = lv_color_hex(rgb);
    s_burst_new = true; /* the clock starts at the next tick: this file has no time of its own */
    lv_obj_remove_flag(s_burst, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_burst);
    dbg_event("ui", "burst", "\"text\":%s", dbg_q(words));
}

bool ui_burst_active(void) { return s_burst && !lv_obj_has_flag(s_burst, LV_OBJ_FLAG_HIDDEN); }

void ui_burst_tick(uint32_t now_ms)
{
    if (!ui_burst_active()) return;
    if (s_burst_new) {
        s_burst_new = false;
        s_burst_until = now_ms + UI_BURST_MS;
    } else if ((int32_t)(now_ms - s_burst_until) >= 0) {
        lv_obj_add_flag(s_burst, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_burst_report(ui_report_t *r)
{
    if (ui_burst_active()) ui_report_add(r, "%s", s_burst_words);
}

/* ───────── §3.7 sign ───────── */

#define SIGN_H 56
#define SIGN_H_TEXT 44
#define SIGN_TEXT_W 210

typedef struct {
    int16_t cx, cy;
} sign_t;

lv_obj_t *ui_sign_create(lv_obj_t *parent, int cx, int cy)
{
    lv_obj_t *s = blank(parent, sizeof(sign_t), NULL);
    sign_t *d = lv_obj_get_user_data(s);
    d->cx = (int16_t)cx;
    d->cy = (int16_t)cy;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s, 2, 0);
    lv_obj_set_style_outline_width(s, 6, 0); /* a black margin: what it lies over never touches the capsule */
    lv_obj_set_style_outline_color(s, COL_BG, 0);
    lv_obj_set_style_outline_pad(s, 0, 0);
    lv_obj_set_style_pad_hor(s, 20, 0);
    lv_obj_set_style_pad_column(s, 10, 0);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *icons = lv_label_create(s);
    lv_obj_set_style_text_letter_space(icons, 6, 0);
    lv_label_set_text(icons, "");
    lv_obj_t *text = lv_label_create(s);
    lv_obj_set_style_text_font(text, FONT_BODY_STRONG, 0);
    lv_obj_set_style_text_color(text, COL_TEXT, 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(text, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(text, SIGN_TEXT_W, 0);
    lv_obj_set_height(text, lv_font_get_line_height(FONT_BODY_STRONG)); /* one line: the dots need a fixed height */
    lv_label_set_text(text, "");
    return s;
}

void ui_sign_set(lv_obj_t *s, const char *icons, const char *text, uint32_t rgb)
{
    sign_t *d = lv_obj_get_user_data(s);
    lv_obj_t *il = lv_obj_get_child(s, 0), *tl = lv_obj_get_child(s, 1);
    bool has_text = text && text[0];
    int h = has_text ? SIGN_H_TEXT : SIGN_H;
    lv_obj_set_style_border_color(s, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_font(il, has_text ? FONT_ICON_M : FONT_ICON_L, 0);
    lv_obj_set_style_text_color(il, lv_color_hex(rgb), 0);
    lv_label_set_text(il, icons ? icons : "");
    lv_label_set_text(tl, has_text ? text : "");
    if (has_text) lv_obj_remove_flag(tl, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(tl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s, LV_SIZE_CONTENT, h);
    lv_obj_update_layout(s);
    lv_obj_set_pos(s, d->cx - lv_obj_get_width(s) / 2, d->cy - h / 2);
}

/* ───────── the gallery (simulator only) ───────── */

#ifdef UI_KIT_DEMO
static lv_obj_t *s_demo;

static lv_obj_t *demo_name(lv_obj_t *parent, const char *t, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, FONT_BODY, 0);
    lv_obj_set_style_text_color(l, COL_TEXT2, 0);
    lv_label_set_text(l, t);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y - 12);
    return l;
}

void ui_kit_demo(int page)
{
    if (!s_demo) {
        s_demo = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_demo);
        lv_obj_set_size(s_demo, UI_SIZE, UI_SIZE);
        lv_obj_set_style_bg_color(s_demo, COL_BG, 0);
        lv_obj_set_style_bg_opa(s_demo, LV_OPA_COVER, 0);
        lv_obj_remove_flag(s_demo, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_clean(s_demo);
    if (page <= 0) {
        lv_obj_add_flag(s_demo, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_demo, LV_OBJ_FLAG_HIDDEN);
    static const ui_orbit_item_t menu[11] = {
        {ICON_ACTIVITY, 0, false}, {ICON_IDLE, 0, false}, {ICON_NOTIFY, 0, false}, {ICON_REPLY, 0, false},
        {ICON_COMPUTER, 0, false}, {ICON_STOP, RGB_ERROR, false}, {ICON_GRAPH, 0, false}, {ICON_READ, 0, true},
        {ICON_MIC, 0, false}, {ICON_KEYBOARD, 0, false}, {ICON_X, 0, false}};
    switch (page) {
    case 1: { /* sigils: sizes and names; the folder glyph */
        static const char *const names[] = {"webapp", "api", "tools", "m5-agent-link", "docs", "infra"};
        for (int i = 0; i < 6; i++) ui_sigil_set_name(ui_sigil_create(s_demo, 93 + i % 3 * 140, 120 + i / 3 * 80, 26), names[i]);
        ui_sigil_set_name(ui_sigil_create(s_demo, 150, 310, 52), "webapp");
        lv_obj_t *big = ui_sigil_create(s_demo, 290, 310, 52);
        ui_sigil_set_name(big, "api");
        ui_sigil_set_color(big, lv_color_hex(RGB_PERMISSION));
        ui_sigil_set_name(ui_sigil_create(s_demo, 233, 400, 22), "");
        break;
    }
    case 2: { /* the big mark */
        lv_obj_t *s = ui_sigil_create(s_demo, UI_CX, UI_CX, UI_SIGIL_R_MAX);
        ui_sigil_set_name(s, "webapp");
        ui_sigil_set_color(s, lv_color_hex(RGB_WORKING));
        break;
    }
    case 3: { /* discs: primary with its hold ring, secondary, small, disabled; the key hints */
        lv_obj_t *p = ui_disc_create(s_demo, 300, 232, UI_DISC_R_PRIMARY);
        ui_disc_set_glyph(p, ICON_CHECK);
        ui_disc_set_rgb(p, RGB_PERMISSION);
        ui_disc_set_hold(p, 690);
        lv_obj_t *x = ui_disc_create(s_demo, 140, 232, UI_DISC_R_SECONDARY);
        ui_disc_set_glyph(x, ICON_X);
        lv_obj_t *pc = ui_disc_create(s_demo, 233, 372, UI_DISC_R_SMALL);
        ui_disc_set_glyph(pc, ICON_COMPUTER);
        lv_obj_t *off = ui_disc_create(s_demo, 233, 80, UI_DISC_R_SMALL);
        ui_disc_set_glyph(off, ICON_MIC);
        ui_disc_set_state(off, UI_DISC_DISABLED);
        ui_key_hint_set(ui_key_hint_create(s_demo, UI_KEY_A), ICON_COMPUTER);
        lv_obj_t *b = ui_key_hint_create(s_demo, UI_KEY_B);
        ui_key_hint_set(b, ICON_X);
        ui_disc_set_hold(b, 400);
        break;
    }
    case 4:
    case 5:
    case 6: { /* orbits: 8 round a sigil; 3; 11 on two pages with the key hints */
        lv_obj_t *o = ui_orbit_create(s_demo);
        if (page == 6) ui_orbit_set_geometry(o, UI_ORBIT_R, 22);
        ui_orbit_set_items(o, menu, page == 4 ? 8 : page == 5 ? 3 : 11);
        if (page == 4) ui_orbit_set_hold(o, 500);
        if (page == 6) {
            ui_orbit_select(o, 9);
            ui_key_hint_set(ui_key_hint_create(s_demo, UI_KEY_A), ICON_CHECK);
            ui_key_hint_set(ui_key_hint_create(s_demo, UI_KEY_B), ICON_NEXT);
        }
        ui_sigil_set_name(ui_sigil_create(s_demo, UI_CX, UI_CX - 6, 52), "webapp");
        demo_name(s_demo, "webapp", UI_CX + 70);
        break;
    }
    case 7: { /* time: 14 minutes, 2 h 25 min, tallies; a progress arc and a countdown; page pips */
        lv_obj_t *g = ui_glyph_create(s_demo, FONT_ICON_HERO, 233, 170);
        ui_glyph_set(g, ICON_WORKING, lv_color_hex(RGB_WORKING));
        ui_dots_set(ui_dots_create(s_demo, 233, 170, 90), 14, lv_color_hex(RGB_WORKING));
        ui_dots_set(ui_dots_create(s_demo, 120, 330, 44), 145, lv_color_hex(RGB_DONE));
        ui_tally_set(ui_tally_create(s_demo, 233, 290), 34, COL_TEXT);
        ui_tally_set(ui_tally_create(s_demo, 233, 316), 85, lv_color_hex(RGB_AMBER));
        ui_tally_set(ui_tally_create(s_demo, 233, 342), 599, COL_TEXT2);
        lv_obj_t *a = ui_arc_create(s_demo, 340, 330, 40, 6);
        ui_arc_set(a, 620, lv_color_hex(RGB_WORKING), COL_FILL2);
        lv_obj_t *c = ui_arc_create(s_demo, 340, 330, 26, 4);
        ui_arc_set_span(c, -120, 240);
        ui_arc_set(c, 300, COL_TEXT, COL_FILL2);
        ui_pips_set(ui_pips_create(s_demo, 233, 392), 5, 1);
        ui_pips_set(ui_pips_create(s_demo, 233, 412), 9, 8);
        break;
    }
    case 8: /* a burst */
        lv_obj_add_flag(s_demo, LV_OBJ_FLAG_HIDDEN);
        ui_burst("Approved", ICON_CHECK, RGB_DONE);
        break;
    case 9: { /* signs: one glyph, two, and one that carries content */
        ui_sign_set(ui_sign_create(s_demo, 233, 130), ICON_OFFLINE, NULL, RGB_AMBER);
        ui_sign_set(ui_sign_create(s_demo, 233, 210), ICON_COMPUTER ICON_REFUSED, NULL, RGB_AMBER);
        ui_sign_set(ui_sign_create(s_demo, 233, 290), ICON_SENT, "\"yes, go ahead\"", RGB_DONE);
        ui_sign_set(ui_sign_create(s_demo, 233, 360), ICON_ERROR, "17", RGB_ERROR);
        break;
    }
    default: break;
    }
}
#endif
