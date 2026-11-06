/* Palette, fonts and small formatting helpers (DESIGN §8: Primer-dark inspired, original art). */
#pragma once
#include "lvgl.h"
#include "model.h"

#define COL_BG lv_color_hex(0x000000) /* AMOLED black: pixels off */
#define COL_SURFACE lv_color_hex(0x0d1117)
#define COL_SURFACE2 lv_color_hex(0x161b22)
#define COL_BORDER lv_color_hex(0x30363d)
#define COL_TEXT lv_color_hex(0xe6edf3)
#define COL_MUTED lv_color_hex(0x8b949e)
#define RGB_AMBER 0xd29922
#define RGB_DANGER 0xf85149
#define RGB_SUCCESS 0x3fb950
#define RGB_ACCENT 0x58a6ff
#define RGB_IDLE 0x6e7681
#define RGB_CLOUD 0xa371f7

uint32_t theme_state_rgb(sess_state_t s);
/* Words as well as colour, so state never depends on colour alone. */
const char *theme_state_label(const session_t *s);
const char *theme_agent_symbol(agent_t a); /* generic glyphs, no logos */
const char *theme_agent_name(agent_t a);
/* Nearest enabled Montserrat size; falls back to the default font. */
const lv_font_t *theme_font(int px);
lv_obj_t *theme_label(lv_obj_t *parent, int px, lv_color_t color);
/* Width of the visible circle at screen row y, minus a margin on each side. */
int theme_chord(int y, int margin);
/* Separator the font can draw (" · " when available). */
const char *theme_sep(void);
/* "repo · branch"; `display` picks a separator the font can draw, otherwise the canonical " · ". */
void theme_session_label(char *dst, size_t cap, const session_t *s, bool display);
void theme_elapsed(char *dst, size_t cap, uint32_t secs);
uint32_t theme_now_unix(void);
