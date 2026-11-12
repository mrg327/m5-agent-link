/* The done summary line on the session card (docs/design/DONE-SUMMARY.md): up to four parts in a row,
 * centred on y 246, FONT_BODY in COL_TEXT2; failed tests alone in the error colour. Redraw discipline
 * (UI-V2 §8a): change-only setters; the row is rebuilt only when its text changes. */
#include <stdio.h>
#include <string.h>

#include "done.h"
#include "ui_done.h"
#include "ui_theme.h"

#define DONE_Y 246 /* centre of the line: where the first row of a step summary was */
#define DONE_H 28

static lv_obj_t *s_row, *s_seg[DONE_SEGS];
static char s_cache[DONE_SEGS][56];
static bool s_shown[DONE_SEGS];

static int measure(const char *text)
{
    lv_point_t sz;
    lv_text_get_size(&sz, text, FONT_BODY, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

void ui_done_create(lv_obj_t *centre)
{
    s_row = lv_obj_create(centre);
    lv_obj_remove_style_all(s_row);
    lv_obj_set_size(s_row, LV_SIZE_CONTENT, DONE_H);
    lv_obj_set_flex_flow(s_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_row, 0, 0);
    lv_obj_remove_flag(s_row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_row, LV_ALIGN_TOP_MID, 0, DONE_Y - DONE_H / 2);
    for (int i = 0; i < DONE_SEGS; i++) {
        s_seg[i] = lv_label_create(s_row);
        lv_obj_set_style_text_font(s_seg[i], FONT_BODY, 0);
        lv_obj_set_style_text_color(s_seg[i], COL_TEXT2, 0);
        lv_label_set_long_mode(s_seg[i], LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_width(s_seg[i], LV_SIZE_CONTENT);
        lv_label_set_text(s_seg[i], "");
        lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_row, LV_OBJ_FLAG_HIDDEN);
}

bool ui_done_update(const session_t *s, bool allowed, bool offline)
{
    bool on = allowed && s && (s->state == SESS_DONE || s->state == SESS_ERROR) && s->agent != AGENT_SHELL && done_known(&s->sum);
    memset(s_shown, 0, sizeof s_shown);
    if (!on) {
        if (!lv_obj_has_flag(s_row, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(s_row, LV_OBJ_FLAG_HIDDEN);
        return false;
    }
    done_line_t l;
    done_line_build(&s->sum, theme_sep(), DONE_MAX_W, measure, &l);
    for (int i = 0; i < DONE_SEGS; i++) {
        bool seg_on = l.on[i];
        s_shown[i] = seg_on;
        if (seg_on) theme_set_text(s_seg[i], s_cache[i], sizeof s_cache[i], l.txt[i]);
        else s_cache[i][0] = 0;
        if (lv_obj_has_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN) == seg_on) {
            if (seg_on) lv_obj_remove_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
        }
        theme_set_text_color(s_seg[i], l.fail[i] && !offline ? lv_color_hex(RGB_ERROR) : COL_TEXT2);
    }
    if (lv_obj_has_flag(s_row, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(s_row, LV_OBJ_FLAG_HIDDEN);
    return true;
}

/* Two entries (the report keeps few and short ones): files and lines, then tests and time. */
void ui_done_report(ui_report_t *r)
{
    char a[96] = "", b[96] = "";
    for (int i = 0; i < DONE_SEGS; i++) {
        if (!s_shown[i]) continue;
        char *dst = i < DONE_TESTS ? a : b;
        size_t n = strlen(dst);
        snprintf(dst + n, sizeof a - n, "%.52s", s_cache[i]);
    }
    if (a[0]) ui_report_add(r, "%s", a);
    if (b[0]) ui_report_add(r, "%s", b);
}
