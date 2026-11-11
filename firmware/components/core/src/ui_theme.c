#include "ui_theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
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
    case SESS_INPUT: return s->sub == 1 ? "Has a question" : "Needs approval";
    case SESS_ERROR: return "Error";
    case SESS_DONE: return "Done";
    case SESS_IDLE: return "Idle";
    default: return "Ended";
    }
}

/* Centre glyph per state: a shape beside the word, so state never depends on colour alone. */
const char *theme_state_glyph(const session_t *s)
{
    switch (s->state) {
    case SESS_INPUT: return s->sub == 0 ? ICON_PERMISSION : ICON_QUESTION;
    case SESS_ERROR: return ICON_ERROR;
    case SESS_DONE: return ICON_DONE;
    case SESS_IDLE: return ICON_IDLE;
    default: return ICON_WORKING;
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
    (void)a; /* no per-agent art: the name says which agent it is */
    return ICON_ACTIVITY;
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
    if (px <= 17) return FONT_CAPTION;
    if (px <= 21) return FONT_BODY;
    if (px <= 28) return FONT_HEAD;
    if (px <= 40) return FONT_TITLE;
    return FONT_NUM;
}

lv_color_t theme_dim(uint32_t rgb, int pct)
{
    return lv_color_mix(lv_color_hex(rgb), lv_color_black(), (uint8_t)(255 * pct / 100));
}

lv_obj_t *theme_label(lv_obj_t *parent, int px, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, theme_font(px), 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

static bool local_is(lv_obj_t *o, lv_style_prop_t prop, lv_style_value_t *v)
{
    return lv_obj_get_local_style_prop(o, prop, v, LV_PART_MAIN) == LV_STYLE_RES_FOUND;
}

void theme_set_text_color(lv_obj_t *o, lv_color_t c)
{
    lv_style_value_t v;
    if (local_is(o, LV_STYLE_TEXT_COLOR, &v) && lv_color_eq(v.color, c)) return;
    lv_obj_set_style_text_color(o, c, 0);
}

void theme_set_bg_color(lv_obj_t *o, lv_color_t c)
{
    lv_style_value_t v;
    if (local_is(o, LV_STYLE_BG_COLOR, &v) && lv_color_eq(v.color, c)) return;
    lv_obj_set_style_bg_color(o, c, 0);
}

void theme_set_opa(lv_obj_t *o, lv_opa_t opa)
{
    lv_style_value_t v;
    if (local_is(o, LV_STYLE_OPA, &v) && v.num == opa) return;
    lv_obj_set_style_opa(o, opa, 0);
}

void theme_align(lv_obj_t *o, lv_align_t align, int32_t x, int32_t y)
{
    lv_style_value_t a, vx, vy;
    if (local_is(o, LV_STYLE_ALIGN, &a) && a.num == (int32_t)align && local_is(o, LV_STYLE_X, &vx) && vx.num == x &&
        local_is(o, LV_STYLE_Y, &vy) && vy.num == y)
        return;
    lv_obj_align(o, align, x, y);
}

void theme_set_text(lv_obj_t *l, char *cache, size_t cap, const char *t)
{
    size_t n = strlen(t);
    if (n < cap) {
        if (!strcmp(cache, t)) return;
        memcpy(cache, t, n + 1);
    } else {
        uint32_t h = 2166136261u; /* FNV-1a */
        for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)t[i]) * 16777619u;
        char key[16];
        snprintf(key, sizeof key, "\x01%08x%05x", (unsigned)h, (unsigned)(n & 0xfffff)); /* no text starts with 0x01 */
        if (!strcmp(cache, key)) return;
        memcpy(cache, key, sizeof key);
    }
    lv_label_set_text(l, t);
}

int theme_chord(int y, int margin)
{
    float dy = (float)(y - 233);
    float r2 = 233.0f * 233.0f - dy * dy;
    int w = r2 > 0 ? (int)(2.0f * sqrtf(r2)) - 2 * margin : 0;
    return w > 0 ? w : 0;
}

/* Figtree has U+00B7; the fallbacks keep a font without it (a test build, a new face) readable. */
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

/* The daemon's separator is U+00B7, which the built-in font may lack: show the theme's separator. */
void theme_display_text(char *dst, size_t cap, const char *text)
{
    const char *sep = strstr(text, " \xC2\xB7 ");
    if (sep) snprintf(dst, cap, "%.*s%s%s", (int)(sep - text), text, theme_sep(), sep + 4);
    else snprintf(dst, cap, "%s", text);
}

void theme_session_label(char *dst, size_t cap, const session_t *s, bool display)
{
    const char *sep = display ? theme_sep() : " \xC2\xB7 ";
    if (s->repo[0] && s->branch[0]) snprintf(dst, cap, "%s%s%s", s->repo, sep, s->branch);
    else if (s->repo[0]) snprintf(dst, cap, "%s", s->repo);
    else snprintf(dst, cap, "%s", theme_agent_name(s->agent));
}

void theme_host_prefix(char *dst, size_t cap, const session_t *s)
{
    if (!s->host) dst[0] = 0;
    else snprintf(dst, cap, "%s%s", s->hn[0] ? s->hn : "Remote", theme_sep());
}

void theme_elapsed(char *dst, size_t cap, uint32_t secs)
{
    if (secs < 60) snprintf(dst, cap, "%us", (unsigned)secs);
    else if (secs < 3600) snprintf(dst, cap, "%um", (unsigned)(secs / 60));
    else snprintf(dst, cap, "%uh%02um", (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60));
}

uint32_t theme_now_unix(void) { return app_now_unix(); }

void theme_elapsed_long(char *dst, size_t cap, uint32_t secs)
{
    if (secs < 60) snprintf(dst, cap, "%us", (unsigned)secs);
    else if (secs < 3600) snprintf(dst, cap, "%um %02us", (unsigned)(secs / 60), (unsigned)(secs % 60));
    else snprintf(dst, cap, "%uh %02um", (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60));
}

#define LEGEND_H UI_KEYCAP_PX
#define LEGEND_R 212 /* the farthest corner of a legend: inside the ring (r 218), not on it */

lv_obj_t *theme_key_badge(lv_obj_t *parent, const char *key, const char *word, bool key_left, int deg, int r,
                          lv_obj_t **key_out, lv_obj_t **word_out)
{
    (void)deg;
    (void)r;
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 6, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(b, (void *)(intptr_t)(key_left ? 1 : 0)); /* which side of the row it hugs */
    lv_obj_t *cap = lv_obj_create(b);
    lv_obj_remove_style_all(cap);
    lv_obj_set_size(cap, UI_KEYCAP_PX, UI_KEYCAP_PX);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cap, 2, 0); /* the spec's 1.5 px: LVGL borders are whole pixels */
    lv_obj_set_style_border_color(cap, COL_TEXT3, 0);
    lv_obj_remove_flag(cap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *k = lv_label_create(cap);
    lv_obj_set_style_text_font(k, FONT_CAPTION, 0);
    lv_obj_set_style_text_color(k, COL_TEXT2, 0);
    lv_label_set_text(k, key);
    lv_obj_center(k);
    lv_obj_t *w = theme_label(b, 16, COL_TEXT2);
    lv_label_set_long_mode(w, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(w, word);
    if (!key_left) lv_obj_move_to_index(cap, 1); /* word first, cap on the button side */
    theme_key_badge_set_word(b, w, word, 0, 0);
    if (key_out) *key_out = k;
    if (word_out) *word_out = w;
    return b;
}

void theme_key_badge_set_word(lv_obj_t *b, lv_obj_t *word_lbl, const char *word, int deg, int r)
{
    (void)deg;
    (void)r;
    lv_label_set_text(word_lbl, word);
    lv_obj_update_layout(b);
    int bw = lv_obj_get_width(b), bh = lv_obj_get_height(b);
    int dy = UI_Y_LEGEND + LEGEND_H / 2 - 233; /* the lower corner is the one nearest the ring */
    int edge = (int)sqrtf((float)(LEGEND_R * LEGEND_R - dy * dy));
    int x = lv_obj_get_user_data(b) ? 233 - edge : 233 + edge - bw;
    lv_obj_set_pos(b, x, UI_Y_LEGEND - bh / 2);
}
