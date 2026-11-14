/* Palette, fonts and small formatting helpers (DESIGN §8: Primer-dark inspired, original art). */
#pragma once
#include "lvgl.h"
#include "model.h"
#include "ui_icons.h"

/* Fonts (UI-V3 §2.1): Figtree text at five sizes plus a digits-only number font; Phosphor icons are
 * each text font's fallback, so an ICON_* string can sit in any label (ui_icons.h). */
LV_FONT_DECLARE(font_figtree_20_500)
LV_FONT_DECLARE(font_figtree_20_400)
LV_FONT_DECLARE(font_figtree_20_600)
LV_FONT_DECLARE(font_figtree_24_600)
LV_FONT_DECLARE(font_figtree_34_600)
LV_FONT_DECLARE(font_figtree_56_600)
LV_FONT_DECLARE(font_phosphor_20)
LV_FONT_DECLARE(font_phosphor_28)
LV_FONT_DECLARE(font_phosphor_44)
#define FONT_CAPTION (&font_figtree_20_500)     /* context line, legends, footers */
#define FONT_BODY (&font_figtree_20_400)        /* summaries, prompt text, unselected rows */
#define FONT_BODY_STRONG (&font_figtree_20_600) /* toasts, chips, small buttons */
#define FONT_HEAD (&font_figtree_24_600)        /* state word, selected row, card titles, buttons */
#define FONT_TITLE (&font_figtree_34_600)       /* the subject of the screen */
#define FONT_NUM (&font_figtree_56_600)         /* clock, timers, counters: 0-9 : - % . and space only */
#define FONT_ICON_S (&font_phosphor_20)
#define FONT_ICON_M (&font_phosphor_28)
#define FONT_ICON_L (&font_phosphor_44)

/* Colour (UI-V3 §2.3): chrome is white and grey, state colours are the only colour on a screen. */
#define COL_BG lv_color_hex(0x000000) /* AMOLED black: pixels off */
#define COL_TEXT lv_color_hex(0xffffff)
#define COL_TEXT2 lv_color_hex(0xa0a0a8) /* secondary text */
#define COL_TEXT3 lv_color_hex(0x6c6c73) /* legends, footers, disabled */
#define COL_FILL1 lv_color_hex(0x1c1c1e) /* platter: text block, inactive toggle */
#define COL_FILL2 lv_color_hex(0x2c2c2e) /* raised platter: selected row, toast, neutral button */
#define COL_ON_ACCENT lv_color_hex(0x000000) /* text and icons on a filled state colour */
/* Old names, kept for the screens that have not been restyled yet. */
#define COL_MUTED COL_TEXT2
#define COL_SURFACE COL_FILL1
#define COL_SURFACE2 COL_FILL2

/* ui_keys.c (another issue's file) still draws the built-in backspace symbol, which only Montserrat had. */
#undef LV_SYMBOL_BACKSPACE
#define LV_SYMBOL_BACKSPACE ICON_BACKSPACE

/* Geometry (UI-V3 §2.4): the ring owns r 218-233, content stays inside r 205. */
#define UI_CONTENT_R 205
#define UI_Y_CAPTION 72 /* context line, centred */
#define UI_Y_LEGEND 364 /* the legends and the toast share this row */
#define UI_Y_TOAST UI_Y_LEGEND
#define UI_PLATTER_R 18
#define UI_MOTION_MS 160 /* ease-out; see ui_motion_enter() */
#define UI_MOTION_RISE 20
#define UI_TOAST_RISE 10
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

/* Legends are no longer placed on the rim; these only keep old callers compiling (ignored). */
#define UI_BADGE_A_DEG 225
#define UI_BADGE_B_DEG 135
#define UI_BADGE_R 158
#define UI_KEYCAP_PX 22

/* rgb mixed toward black: pct % of its brightness (ring segments at rest are 40). */
lv_color_t theme_dim(uint32_t rgb, int pct);

uint32_t theme_state_rgb(sess_state_t s);
/* GRAPH (GRAPH.md §4): lane 0 is the session's own colour; lanes 1-7 a fixed palette, distinct from it and from
 * the failed red. */
uint32_t theme_lane_rgb(const session_t *s, int lane);
/* Colour of a session: like theme_state_rgb, but approval requests (orange) differ from questions (yellow). */
uint32_t theme_session_rgb(const session_t *s);
/* Title-case state word for the deck ("Working", "Has a question", "Needs approval", ...). */
const char *theme_state_word(const session_t *s);
/* ICON_* drawn beside the state word. */
const char *theme_state_glyph(const session_t *s);
/* Last path component of a working directory or repo name. */
const char *theme_basename(const char *path);
const char *theme_agent_name(agent_t a);
const char *theme_agent_glyph(agent_t a);
/* GLYPHS.md §2: the glyph that stands for a word of the UI's vocabulary ("Open", "Sent", "Offline"), or NULL when
 * the word has none and is drawn as text. theme_is_glyph(): the text starts with an icon. */
const char *theme_glyph(const char *word);
bool theme_is_glyph(const char *text);
bool theme_all_glyphs(const char *text); /* nothing but icons: draw it in an icon font */
/* A job (JOBS.md): a terminal command, not an agent. */
bool theme_is_job(const session_t *s);
/* The time beside a job's state word: the elapsed run time, "about 2m left" under a known estimate,
 * "in 3m 12s" once finished, "exit 101 · 42s" once failed. now = unix seconds. */
void theme_job_time(char *dst, size_t cap, const session_t *s, uint32_t now);
/* A running job's progress against its estimate in permille, or -1 when there is nothing to draw
 * (no estimate, not running, clock unknown). Past the estimate it stays at 1000. */
int theme_job_progress(const session_t *s, uint32_t now);
/* Compatibility shim for screens that still ask for a pixel size: <=17 caption, <=21 body, <=28 head,
 * <=40 title, else the number font (digits only: never use it for words). */
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
/* "HOST · " in front of the muted line of a remote session (host != 0); "" for local ones. */
void theme_host_prefix(char *dst, size_t cap, const session_t *s);
void theme_elapsed(char *dst, size_t cap, uint32_t secs);
/* "42s", "4m 12s", "1h 05m". */
void theme_elapsed_long(char *dst, size_t cap, uint32_t secs);
uint32_t theme_now_unix(void);
/* Legend (UI-V3 §2.5): an outlined 22 px key cap and a sentence-case word, both grey. A (key_left) hugs the
 * left of the y 364 row and B the right, beside the physical buttons, as far out as r 212 allows (the spec's
 * "6 px inside the chord" would put the key cap on the ring, which owns r 218-233). `deg` and `r`
 * are ignored (the old polar position); `key_out`/`word_out` return the two labels. */
lv_obj_t *theme_key_badge(lv_obj_t *parent, const char *key, const char *word, bool key_left, int deg, int r,
                          lv_obj_t **key_out, lv_obj_t **word_out);
/* Change a legend's word; it stays anchored to its side of the row (`deg` and `r` ignored). */
void theme_key_badge_set_word(lv_obj_t *badge, lv_obj_t *word_lbl, const char *word, int deg, int r);
