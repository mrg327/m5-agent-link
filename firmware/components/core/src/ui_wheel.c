#include "ui_wheel.h"

#include <stdio.h>
#include <string.h>

#include "ui_theme.h"

/* Three rows: the selected one in the middle, a neighbour either side. The list wraps, so a short list
 * never shows an item twice. The selected row never moves, so its capsule is one static object. */
#define ROWS 3
#define MID 1
#define LIST_PITCH 62
#define BUTTON_PITCH 48
#define LIST_W 330
#define LIST_H 56
#define LIST_ICON_X 22 /* icon column, from the capsule's left edge */
#define LIST_TEXT_X 64
#define BUTTON_W 280
#define BUTTON_H 48
#define HIT_PAD 10 /* the touch target is a little wider than the drawing */
#define RING_D 20  /* the hold ring */
#define RING_PAD 18
#define PROG_STEP 8 /* permille: about 2.6 px of capsule per repaint */

typedef struct {
    wheel_item_t items[WHEEL_MAX_ITEMS];
    char labels[WHEEL_MAX_ITEMS][28];
    char icons[WHEEL_MAX_ITEMS][8];
    char shown[ROWS][28]; /* what each row label holds: rows are only rewritten when it changes */
    char shown_icon[ROWS][8];
    int n, sel, prog;
    wheel_style_t style;
    int cy, pitch;
    lv_obj_t *cap, *ring, *row[ROWS], *ico[ROWS];
} wheel_t;

static wheel_t *data(const lv_obj_t *w) { return lv_obj_get_user_data((lv_obj_t *)w); }

static int cap_w(const wheel_t *d) { return d->style == WHEEL_STYLE_LIST ? LIST_W : BUTTON_W; }
static int cap_h(const wheel_t *d) { return d->style == WHEEL_STYLE_LIST ? LIST_H : BUTTON_H; }

/* Whether row r shows an item: +k and -k are different items unless n == 2k, which shows on the next side only. */
static bool row_shown(const wheel_t *d, int r)
{
    int off = r - MID, k = off < 0 ? -off : off;
    return d->n > 0 && (off == 0 || d->n > 2 * k || (off > 0 && d->n == 2 * k));
}

static const wheel_item_t *row_item(const wheel_t *d, int r)
{
    return &d->items[((d->sel + r - MID) % d->n + d->n) % d->n];
}

/* The lighter fill that grows across the capsule while a hold is in progress. */
static lv_color_t fill_of(const wheel_t *d)
{
    if (d->n == 0) return COL_FILL2;
    const wheel_item_t *s = &d->items[d->sel];
    return d->style == WHEEL_STYLE_BUTTON && s->rgb ? lv_color_hex(s->rgb) : COL_FILL2;
}

static void cap_draw(lv_event_t *e)
{
    wheel_t *d = lv_event_get_user_data(e);
    if (d->prog <= 0) return;
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(d->cap, &a);
    int x2 = a.x1 + (lv_area_get_width(&a) * d->prog + 999) / 1000 - 1;
    lv_area_t saved = l->_clip_area; /* draw the whole capsule shape, but only the part the hold has reached */
    if (x2 >= saved.x1 && a.x1 <= saved.x2) {
        l->_clip_area.x2 = x2 < saved.x2 ? x2 : saved.x2;
        lv_draw_rect_dsc_t r;
        lv_draw_rect_dsc_init(&r);
        r.bg_color = lv_color_mix(lv_color_white(), fill_of(d), d->style == WHEEL_STYLE_BUTTON ? 45 : 22);
        r.bg_opa = LV_OPA_COVER;
        r.radius = LV_RADIUS_CIRCLE;
        lv_draw_rect(l, &r, &a);
        l->_clip_area = saved;
    }
}

static void set_row(wheel_t *d, int r, const char *text, const char *icon)
{
    if (strcmp(d->shown[r], text)) {
        snprintf(d->shown[r], sizeof d->shown[r], "%s", text);
        lv_label_set_text(d->row[r], text);
    }
    if (strcmp(d->shown_icon[r], icon)) {
        snprintf(d->shown_icon[r], sizeof d->shown_icon[r], "%s", icon);
        lv_label_set_text(d->ico[r], icon);
    }
}

static void render(lv_obj_t *w)
{
    wheel_t *d = data(w);
    bool list = d->style == WHEEL_STYLE_LIST;
    for (int r = 0; r < ROWS; r++) {
        const wheel_item_t *it = row_shown(d, r) ? row_item(d, r) : NULL;
        set_row(d, r, it ? it->label : "", it && it->icon ? it->icon : "");
    }
    const wheel_item_t *s = d->n > 0 ? &d->items[d->sel] : NULL;
    if (!s) {
        lv_obj_add_flag(d->cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(d->ring, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(d->cap, LV_OBJ_FLAG_HIDDEN);
    bool filled = !list && s->rgb;
    lv_color_t fg = list ? (s->hold ? lv_color_hex(s->rgb) : COL_TEXT) : (filled ? COL_ON_ACCENT : COL_TEXT);
    theme_set_bg_color(d->cap, fill_of(d));
    theme_set_text_color(d->row[MID], fg);
    theme_set_text_color(d->ico[MID], fg);
    /* the label ends before the hold ring, so a long label cannot run under it */
    int ring = s->hold ? RING_D + RING_PAD + 4 : 0;
    int lw = list ? LIST_W - LIST_TEXT_X - 14 - ring : BUTTON_W - 2 * (ring > 24 ? ring : 24); /* BUTTON text stays centred */
    if (lv_obj_get_width(d->row[MID]) != lw) lv_obj_set_width(d->row[MID], lw);
    if (s->hold) {
        lv_obj_remove_flag(d->ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(d->ring, fg, 0);
    } else {
        lv_obj_add_flag(d->ring, LV_OBJ_FLAG_HIDDEN);
    }
}


lv_obj_t *wheel_create_at(lv_obj_t *parent, wheel_layout_t lay)
{
    lv_obj_t *w = lv_obj_create(parent);
    lv_obj_remove_style_all(w);
    lv_obj_set_size(w, 466, 466);
    lv_obj_set_pos(w, 0, 0);
    lv_obj_remove_flag(w, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    wheel_t *d = lv_malloc_zeroed(sizeof *d); /* lives as long as the screen: never freed */
    lv_obj_set_user_data(w, d);
    d->style = lay.style;
    d->cy = lay.cy;
    d->pitch = lay.dy1 ? lay.dy1 : lay.style == WHEEL_STYLE_LIST ? LIST_PITCH : BUTTON_PITCH;
    bool list = d->style == WHEEL_STYLE_LIST;

    d->cap = lv_obj_create(w); /* first child: the labels draw over it */
    lv_obj_remove_style_all(d->cap);
    lv_obj_set_size(d->cap, cap_w(d), cap_h(d));
    lv_obj_set_pos(d->cap, 233 - cap_w(d) / 2, d->cy - cap_h(d) / 2);
    lv_obj_set_style_radius(d->cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d->cap, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d->cap, COL_FILL2, 0);
    lv_obj_remove_flag(d->cap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(d->cap, cap_draw, LV_EVENT_DRAW_MAIN_END, d);
    lv_obj_add_flag(d->cap, LV_OBJ_FLAG_HIDDEN);

    int left = 233 - cap_w(d) / 2;
    for (int r = 0; r < ROWS; r++) {
        int y = d->cy + (r - MID) * d->pitch;
        bool sel = r == MID;
        const lv_font_t *f = sel ? FONT_HEAD : FONT_BODY;
        lv_color_t c = sel ? COL_TEXT : COL_TEXT2;
        int h = lv_font_get_line_height(f);
        lv_obj_t *l = d->row[r] = lv_label_create(w);
        lv_obj_set_style_text_font(l, f, 0);
        lv_obj_set_style_text_color(l, c, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(l, "");
        if (list) {
            lv_obj_set_size(l, LIST_W - LIST_TEXT_X - 14, h);
            lv_obj_set_pos(l, left + LIST_TEXT_X, y - h / 2);
        } else {
            lv_obj_set_size(l, BUTTON_W - 80, h);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(l, 233 - (BUTTON_W - 80) / 2, y - h / 2);
        }
        lv_obj_t *ic = d->ico[r] = lv_label_create(w);
        lv_obj_set_style_text_font(ic, FONT_ICON_M, 0);
        lv_obj_set_style_text_color(ic, sel ? COL_TEXT : COL_TEXT3, 0);
        lv_label_set_text(ic, "");
        int ih = lv_font_get_line_height(FONT_ICON_M);
        lv_obj_set_pos(ic, left + (list ? LIST_ICON_X : 16), y - ih / 2); /* a BUTTON item may carry an icon too */
        lv_obj_set_width(ic, 36);
    }
    d->ring = lv_obj_create(w); /* the hold ring: a 20 px outline at the capsule's right end */
    lv_obj_remove_style_all(d->ring);
    lv_obj_set_size(d->ring, RING_D, RING_D);
    lv_obj_set_pos(d->ring, 233 + cap_w(d) / 2 - RING_PAD - RING_D, d->cy - RING_D / 2);
    lv_obj_set_style_radius(d->ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(d->ring, 2, 0);
    lv_obj_set_style_border_color(d->ring, COL_TEXT, 0);
    lv_obj_remove_flag(d->ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(d->ring, LV_OBJ_FLAG_HIDDEN);
    return w;
}

void wheel_set(lv_obj_t *w, const wheel_item_t *items, int n, int sel)
{
    wheel_t *d = data(w);
    if (n > WHEEL_MAX_ITEMS) n = WHEEL_MAX_ITEMS;
    d->n = n;
    for (int i = 0; i < n; i++) {
        d->items[i] = items[i];
        snprintf(d->labels[i], sizeof d->labels[i], "%s", items[i].label);
        snprintf(d->icons[i], sizeof d->icons[i], "%s", items[i].icon ? items[i].icon : "");
        d->items[i].label = d->labels[i];
        d->items[i].icon = d->icons[i][0] ? d->icons[i] : NULL;
    }
    d->sel = n ? ((sel % n) + n) % n : 0;
    wheel_set_progress(w, 0);
    render(w);
}

void wheel_select(lv_obj_t *w, int sel)
{
    wheel_t *d = data(w);
    if (!d->n) return;
    d->sel = ((sel % d->n) + d->n) % d->n;
    wheel_set_progress(w, 0);
    render(w);
}

void wheel_set_shown(lv_obj_t *w, bool shown)
{
    if (shown) lv_obj_remove_flag(w, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);
}

int wheel_sel(const lv_obj_t *w) { return data(w)->sel; }
int wheel_count(const lv_obj_t *w) { return data(w)->n; }
const wheel_item_t *wheel_item(const lv_obj_t *w, int i)
{
    const wheel_t *d = data(w);
    return i >= 0 && i < d->n ? &d->items[i] : NULL;
}

int wheel_hit(const lv_obj_t *w, int x, int y)
{
    const wheel_t *d = data(w);
    if (lv_obj_has_flag(w, LV_OBJ_FLAG_HIDDEN) || x < 233 - cap_w(d) / 2 - HIT_PAD || x > 233 + cap_w(d) / 2 + HIT_PAD)
        return -1;
    for (int r = 0; r < ROWS; r++) {
        int cy = d->cy + (r - MID) * d->pitch;
        if (row_shown(d, r) && y >= cy - d->pitch / 2 && y < cy + (d->pitch + 1) / 2)
            return ((d->sel + r - MID) % d->n + d->n) % d->n;
    }
    return -1;
}

void wheel_set_progress(lv_obj_t *w, int permille)
{
    wheel_t *d = data(w);
    permille = permille < 0 ? 0 : permille > 1000 ? 1000 : permille / PROG_STEP * PROG_STEP;
    if (permille == d->prog) return;
    int lo = d->prog < permille ? d->prog : permille, hi = d->prog < permille ? permille : d->prog;
    d->prog = permille;
    lv_area_t a;
    lv_obj_get_coords(d->cap, &a);
    int cw = lv_area_get_width(&a);
    a.x2 = a.x1 + cw * hi / 1000 + 2; /* only the strip the fill moved across */
    a.x1 += cw * lo / 1000 - 2;
    lv_obj_invalidate_area(d->cap, &a);
}
