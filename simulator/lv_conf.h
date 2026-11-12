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

/* Only what the UI uses is built: plain objects, labels, one line, flex rows, animations. No theme: every
 * object is styled by ui_theme / its screen, and the default theme allocated ~30 KB of styles (internal RAM
 * on the device) and looked each new object up against every widget class. The device has the same list in
 * firmware/sdkconfig.defaults; the two must stay in step, because the simulator's screenshots stand in for
 * the glass. */
/* The display is RGB565; layers (a dimmed or fading container) are ARGB8888; glyphs are A8 masks. */
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB565A8 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_I1 0
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0
#define LV_USE_OBSERVER 0
#define LV_USE_GRID 0
#define LV_LABEL_TEXT_SELECTION 0
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_ARCLABEL 0
#define LV_USE_BAR 0
#define LV_USE_BUTTON 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGE 1 /* lv_draw_image and the layer code need the widget's header types */
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

#endif
