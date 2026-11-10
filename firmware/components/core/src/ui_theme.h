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

/* Round language (UI-V2 §4.1, capsules): key caps are circles, the action pill and the toast are
 * filled with a tint of their state colour instead of a border. */
#define UI_KEYCAP_PX 24
/* Key badges sit beside the physical buttons (clockwise from 12 o'clock): KEYA lower-left at 7:30,
 * KEYB lower-right at 4:30. Legends, not touch targets. */
#define UI_BADGE_A_DEG 225
#define UI_BADGE_B_DEG 135
#define UI_BADGE_R 158
#define UI_PILL_TINT_OPA LV_OPA_20 /* pill background: state colour at this opacity over black */
#define UI_TOAST_TINT_MIX 60       /* toast background: lv_color_mix(state colour, black, this) */

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
/* Change-only setters for the refresh paths: LVGL invalidates an object on every style write, even
 * one that changes nothing, and the deck refreshes on every session delta. */
void theme_set_text_color(lv_obj_t *o, lv_color_t c);
void theme_set_bg_color(lv_obj_t *o, lv_color_t c);
void theme_set_opa(lv_obj_t *o, lv_opa_t opa);
void theme_align(lv_obj_t *o, lv_align_t align, int32_t x, int32_t y);
/* Set a label's text only when it differs from `cache` (cap >= 16). Text that does not fit the cache is
 * remembered by length and hash, so a long text that repeats is still skipped. */
void theme_set_text(lv_obj_t *l, char *cache, size_t cap, const char *t);
/* Width of the visible circle at screen row y, minus a margin on each side. */
int theme_chord(int y, int margin);
/* Separator the font can draw (" · " when available). */
const char *theme_sep(void);
void theme_display_text(char *dst, size_t cap, const char *text); /* sum with its separator made displayable */
/* "repo · branch"; `display` picks a separator the font can draw, otherwise the canonical " · ". */
void theme_session_label(char *dst, size_t cap, const session_t *s, bool display);
/* "HOST · " in front of the muted line of a remote session (host != 0); "" for local ones. */
void theme_host_prefix(char *dst, size_t cap, const session_t *s);
void theme_elapsed(char *dst, size_t cap, uint32_t secs);
/* "42s", "4m 12s", "1h 05m". */
void theme_elapsed_long(char *dst, size_t cap, uint32_t secs);
uint32_t theme_now_unix(void);
/* "[A] WORD" (key_left) or "WORD [B]": a circular key cap and a word, centred at polar (deg, r). */
lv_obj_t *theme_key_badge(lv_obj_t *parent, const char *key, const char *word, bool key_left, int deg, int r,
                          lv_obj_t **key_out, lv_obj_t **word_out);
/* Change a badge's word and re-centre it on the same polar spot (the width changed). */
void theme_key_badge_set_word(lv_obj_t *badge, lv_obj_t *word_lbl, const char *word, int deg, int r);
