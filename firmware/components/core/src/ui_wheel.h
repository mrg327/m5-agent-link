/* The wheel: a short curved list with the selected item in the centre row (UI-V2 §4.2). One
 * presentation component, driven by an item list; AGENT, ASK and STATUS all use it. It knows
 * nothing about gestures: the owner moves the selection and commits. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define WHEEL_MAX_ITEMS 8

typedef struct {
    const char *label;
    uint32_t rgb; /* chevrons and tag colour */
    bool hold;    /* destructive: shows a HOLD tag; the owner commits it only by a hold */
    int id;       /* owner's action id */
} wheel_item_t;

lv_obj_t *wheel_create(lv_obj_t *parent);
void wheel_set(lv_obj_t *w, const wheel_item_t *items, int n, int sel);
void wheel_select(lv_obj_t *w, int sel);
int wheel_sel(const lv_obj_t *w);
int wheel_count(const lv_obj_t *w);
const wheel_item_t *wheel_item(const lv_obj_t *w, int i);
