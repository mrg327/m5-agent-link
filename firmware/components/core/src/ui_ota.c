/* The Updating overlay in Sigil (docs/design/GLYPHS.md §5): full screen over everything while a
 * firmware update runs. One hero glyph inside a progress arc, and nothing spelled out:
 *
 *   - updating: the download glyph in the working blue, the arc filling round it from 12 o'clock;
 *   - the link is down (the transfer resumes when the computer is back): the plug in amber, the arc
 *     grey;
 *   - restarting: the working glyph in green inside the closed green ring;
 *   - the one control, hold B to cancel: B's key hint with the cross, its hold ring partly drawn (a
 *     press does nothing, a hold does);
 *   - the version being installed: small digits under the arc (a value that cannot be drawn).
 *
 * The words (Updating, the percentage, Waiting for the computer…, Hold B to cancel, Restarting…)
 * are in the report (rule 9). A percent step repaints only the piece of arc it adds (ui_arc_set):
 * the overlay is full screen, and redrawing all of it from PSRAM on each percent slowed a delta
 * update's flash writes by half (issue 203f). */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_ota.h"
#include "ui_theme.h"

#define HERO_Y UI_CX
#define ARC_R 112 /* centre line: the hero glyph (128 px) sits inside with room to spare */
#define ARC_W 10
#define VER_Y 374   /* under the arc, in the bottom slot */
#define HOLD_PM 700 /* B's hold ring as drawn: a hold in progress */

static lv_obj_t *s_root, *s_arc, *s_hero, *s_ver, *s_cancel;
static unsigned s_pct_v;
static bool s_waiting, s_restarting;
static char s_ver_txt[24];

void ui_ota_init(lv_obj_t *layer)
{
    s_root = lv_obj_create(layer);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    s_arc = ui_arc_create(s_root, UI_CX, HERO_Y, ARC_R, ARC_W);
    s_hero = ui_glyph_create(s_root, FONT_ICON_HERO, UI_CX, HERO_Y);
    s_ver = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_ver, FONT_CAPTION, 0);
    lv_obj_set_style_text_color(s_ver, COL_TEXT3, 0);
    lv_obj_set_style_text_align(s_ver, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_ver, LV_LABEL_LONG_MODE_DOTS);
    int w = 2 * (UI_KEY_B_X - UI_CX - UI_DISC_R_KEY - 12),
        h = lv_font_get_line_height(FONT_CAPTION);
    lv_obj_set_size(s_ver, w, h);
    lv_obj_set_pos(s_ver, UI_CX - w / 2, VER_Y - h / 2);
    lv_label_set_text(s_ver, "");
    s_cancel = ui_key_hint_create(s_root, UI_KEY_B);
    ui_key_hint_set(s_cancel, ICON_X);
    ui_disc_set_hold(s_cancel, HOLD_PM);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

/* Every setter is change-only: a percent step is the new piece of the arc and nothing else. */
static void refresh(void)
{
    uint32_t rgb = s_restarting ? RGB_SUCCESS : RGB_ACCENT;
    bool stalled = s_waiting && !s_restarting;
    ui_arc_set(s_arc, s_restarting ? 1000 : (int)s_pct_v * 10,
               stalled ? COL_TEXT3 : lv_color_hex(rgb), COL_FILL2);
    if (stalled) ui_glyph_set(s_hero, ICON_OFFLINE, lv_color_hex(RGB_AMBER));
    else ui_glyph_set(s_hero, s_restarting ? ICON_WORKING : ICON_UPDATE, lv_color_hex(rgb));
    if (lv_obj_has_flag(s_cancel, LV_OBJ_FLAG_HIDDEN) != s_restarting) {
        /* past the point of cancelling */
        if (s_restarting) lv_obj_add_flag(s_cancel, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(s_cancel, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_ota_show(const char *ver)
{
    s_pct_v = 0;
    s_waiting = s_restarting = false;
    theme_set_text(s_ver, s_ver_txt, sizeof s_ver_txt, ver ? ver : "");
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    refresh();
}

void ui_ota_progress(unsigned pct, bool waiting)
{
    if (pct > 100) pct = 100;
    if (pct == s_pct_v && waiting == s_waiting) return;
    s_pct_v = pct;
    s_waiting = waiting;
    refresh();
}

void ui_ota_restarting(void)
{
    s_restarting = true;
    refresh();
}

void ui_ota_hide(void) { lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN); }
bool ui_ota_visible(void) { return s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN); }

void ui_ota_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "ota");
    ui_report_add(r, "%s", s_restarting ? "Restarting…" : "Updating");
    if (s_ver_txt[0]) ui_report_add(r, "%s", s_ver_txt);
    ui_report_add(r, "%u%%", s_restarting ? 100 : s_pct_v);
    if (s_waiting && !s_restarting) ui_report_add(r, "Waiting for the computer…");
    if (!s_restarting) ui_report_add(r, "Hold B to cancel");
}
