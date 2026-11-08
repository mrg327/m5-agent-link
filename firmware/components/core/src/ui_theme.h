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
/* State colours: Okabe-Ito, colour-blind safe (docs/design/UI-RING.md §4). The old Primer names keep their roles. */
#define RGB_WORKING 0x56b4e9    /* sky blue */
#define RGB_ASK 0xf0e442        /* yellow: needs input */
#define RGB_PERMISSION 0xe69f00 /* orange */
#define RGB_DONE 0x009e73       /* bluish green */
#define RGB_ERROR 0xd55e00      /* vermillion */
#define RGB_IDLE 0x999999       /* grey */
#define RGB_AMBER RGB_PERMISSION
#define RGB_DANGER RGB_ERROR
#define RGB_SUCCESS RGB_DONE
#define RGB_ACCENT RGB_WORKING

uint32_t theme_state_rgb(sess_state_t s);
/* Colour of a session: like theme_state_rgb, but approval requests (orange) differ from questions (yellow). */
uint32_t theme_session_rgb(const session_t *s);
/* Words as well as colour, so state never depends on colour alone. */
const char *theme_state_label(const session_t *s);
/* Title-case state word for the deck ("Working", "Question", "Approve?", ...). */
const char *theme_state_word(const session_t *s);
/* LV_SYMBOL_* (or "?") drawn beside the state word. */
const char *theme_state_glyph(const session_t *s);
/* Last path component of a working directory or repo name. */
const char *theme_basename(const char *path);
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
/* "42s", "4m 12s", "1h 05m". */
void theme_elapsed_long(char *dst, size_t cap, uint32_t secs);
uint32_t theme_now_unix(void);
