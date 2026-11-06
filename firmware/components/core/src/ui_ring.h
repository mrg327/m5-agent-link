/* Status ring around the bezel: one arc segment per session, drawn by a single custom object.
 * One draw callback instead of N full-screen arc widgets keeps the render cost and RAM low. */
#pragma once
#include "lvgl.h"

#define RING_MAX_SEGMENTS 32

lv_obj_t *ring_create(lv_obj_t *parent);
/* Segments run clockwise from 12 o'clock. n == 0 draws a dim idle ring. */
void ring_set(lv_obj_t *ring, const lv_color_t *colors, int n);
void ring_set_solid(lv_obj_t *ring, lv_color_t color);
/* Segment under a screen point, or -1 when the point is not on the ring band. */
int ring_hit(lv_obj_t *ring, int x, int y);
