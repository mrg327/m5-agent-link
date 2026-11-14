/* The Updating overlay: full screen over everything while a firmware update runs. A progress ring in the
 * rim band (grey track, state-blue arc from 12 o'clock), the title, the version, the percentage and the
 * one control: hold B to cancel. Drawn with the arc primitive and labels only, no widgets. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_ota.h"
#include "ui_theme.h"

#define OTA_R 226 /* outer edge of the progress ring; the rim band is r 218-233 */
#define OTA_W 10
#define Y_TITLE 112
#define Y_VER 160
#define Y_PCT 214
#define Y_NOTE 300

static lv_obj_t *s_root, *s_arc, *s_title, *s_ver, *s_pct, *s_note, *s_hint;
static unsigned s_pct_v, s_pct_drawn;
static bool s_waiting, s_restarting, s_redraw_all;

static void on_draw(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = UI_CX;
    d.center.y = UI_CX;
    d.radius = OTA_R;
    d.width = OTA_W;
    d.color = COL_FILL2;
    d.start_angle = 0;
    d.end_angle = 359;
    lv_draw_arc(l, &d);
    unsigned p = s_restarting ? 100 : s_pct_v;
    if (p == 0) return;
    d.color = lv_color_hex(s_restarting ? RGB_SUCCESS : RGB_ACCENT);
    d.rounded = 1;
    d.start_angle = 270;
    d.end_angle = (lv_value_precise_t)((270 + (p >= 100 ? 359 : p * 360 / 100)) % 360);
    if (p >= 100) d.end_angle = 269;
    lv_draw_arc(l, &d);
}

static lv_obj_t *label(const lv_font_t *font, lv_color_t col, int y)
{
    lv_obj_t *o = lv_label_create(s_root);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, col, 0);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(o, 300);
    lv_label_set_long_mode(o, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(o, UI_CX - 150, y);
    return o;
}

void ui_ota_init(lv_obj_t *layer)
{
    s_root = lv_obj_create(layer);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    s_arc = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_arc);
    lv_obj_set_size(s_arc, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_arc, on_draw, LV_EVENT_DRAW_MAIN, NULL);
    s_title = label(FONT_TITLE, COL_TEXT, Y_TITLE);
    s_ver = label(FONT_CAPTION, COL_TEXT2, Y_VER);
    s_pct = label(FONT_NUM, COL_TEXT, Y_PCT);
    s_note = label(FONT_BODY_STRONG, lv_color_hex(RGB_AMBER), Y_NOTE);
    s_hint = label(FONT_CAPTION, COL_TEXT3, UI_Y_LEGEND);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

/* Only the stretch of rim the arc grew into: the overlay is full screen and redrawing all of it from PSRAM
 * on each percent slowed a delta update's flash writes by half (issue 203f). */
static void invalidate_arc(void)
{
    unsigned from = s_pct_drawn, to = s_restarting ? 100 : s_pct_v;
    if (s_redraw_all || to < from) {
        lv_obj_invalidate(s_arc);
    } else if (to > from) {
        int32_t a0 = (int32_t)(270 + from * 360 / 100) % 360, a1 = (int32_t)(270 + to * 360 / 100) % 360;
        lv_area_t a, b;
        if (a1 > a0) {
            lv_draw_arc_get_area(UI_CX, UI_CX, OTA_R, a0, a1, OTA_W, true, &a);
            lv_obj_invalidate_area(s_arc, &a);
        } else { /* through 0 degrees (3 o'clock) or back to the start */
            lv_draw_arc_get_area(UI_CX, UI_CX, OTA_R, a0, 359, OTA_W, true, &a);
            lv_draw_arc_get_area(UI_CX, UI_CX, OTA_R, 0, a1 ? a1 : 1, OTA_W, true, &b);
            lv_obj_invalidate_area(s_arc, &a);
            lv_obj_invalidate_area(s_arc, &b);
        }
    }
    s_pct_drawn = to;
    s_redraw_all = false;
}

static void refresh(void)
{
    char t[8];
    snprintf(t, sizeof t, "%u%%", s_restarting ? 100 : s_pct_v);
    lv_label_set_text(s_title, s_restarting ? "Restarting…" : "Updating");
    lv_label_set_text(s_pct, t);
    lv_label_set_text(s_note, s_waiting && !s_restarting ? "Waiting for the computer…" : "");
    lv_label_set_text(s_hint, s_restarting ? "" : "Hold B to cancel");
    invalidate_arc();
}

void ui_ota_show(const char *ver)
{
    s_pct_v = s_pct_drawn = 0;
    s_waiting = s_restarting = false;
    s_redraw_all = true;
    lv_label_set_text(s_ver, ver ? ver : "");
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    refresh();
}

void ui_ota_progress(unsigned pct, bool waiting)
{
    if (pct > 100) pct = 100;
    if (pct == s_pct_v && waiting == s_waiting) return;
    if (waiting != s_waiting) s_redraw_all = true;
    s_pct_v = pct;
    s_waiting = waiting;
    refresh();
}

void ui_ota_restarting(void)
{
    s_restarting = true;
    s_redraw_all = true; /* the arc turns green */
    refresh();
}

void ui_ota_hide(void) { lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN); }
bool ui_ota_visible(void) { return s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN); }

void ui_ota_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "ota");
    ui_report_add(r, "%s", lv_label_get_text(s_title));
    if (lv_label_get_text(s_ver)[0]) ui_report_add(r, "%s", lv_label_get_text(s_ver));
    ui_report_add(r, "%s", lv_label_get_text(s_pct));
    if (lv_label_get_text(s_note)[0]) ui_report_add(r, "%s", lv_label_get_text(s_note));
    if (lv_label_get_text(s_hint)[0]) ui_report_add(r, "%s", lv_label_get_text(s_hint));
}
