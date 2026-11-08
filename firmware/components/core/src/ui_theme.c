#include "ui_theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hal.h"

uint32_t theme_state_rgb(sess_state_t s)
{
    switch (s) {
    case SESS_INPUT: return RGB_ASK;
    case SESS_ERROR: return RGB_ERROR;
    case SESS_DONE: return RGB_DONE;
    case SESS_WORK:
    case SESS_START: return RGB_WORKING;
    default: return RGB_IDLE;
    }
}

uint32_t theme_session_rgb(const session_t *s)
{
    return s->state == SESS_INPUT && s->sub == 0 ? RGB_PERMISSION : theme_state_rgb(s->state);
}

const char *theme_state_word(const session_t *s)
{
    switch (s->state) {
    case SESS_START: return "Starting";
    case SESS_WORK: return "Working";
    case SESS_INPUT: return s->sub == 1 ? "Question" : "Approve?";
    case SESS_ERROR: return "Error";
    case SESS_DONE: return "Done";
    case SESS_IDLE: return "Idle";
    default: return "Ended";
    }
}

/* Centre glyph per state (UI-V2 §4): a shape beside the word, so state never depends on colour alone.
 * The permission glyph is a bell until a custom "!" in a circle exists; never a square. */
const char *theme_state_glyph(const session_t *s)
{
    switch (s->state) {
    case SESS_INPUT: return s->sub == 0 ? LV_SYMBOL_BELL : "?";
    case SESS_ERROR: return LV_SYMBOL_WARNING;
    case SESS_DONE: return LV_SYMBOL_OK;
    case SESS_IDLE: return LV_SYMBOL_PAUSE;
    default: return LV_SYMBOL_REFRESH;
    }
}

const char *theme_basename(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if ((*p == '/' || *p == '\\') && p[1]) base = p + 1;
    return base;
}

const char *theme_state_label(const session_t *s)
{
    switch (s->state) {
    case SESS_START: return "starting";
    case SESS_WORK: return "working";
    case SESS_INPUT: return s->sub == 1 ? "question" : "needs approval";
    case SESS_ERROR: return "error";
    case SESS_DONE: return "done";
    case SESS_IDLE: return "idle";
    default: return "ended";
    }
}

const char *theme_agent_symbol(agent_t a)
{
    switch (a) {
    case AGENT_COPILOT: return LV_SYMBOL_KEYBOARD;
    case AGENT_CLAUDE: return LV_SYMBOL_EDIT;
    case AGENT_CLOUD: return LV_SYMBOL_UPLOAD;
    default: return LV_SYMBOL_FILE;
    }
}

const char *theme_agent_name(agent_t a)
{
    switch (a) {
    case AGENT_COPILOT: return "Copilot CLI";
    case AGENT_CLAUDE: return "Claude Code";
    case AGENT_CLOUD: return "Cloud agent";
    default: return "Agent";
    }
}

const lv_font_t *theme_font(int px)
{
    if (px >= 48) {
#if LV_FONT_MONTSERRAT_48
        return &lv_font_montserrat_48;
#endif
    }
    if (px >= 32) {
#if LV_FONT_MONTSERRAT_32
        return &lv_font_montserrat_32;
#endif
    }
    if (px >= 24) {
#if LV_FONT_MONTSERRAT_24
        return &lv_font_montserrat_24;
#endif
    }
    if (px >= 20) {
#if LV_FONT_MONTSERRAT_20
        return &lv_font_montserrat_20;
#endif
    }
#if LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#else
    return LV_FONT_DEFAULT;
#endif
}

lv_obj_t *theme_label(lv_obj_t *parent, int px, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, theme_font(px), 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

int theme_chord(int y, int margin)
{
    float dy = (float)(y - 233);
    float r2 = 233.0f * 233.0f - dy * dy;
    int w = r2 > 0 ? (int)(2.0f * sqrtf(r2)) - 2 * margin : 0;
    return w > 0 ? w : 0;
}

/* The built-in Montserrat fonts cover ASCII plus LV_SYMBOL_*; U+00B7 is usually missing there. */
const char *theme_sep(void)
{
    static const char *sep;
    if (sep) return sep;
    lv_font_glyph_dsc_t dsc;
    if (lv_font_get_glyph_dsc(theme_font(20), &dsc, 0xB7, 0)) sep = " \xC2\xB7 ";
    else if (lv_font_get_glyph_dsc(theme_font(20), &dsc, 0x2022, 0)) sep = " \xE2\x80\xA2 ";
    else sep = " - ";
    return sep;
}

void theme_session_label(char *dst, size_t cap, const session_t *s, bool display)
{
    const char *sep = display ? theme_sep() : " \xC2\xB7 ";
    if (s->repo[0] && s->branch[0]) snprintf(dst, cap, "%s%s%s", s->repo, sep, s->branch);
    else if (s->repo[0]) snprintf(dst, cap, "%s", s->repo);
    else snprintf(dst, cap, "%s", theme_agent_name(s->agent));
}

void theme_elapsed(char *dst, size_t cap, uint32_t secs)
{
    if (secs < 60) snprintf(dst, cap, "%us", (unsigned)secs);
    else if (secs < 3600) snprintf(dst, cap, "%um", (unsigned)(secs / 60));
    else snprintf(dst, cap, "%uh%02um", (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60));
}

uint32_t theme_now_unix(void) { return hal_rtc_get_unix(); }

void theme_elapsed_long(char *dst, size_t cap, uint32_t secs)
{
    if (secs < 60) snprintf(dst, cap, "%us", (unsigned)secs);
    else if (secs < 3600) snprintf(dst, cap, "%um %02us", (unsigned)(secs / 60), (unsigned)(secs % 60));
    else snprintf(dst, cap, "%uh %02um", (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60));
}

lv_obj_t *theme_key_badge(lv_obj_t *parent, const char *key, const char *word, bool key_left, int deg, int r,
                          lv_obj_t **key_out, lv_obj_t **word_out)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 6, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *cap = lv_obj_create(b);
    lv_obj_remove_style_all(cap);
    lv_obj_set_size(cap, UI_KEYCAP_PX, UI_KEYCAP_PX);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cap, COL_SURFACE2, 0);
    lv_obj_set_style_border_width(cap, 1, 0);
    lv_obj_set_style_border_color(cap, COL_BORDER, 0);
    lv_obj_remove_flag(cap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *k = theme_label(cap, 16, COL_TEXT);
    lv_label_set_text(k, key);
    lv_obj_center(k);
    lv_obj_t *w = theme_label(b, 16, key_left ? COL_TEXT : COL_MUTED);
    lv_label_set_text(w, word);
    if (!key_left) lv_obj_move_to_index(cap, 1); /* word first, cap on the button side */
    theme_key_badge_set_word(b, w, word, deg, r);
    if (key_out) *key_out = k;
    if (word_out) *word_out = w;
    return b;
}

void theme_key_badge_set_word(lv_obj_t *b, lv_obj_t *word_lbl, const char *word, int deg, int r)
{
    lv_label_set_text(word_lbl, word);
    float rad = (float)deg * 3.14159265f / 180.0f;
    lv_obj_update_layout(b);
    int bw = lv_obj_get_width(b), bh = lv_obj_get_height(b);
    lv_obj_set_pos(b, 233 + (int)((float)r * sinf(rad)) - bw / 2, 233 - (int)((float)r * cosf(rad)) - bh / 2);
}
