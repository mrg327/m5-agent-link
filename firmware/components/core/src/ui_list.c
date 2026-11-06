/* Session list: a vertical "scaling list" for the round panel. The selected row sits at the
 * centre; neighbours are dimmer and narrower, following the circle's chord. Scrolling animates one
 * position value and re-places five pooled rows, so only the rows themselves are redrawn. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_internal.h"
#include "ui_theme.h"

#define ROW_H 88
#define ROW_VISIBLE_DY 175 /* rows whose centre is farther from the middle are hidden */
#define POOL 6
#define SCROLL_MS 220

typedef struct {
    lv_obj_t *root, *bar, *glyph, *text, *sub;
    uint32_t sid, rev;
    bool bound, emphasised, shown;
} row_t;

static row_t s_rows[POOL];
static lv_obj_t *s_empty;
static uint8_t s_sorted[MODEL_MAX_SESSIONS];
static int s_n, s_sel;
static int32_t s_pos16; /* scroll position in 1/16 rows; row j is centred when s_pos16 == j * 16 */

static int floor_div(int a, int b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
static int pool_slot(int j) { return ((j % POOL) + POOL) % POOL; }

static void fill_row(row_t *r, const session_t *s)
{
    char label[80], sub[64], age[16] = "";
    theme_session_label(label, sizeof label, s, true);
    uint32_t now = theme_now_unix();
    if (now && s->tst && now >= s->tst) theme_elapsed(age, sizeof age, now - s->tst);
    snprintf(sub, sizeof sub, "%s%s%s", theme_state_label(s), age[0] ? theme_sep() : "", age);
    lv_label_set_text(r->text, label);
    lv_label_set_text(r->sub, sub);
    lv_label_set_text(r->glyph, theme_agent_symbol(s->agent));
    lv_color_t c = lv_color_hex(theme_state_rgb(s->state));
    lv_obj_set_style_bg_color(r->bar, c, 0);
    lv_obj_set_style_text_color(r->glyph, c, 0);
    r->sid = s->sid;
    r->rev = model_rev();
    r->bound = true;
}

static void style_row(row_t *r, bool emphasised)
{
    if (r->emphasised == emphasised && r->shown) return;
    r->emphasised = emphasised;
    lv_obj_set_style_bg_color(r->root, emphasised ? COL_SURFACE2 : COL_SURFACE, 0);
    lv_obj_set_style_text_color(r->text, emphasised ? COL_TEXT : COL_MUTED, 0);
}

static void layout(void)
{
    bool want[POOL] = {false};
    int base = floor_div(s_pos16, 16);
    for (int j = base - 2; j <= base + 3; j++) {
        if (j < 0 || j >= s_n) continue;
        row_t *r = &s_rows[pool_slot(j)];
        const session_t *s = model_at(s_sorted[j]);
        int dy = (j * 16 - s_pos16) * ROW_H / 16;
        if (dy > ROW_VISIBLE_DY || dy < -ROW_VISIBLE_DY) continue;
        if (!r->bound || r->sid != s->sid || r->rev != model_rev()) fill_row(r, s);
        int edge = UI_CY + (dy < 0 ? dy - ROW_H / 2 : dy + ROW_H / 2);
        int w = theme_chord(edge, 4);
        if (w > 400) w = 400;
        if (w < 140) w = 140;
        lv_obj_set_size(r->root, w, ROW_H - 8);
        lv_obj_set_pos(r->root, UI_CX - w / 2, UI_CY + dy - (ROW_H - 8) / 2);
        lv_obj_set_width(r->text, w - 84);
        lv_obj_set_width(r->sub, w - 84);
        style_row(r, dy > -ROW_H / 2 && dy < ROW_H / 2);
        r->shown = true;
        want[pool_slot(j)] = true;
        lv_obj_remove_flag(r->root, LV_OBJ_FLAG_HIDDEN);
    }
    for (int k = 0; k < POOL; k++) {
        if (want[k]) continue;
        s_rows[k].shown = false;
        lv_obj_add_flag(s_rows[k].root, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_n == 0) lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
}

static void scroll_cb(void *unused, int32_t v)
{
    (void)unused;
    s_pos16 = v;
    layout();
}

static void select_index(int idx, bool animate)
{
    if (idx < 0 || idx >= s_n) return;
    if (idx != s_sel) {
        s_sel = idx;
        ui_select(model_at(s_sorted[idx])->sid);
        dbg_event("ui", "select", "\"sid\":%u,\"index\":%d", (unsigned)model_at(s_sorted[idx])->sid, idx);
        if (g_ui_actions->tick) g_ui_actions->tick();
    }
    lv_anim_delete(NULL, scroll_cb);
    if (animate && ui_animations_allowed()) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_exec_cb(&a, scroll_cb);
        lv_anim_set_values(&a, s_pos16, idx * 16);
        lv_anim_set_duration(&a, SCROLL_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    } else {
        s_pos16 = idx * 16;
        layout();
    }
}

static void refresh(void)
{
    s_n = (int)model_sorted(s_sorted);
    uint32_t sid;
    int idx = 0;
    if (ui_selected(&sid)) {
        for (int i = 0; i < s_n; i++)
            if (model_at(s_sorted[i])->sid == sid) idx = i;
    }
    if (s_n && idx >= s_n) idx = s_n - 1;
    s_sel = idx;
    if (s_n) ui_select(model_at(s_sorted[idx])->sid);
    lv_anim_delete(NULL, scroll_cb);
    s_pos16 = idx * 16;
    layout();
}

static void on_show(void) { refresh(); }
static void tick(uint32_t now_ms) { (void)now_ms; }

static int row_at(int y)
{
    return floor_div(s_pos16 + (y - UI_CY) * 16 / ROW_H + 8, 16);
}

static bool gesture(const gesture_evt_t *g)
{
    switch (g->kind) {
    case G_SWIPE_UP: select_index(s_sel + 1, true); return true;
    case G_SWIPE_DOWN:
        if (s_sel > 0) select_index(s_sel - 1, true);
        else ui_goto(SCR_GLANCE, NAV_DOWN);
        return true;
    case G_TAP: {
        int j = row_at(g->y);
        if (j < 0 || j >= s_n) return true;
        if (j != s_sel) {
            select_index(j, true);
        } else {
            ui_select(model_at(s_sorted[j])->sid);
            ui_goto(SCR_DETAIL, NAV_FORWARD);
        }
        return true;
    }
    case G_DOUBLE_TAP: {
        int j = row_at(g->y);
        if (j >= 0 && j < s_n) g_ui_actions->focus(model_at(s_sorted[j])->sid);
        return true;
    }
    case G_BTN_A:
        if (s_n) {
            ui_select(model_at(s_sorted[s_sel])->sid);
            ui_goto(SCR_DETAIL, NAV_FORWARD);
        }
        return true;
    case G_SWIPE_LEFT:
    case G_BTN_B: ui_goto(SCR_GLANCE, NAV_BACK); return true;
    default: return false;
    }
}

static void report(ui_report_t *r)
{
    if (s_n == 0) {
        ui_report_add(r, "No sessions");
        return;
    }
    for (int j = 0; j < s_n; j++) {
        int dy = (j * 16 - s_pos16) * ROW_H / 16;
        if (dy > ROW_VISIBLE_DY || dy < -ROW_VISIBLE_DY) continue;
        char label[80];
        theme_session_label(label, sizeof label, model_at(s_sorted[j]), false);
        ui_report_add(r, "%s", label);
    }
}

ui_screen_t g_list = {.on_show = on_show, .refresh = refresh, .tick = tick, .gesture = gesture, .report = report};

void ui_list_init(void)
{
    lv_obj_t *scr = g_list.root = ui_new_screen();
    for (int k = 0; k < POOL; k++) {
        row_t *r = &s_rows[k];
        r->root = lv_obj_create(scr);
        lv_obj_remove_style_all(r->root);
        lv_obj_set_style_bg_opa(r->root, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(r->root, 22, 0);
        lv_obj_remove_flag(r->root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        r->bar = lv_obj_create(r->root);
        lv_obj_remove_style_all(r->bar);
        lv_obj_set_size(r->bar, 6, 44);
        lv_obj_set_style_radius(r->bar, 3, 0);
        lv_obj_set_style_bg_opa(r->bar, LV_OPA_COVER, 0);
        lv_obj_align(r->bar, LV_ALIGN_LEFT_MID, 12, 0);
        r->glyph = theme_label(r->root, 24, COL_TEXT);
        lv_obj_align(r->glyph, LV_ALIGN_LEFT_MID, 30, 0);
        r->text = theme_label(r->root, 20, COL_TEXT);
        lv_obj_align(r->text, LV_ALIGN_TOP_LEFT, 66, 12);
        r->sub = theme_label(r->root, 16, COL_MUTED);
        lv_obj_align(r->sub, LV_ALIGN_TOP_LEFT, 66, 44);
        lv_obj_add_flag(r->root, LV_OBJ_FLAG_HIDDEN);
    }
    s_empty = theme_label(scr, 24, COL_MUTED);
    lv_label_set_text(s_empty, "No sessions");
    lv_obj_set_width(s_empty, 260);
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_empty, LV_ALIGN_CENTER, 0, 0);
}
