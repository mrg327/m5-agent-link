#include "ui_wheel.h"

#include <stdio.h>
#include <string.h>

#include "ui_theme.h"

/* Five rows around the centre: the selected one large and white, its neighbours muted, the outer
 * two dimmer still. The list wraps, so a short list never shows an item twice. */
#define ROWS 5
#define ROW_MID 2
#define CY 233
static const int row_y[ROWS] = {CY - 84, CY - 46, CY, CY + 46, CY + 84};
static const int row_px[ROWS] = {16, 20, 24, 20, 16};
#define ROW_W 290
#define CHEVRON_PAD 14
#define TAG_PAD 10

typedef struct {
    wheel_item_t items[WHEEL_MAX_ITEMS];
    char labels[WHEEL_MAX_ITEMS][28];
    int n, sel;
    lv_obj_t *row[ROWS], *chev_l, *chev_r, *tag, *tag_lbl;
} wheel_t;

static wheel_t *data(const lv_obj_t *w) { return lv_obj_get_user_data((lv_obj_t *)w); }

static void render(lv_obj_t *w)
{
    wheel_t *d = data(w);
    for (int r = 0; r < ROWS; r++) {
        int off = r - ROW_MID, k = off < 0 ? -off : off;
        /* show an offset only when +k and -k are different items; when they would be the same
         * item (n == 2k) show it on the "next" side only */
        bool show = d->n > 0 && (off == 0 || d->n > 2 * k || (off > 0 && d->n == 2 * k));
        if (!show) {
            lv_label_set_text(d->row[r], "");
            continue;
        }
        int i = ((d->sel + off) % d->n + d->n) % d->n;
        lv_label_set_text(d->row[r], d->items[i].label);
    }
    bool any = d->n > 0;
    const wheel_item_t *s = any ? &d->items[d->sel] : NULL;
    lv_color_t col = s ? lv_color_hex(s->rgb) : COL_MUTED;
    lv_obj_set_style_text_color(d->chev_l, col, 0);
    lv_obj_set_style_text_color(d->chev_r, col, 0);
    if (any) {
        lv_point_t sz;
        lv_text_get_size(&sz, s->label, theme_font(row_px[ROW_MID]), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int half = (sz.x < ROW_W ? sz.x : ROW_W) / 2;
        lv_obj_remove_flag(d->chev_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(d->chev_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(d->chev_l, LV_ALIGN_TOP_MID, -half - CHEVRON_PAD - 8, row_y[ROW_MID] + 4);
        lv_obj_align(d->chev_r, LV_ALIGN_TOP_MID, half + CHEVRON_PAD + 8, row_y[ROW_MID] + 4);
        if (s->hold) {
            lv_obj_set_style_bg_color(d->tag, col, 0);
            lv_obj_set_style_text_color(d->tag_lbl, col, 0);
            lv_obj_remove_flag(d->tag, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(d->tag, LV_ALIGN_TOP_MID, 0, row_y[ROW_MID] + 30);
        } else {
            lv_obj_add_flag(d->tag, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(d->chev_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(d->chev_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(d->tag, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *wheel_create(lv_obj_t *parent)
{
    lv_obj_t *w = lv_obj_create(parent);
    lv_obj_remove_style_all(w);
    lv_obj_set_size(w, 466, 466);
    lv_obj_set_pos(w, 0, 0);
    lv_obj_remove_flag(w, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    wheel_t *d = lv_malloc_zeroed(sizeof *d); /* lives as long as the screen: never freed */
    lv_obj_set_user_data(w, d);
    for (int r = 0; r < ROWS; r++) {
        int off = r - ROW_MID;
        lv_color_t c = off == 0 ? COL_TEXT : (off == -1 || off == 1) ? COL_MUTED : lv_color_mix(COL_MUTED, COL_BG, 150);
        lv_obj_t *l = d->row[r] = theme_label(w, row_px[r], c);
        int h = lv_font_get_line_height(theme_font(row_px[r]));
        lv_obj_set_size(l, ROW_W, h);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, 233 - ROW_W / 2, row_y[r] - h / 2);
        lv_label_set_text(l, "");
    }
    d->chev_l = theme_label(w, 16, COL_MUTED);
    lv_label_set_text(d->chev_l, LV_SYMBOL_RIGHT);
    d->chev_r = theme_label(w, 16, COL_MUTED);
    lv_label_set_text(d->chev_r, LV_SYMBOL_LEFT);
    d->tag = lv_obj_create(w);
    lv_obj_remove_style_all(d->tag);
    lv_obj_set_size(d->tag, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(d->tag, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d->tag, UI_PILL_TINT_OPA, 0);
    lv_obj_set_style_pad_hor(d->tag, TAG_PAD, 0);
    lv_obj_set_style_pad_ver(d->tag, 2, 0);
    lv_obj_remove_flag(d->tag, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    d->tag_lbl = theme_label(d->tag, 16, COL_TEXT);
    lv_label_set_text(d->tag_lbl, "HOLD");
    lv_obj_add_flag(d->tag, LV_OBJ_FLAG_HIDDEN);
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
        d->items[i].label = d->labels[i];
    }
    d->sel = n ? ((sel % n) + n) % n : 0;
    render(w);
}

void wheel_select(lv_obj_t *w, int sel)
{
    wheel_t *d = data(w);
    if (!d->n) return;
    d->sel = ((sel % d->n) + d->n) % d->n;
    render(w);
}

int wheel_sel(const lv_obj_t *w) { return data(w)->sel; }
int wheel_count(const lv_obj_t *w) { return data(w)->n; }
const wheel_item_t *wheel_item(const lv_obj_t *w, int i)
{
    const wheel_t *d = data(w);
    return i >= 0 && i < d->n ? &d->items[i] : NULL;
}
