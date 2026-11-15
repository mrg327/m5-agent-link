/* Sigil primitives (docs/design/GLYPHS.md §3): what every screen is assembled from. A screen that needs
 * something new adds it here, not locally.
 *
 * Each is one LVGL object with its own draw callback (the orbit holds its discs; the sign is a row of two labels).
 * Positions are centres, in the parent's coordinates. Every setter is change-only: calling it with the value
 * the object already has costs nothing, and a change invalidates only the pixels it moves (one dot, the piece
 * of an arc between the old and the new end), so they are safe to call from a tick (UI-V2 §8a).
 * The objects live as long as their screen; none takes input: hit tests are functions the screen calls from
 * its own gesture handler. Angles are degrees clockwise from 12 o'clock. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "ui_internal.h"

/* ── glyphs ── */

/* Draws a row of icons (an ICON_* string, or several concatenated: a sign) centred on (cx, cy), each centred on
 * its own ink. For a screen's own draw callback; `cx`, `cy` are screen coordinates. */
void ui_glyph_draw(lv_layer_t *l, const lv_font_t *font, const char *icons, int cx, int cy, lv_color_t c, lv_opa_t opa);
/* A glyph object: one icon (or a sign of several) centred on a point. Use it for the hero glyph of a screen
 * (FONT_ICON_HERO) instead of a label: a label centres the font's line box, this centres the ink. */
lv_obj_t *ui_glyph_create(lv_obj_t *parent, const lv_font_t *font, int cx, int cy);
void ui_glyph_set(lv_obj_t *g, const char *icons, lv_color_t c); /* `icons` must outlive the object (an ICON_* literal); "" draws nothing */
void ui_glyph_set_pos(lv_obj_t *g, int cx, int cy);
/* The largest icon font, not above `px`, that has every glyph of `icons` (the hero sizes hold only a few). */
const lv_font_t *ui_glyph_font(const char *icons, int px);

/* ── §3.1 sigil: a project's own mark ── */

#define UI_SIGIL_R_MIN 22
#define UI_SIGIL_R_MAX 110
#define UI_SIGIL_HASH 10 /* bytes of the name's SHA-256 a sigil is drawn from */

/* Three concentric rings of arcs on an 8-sector grid and a core (a dot or a bar), all from the hash of the name:
 * the same name always gives the same mark (the port of Scr.sigil() in docs/design/glyphs/mock.py). `r` is the
 * outer radius. The stroke is 3 px up to r 24, 4 px up to r 40, r / 10 above. */
lv_obj_t *ui_sigil_create(lv_obj_t *parent, int cx, int cy, int r);
void ui_sigil_set_name(lv_obj_t *s, const char *name); /* "" (no folder): the dashed-folder glyph */
void ui_sigil_set_color(lv_obj_t *s, lv_color_t c);    /* white until set */
void ui_sigil_set_pos(lv_obj_t *s, int cx, int cy);
/* The same mark inside a screen's own draw callback (screen coordinates; w 0 = the stroke for r). */
void ui_sigil_hash(const char *name, uint8_t h[UI_SIGIL_HASH]);
void ui_sigil_paint(lv_layer_t *l, const uint8_t h[UI_SIGIL_HASH], int cx, int cy, int r, int w, lv_color_t c);

/* ── §3.3 time dots ── */

/* Elapsed minutes as dots on a circle of radius `r`: one a minute from 12 o'clock, 6 degrees apart, every tenth
 * larger. Under a minute there are none. From 60 minutes on the circle is full: the minutes of the running hour
 * are bright, the rest dim, and each whole hour is a pip 12 px inside the circle (up to 12). A new minute
 * invalidates one dot. The dots are 5 px (every tenth 9 px): give the circle a radius of 60 or more. */
lv_obj_t *ui_dots_create(lv_obj_t *parent, int cx, int cy, int r);
void ui_dots_set(lv_obj_t *d, uint32_t minutes, lv_color_t c);
/* The same time where there is no room for a circle, as a tally on one row centred on (cx, cy): a short bar for
 * each hour (up to 9), a large dot for each ten minutes, a small dot for each minute. At most 190 px wide. */
lv_obj_t *ui_tally_create(lv_obj_t *parent, int cx, int cy);
void ui_tally_set(lv_obj_t *t, uint32_t minutes, lv_color_t c);
int ui_tally_width(uint32_t minutes); /* px the tally of `minutes` takes: to lay a glyph out beside it */
void ui_tally_set_pos(lv_obj_t *t, int cx, int cy);

/* ── arcs: a quantity as geometry (rule 4) ── */

/* A round-capped arc on the circle of centre-line radius `r` about (cx, cy), `w` px wide: a track from `start`
 * over `span` degrees and a fill over `permille` of it. Progress fills; a countdown drains (set it from 1000
 * down). A value step invalidates only the piece between the old end and the new one. */
lv_obj_t *ui_arc_create(lv_obj_t *parent, int cx, int cy, int r, int w);
void ui_arc_set_span(lv_obj_t *a, int start_deg, int span_deg); /* default 0, 360: a whole circle from 12 o'clock */
void ui_arc_set(lv_obj_t *a, int permille, lv_color_t fill, lv_color_t track); /* a black track draws no track */
/* A level of `n` equal steps (a volume of 5): the arc is drawn as n pieces with gaps between them, and the fill
 * lights whole pieces, the nearest count to `permille`. 0 or 1 (the default): one continuous arc. */
void ui_arc_set_steps(lv_obj_t *a, int n);
/* Moves the arc and gives it another radius: an arc round a disc follows the disc (an orbit's selected disc is
 * larger and a screen may shift its centre). Change-only. */
void ui_arc_set_geometry(lv_obj_t *a, int cx, int cy, int r);

/* ── pips: a position among a few (rule 4) ── */

#define UI_PIPS_MAX 12
#define UI_PIPS_PITCH 14

/* A row of `n` dots centred on (cx, cy), the `current` one larger and white, the rest small and grey: which
 * page of a reader, which page of an orbit. More than UI_PIPS_MAX pages show UI_PIPS_MAX dots and the position
 * in proportion. One page draws nothing. */
lv_obj_t *ui_pips_create(lv_obj_t *parent, int cx, int cy);
void ui_pips_set(lv_obj_t *p, int n, int current);

/* ── §3.4 disc: an action ── */

#define UI_DISC_R_PRIMARY 78   /* the one thing to do; in the state colour; glyph 96 px */
#define UI_DISC_R_SECONDARY 52 /* glyph 64 px */
#define UI_DISC_R_ORBIT_SEL 40 /* the selected disc of an orbit; glyph 44 px */
#define UI_DISC_R_SMALL 30     /* glyph 28 px */
#define UI_DISC_R_KEY 20       /* a key hint: a thin ring, no fill; glyph 20 px */
#define UI_DISC_SLACK 10       /* a fingertip's slack round a disc for ui_disc_hit */

typedef enum {
    UI_DISC_NORMAL,   /* rgb set: filled in it, dark glyph; rgb 0: grey fill, light glyph */
    UI_DISC_SELECTED, /* filled in rgb (the working blue when 0), dark glyph */
    UI_DISC_DISABLED  /* dim fill, dim glyph */
} ui_disc_state_t;

/* A filled circle with a centred glyph. A radius of UI_DISC_R_KEY or less draws the key-hint form: a 2 px ring
 * and the glyph, both in rgb. The glyph's size follows the radius (see above); a glyph the size lacks falls
 * back to the next smaller font. */
lv_obj_t *ui_disc_create(lv_obj_t *parent, int cx, int cy, int r);
void ui_disc_set_glyph(lv_obj_t *d, const char *icon); /* an ICON_* literal (it must outlive the object); "" = none */
void ui_disc_set_rgb(lv_obj_t *d, uint32_t rgb);       /* 0 = neutral grey */
void ui_disc_set_state(lv_obj_t *d, ui_disc_state_t st);
void ui_disc_set_pos(lv_obj_t *d, int cx, int cy);
void ui_disc_set_r(lv_obj_t *d, int r);
/* The hold ring: an arc round the disc, 0..1000 per mille clockwise from 12 o'clock, 0 hides it. 8 px outside
 * the disc and 6 px wide (3 and 3 on a key hint), white. Each step invalidates only the arc it adds. */
void ui_disc_set_hold(lv_obj_t *d, int permille);
/* The screen point is on the disc or within UI_DISC_SLACK of it. False while the disc is hidden. */
bool ui_disc_hit(lv_obj_t *d, int x, int y);

/* ── §3.5 orbit: a menu ── */

#define UI_ORBIT_SLOTS 8  /* discs on screen */
#define UI_ORBIT_ITEMS 24 /* items in all: three pages */
#define UI_ORBIT_R 150    /* the circle the discs sit on */

typedef struct {
    const char *glyph; /* an ICON_* literal */
    uint32_t rgb;      /* its colour when selected; 0 = the orbit's accent */
    bool disabled;
} ui_orbit_item_t;

/* Up to 8 discs on a circle about the screen centre, one selected (r 40, accent fill), the rest small and grey.
 * Up to 8 items are spread evenly from 12 o'clock; more are shown 8 to a page, 45 degrees apart, and the page of
 * the selection is the one on screen (pips under the subject say which). The subject (a sigil, a clock, a glyph)
 * is the screen's own, in the middle: the orbit leaves r 0..105 free.
 * The screen maps its input: the dial and button B to ui_orbit_step(), a tap to ui_orbit_hit() (select it, or run
 * it when it is the selection), button A to running ui_orbit_selected(), a hold to ui_orbit_set_hold(). */
lv_obj_t *ui_orbit_create(lv_obj_t *parent);
void ui_orbit_set_items(lv_obj_t *o, const ui_orbit_item_t *items, int n); /* copied; the selection is kept when it still exists */
void ui_orbit_set_accent(lv_obj_t *o, uint32_t rgb); /* the selected disc's colour: the session's state colour */
/* Radius of the circle and where the first item sits. With key hints on the screen and 8 items use phase 22:
 * the two lower discs then straddle the hints instead of covering them. */
void ui_orbit_set_geometry(lv_obj_t *o, int radius, int phase_deg);
void ui_orbit_select(lv_obj_t *o, int i);
int ui_orbit_step(lv_obj_t *o, int dir); /* +1 / -1, wraps, turns the page; returns the new selection */
int ui_orbit_selected(lv_obj_t *o);
int ui_orbit_count(lv_obj_t *o);
int ui_orbit_hit(lv_obj_t *o, int x, int y); /* the item under the screen point, or -1 */
void ui_orbit_set_hold(lv_obj_t *o, int permille); /* the hold ring of the selected disc */

/* ── §3.6 key hints ── */

typedef enum { UI_KEY_A, UI_KEY_B } ui_key_t;
#define UI_KEY_A_X 101 /* lower left, beside button A */
#define UI_KEY_B_X 365 /* lower right, beside button B */
#define UI_KEY_Y 365
#define RGB_KEY_A 0xe69f00 /* the physical button is amber */
#define RGB_KEY_B 0xe04a3a /* the physical button is red */

/* What a button does: a glyph in a thin ring at the rim, beside the button, in the button's colour. No letters.
 * The hint is a disc (r 20): hide, show and ui_disc_set_hold() it like one (a hold on a button fills its ring).
 * Its ring reaches r 207 from the screen centre: clear of the countdown (r 213) and the index ring (r 218). */
lv_obj_t *ui_key_hint_create(lv_obj_t *parent, ui_key_t key);
void ui_key_hint_set(lv_obj_t *hint, const char *icon); /* an ICON_* literal; "" or NULL: the ring alone */

/* ── §3.7 burst and sign ── */

#define UI_BURST_MS 700

/* Rule 7, a result: one hero glyph (128 px) alone on a black centre, in its colour, for UI_BURST_MS; then the
 * screen below shows again. `words` is what it means ("Approved"): it goes to the report and the debug log, never
 * to the glass. `icons` NULL takes theme_glyph(words). The caller plays the haptic.
 * It covers the screen below, so a screen that arms a hold when it appears (ASK) must not arm while
 * ui_burst_active(). The index ring stays visible round it. */
void ui_burst(const char *words, const char *icons, uint32_t rgb);
bool ui_burst_active(void);
void ui_burst_init(lv_obj_t *top);       /* ui.c, once */
void ui_burst_tick(uint32_t now_ms);     /* ui.c, every step */
void ui_burst_report(ui_report_t *r);    /* ui.c: the words, while it shows */

/* Rule 8, a sign: one or two glyphs side by side in a thin outlined capsule, both in `rgb`; `text` (may be NULL)
 * is content shown after them in white (what was sent, a short number). 56 px high, as wide as it needs, centred
 * on (cx, cy). ui_toast() shows one on the key-hint row; a screen may place its own. */
lv_obj_t *ui_sign_create(lv_obj_t *parent, int cx, int cy);
void ui_sign_set(lv_obj_t *s, const char *icons, const char *text, uint32_t rgb);

#ifdef UI_KIT_DEMO
/* Simulator only (script command `kit N`): page N of a gallery of every primitive on the top layer; 0 hides it. */
void ui_kit_demo(int page);
#endif
