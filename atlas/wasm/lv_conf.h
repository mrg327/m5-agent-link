/* LVGL configuration for the desktop simulator. Everything not listed keeps LVGL's default.
 * Mirrors the intent of the device config: RGB565, software renderer, 30 fps cap. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (384 * 1024)
#define LV_DEF_REFR_PERIOD 33
/* Per-frame cache of anti-aliased circle edges (LVGL flushes it after every refresh): the ring alone
 * needs about 9 radii in one frame (capsule 228/222 + cap 3, selected 231/219 + cap 6, hold arc 230/220 + cap 5,
 * pip), the deck adds a few small ones. With the default 4 the large ones are recomputed several times a frame. */
#define LV_DRAW_SW_CIRCLE_CACHE_SIZE 12

#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/* No built-in Montserrat: the UI's fonts are the generated Figtree and Phosphor sources in
 * core/src/fonts (UI-V3 §2.1). The default font, for a label that names none, is Figtree 20. */
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(font_figtree_20_400)
#define LV_FONT_DEFAULT &font_figtree_20_400

#define LV_USE_DEMO_WIDGETS 0
#define LV_BUILD_EXAMPLES 0

#endif
