/* The wheel: a short list of three rows with the selected item in the middle (UI-V3 §3.2, §3.3). One
 * presentation component, driven by an item list; AGENT, ASK and STATUS use it. It knows nothing about
 * gestures: the owner moves the selection, hit-tests taps with wheel_hit() and commits.
 *   WHEEL_STYLE_LIST    pitch 62: a raised capsule behind the selected row, an icon column, left-aligned
 *                       labels; a hold item is drawn in its colour with the hold ring at the capsule's end.
 *   WHEEL_STYLE_BUTTON  pitch 48: the selected row is a 280 px button filled with the item's colour
 *                       (rgb 0 = neutral), neighbours are plain centred text; a hold item has the hold ring. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define WHEEL_MAX_ITEMS 8

typedef enum { WHEEL_STYLE_LIST = 0, WHEEL_STYLE_BUTTON } wheel_style_t;

typedef struct {
    const char *label;
    uint32_t rgb; /* LIST: colour of a hold item; BUTTON: the button's fill (0 = neutral) */
    bool hold;    /* destructive or approve-class: shows the hold ring; the owner commits it only by a hold */
    int id;       /* owner's action id */
    const char *icon; /* optional ICON_* string (UTF-8, copied); NULL for none */
} wheel_item_t;

/* Where the rows sit: the selected row at cy, its neighbours dy1 above and below (0 = the style's pitch:
 * 62 for LIST, 48 for BUTTON). dy2 is unused since UI-V3 (the wheel shows three rows); kept so old
 * layouts compile. */
typedef struct {
    int cy, dy1, dy2;
    wheel_style_t style;
} wheel_layout_t;
#define WHEEL_LAYOUT_CENTRE ((wheel_layout_t){226, 0, 0, WHEEL_STYLE_LIST})
#define WHEEL_LAYOUT_LIST(cy) ((wheel_layout_t){(cy), 0, 0, WHEEL_STYLE_LIST})
#define WHEEL_LAYOUT_BUTTON(cy) ((wheel_layout_t){(cy), 0, 0, WHEEL_STYLE_BUTTON})

lv_obj_t *wheel_create_at(lv_obj_t *parent, wheel_layout_t layout);
void wheel_set(lv_obj_t *w, const wheel_item_t *items, int n, int sel);
void wheel_set_shown(lv_obj_t *w, bool shown);
void wheel_select(lv_obj_t *w, int sel);
int wheel_sel(const lv_obj_t *w);
int wheel_count(const lv_obj_t *w);
const wheel_item_t *wheel_item(const lv_obj_t *w, int i);
/* Index of the item whose row contains the screen point, or -1 (outside the rows, or nothing shown). */
int wheel_hit(const lv_obj_t *w, int x, int y);
/* Hold fill on the selected row: a lighter overlay grows from the left edge of the capsule. 0 clears it. */
void wheel_set_progress(lv_obj_t *w, int permille);
