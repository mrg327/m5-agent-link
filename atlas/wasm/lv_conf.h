/* LVGL configuration for the desktop simulator. Everything not listed keeps LVGL's default.
 * Mirrors the intent of the device config: RGB565, software renderer, 30 fps cap. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (384 * 1024)
#define LV_DEF_REFR_PERIOD 33
/* Per-frame cache of anti-aliased circle edges (LVGL flushes it after every refresh): the ring alone
 * needs 9 radii in one frame (capsule 226/218 + cap 4, selected 229/215 + cap 7, underline 212/210 + cap 1),
 * the deck adds a few small ones. With the default 4 the large ones are recomputed several times a frame. */
#define LV_DRAW_SW_CIRCLE_CACHE_SIZE 12

#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/* Only the sizes the UI uses (ui_theme.c falls back to the default font when one is missing). */
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

#define LV_USE_DEMO_WIDGETS 0
#define LV_BUILD_EXAMPLES 0

#endif
