/* Ring screen host: toast, alert glow, link state, report for the debug channel. */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define TOAST_MS 1800

const ui_actions_t *g_ui_actions;
static lv_obj_t *s_home;
static bool s_link_up, s_animate = true;
static uint32_t s_seen_rev;
static uint32_t s_now_ms;

static lv_obj_t *s_toast, *s_toast_label;
static uint32_t s_toast_until_ms;
static lv_obj_t *s_glow, *s_glow_ring;

lv_obj_t *ui_new_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, UI_SIZE, UI_SIZE);
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static void create_overlays(void)
{
    lv_obj_t *top = lv_layer_top();
    s_toast = lv_obj_create(top);
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_style_bg_color(s_toast, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, LV_RADIUS_CIRCLE, 0); /* a tinted pill, no border */
    lv_obj_set_style_pad_hor(s_toast, 18, 0);
    lv_obj_set_style_pad_ver(s_toast, 8, 0);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    s_toast_label = theme_label(s_toast, 20, COL_TEXT);
    lv_label_set_long_mode(s_toast_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(s_toast, 280, 0);
    lv_obj_set_width(s_toast_label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toast_label, 240, 0);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    s_glow = lv_obj_create(top);
    lv_obj_remove_style_all(s_glow);
    lv_obj_set_size(s_glow, UI_SIZE, UI_SIZE);
    lv_obj_set_style_bg_color(s_glow, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_glow, LV_OPA_COVER, 0);
    s_glow_ring = ring_create(s_glow);
    lv_obj_add_flag(s_glow, LV_OBJ_FLAG_HIDDEN);
}

void ui_init(const ui_actions_t *actions)
{
    g_ui_actions = actions;
    s_home = ui_home_init();
    create_overlays();
    lv_screen_load(s_home);
    ui_home_refresh(0);
    dbg_event("ui", "screen", "\"screen\":\"ring\"");
}

void ui_set_animations(bool enabled)
{
    s_animate = enabled;
    ui_home_animations_changed();
}
bool ui_animations_allowed(void) { return s_animate; }

bool ui_link_connected(void) { return s_link_up; }

static bool s_was_up;
static uint32_t s_link_down_at;
uint32_t ui_link_down_ms(void) { return s_link_up || !s_was_up ? 0 : s_now_ms - s_link_down_at; }

void ui_set_link_state(bool connected)
{
    if (s_link_up == connected) return;
    if (connected) s_was_up = true;
    else s_link_down_at = s_now_ms;
    s_link_up = connected;
    s_seen_rev = 0; /* force a refresh */
}

static int s_link_kind = -1;
int ui_link_transport(void) { return s_link_kind; }

void ui_set_link_transport(int kind)
{
    if (s_link_kind == kind) return;
    s_link_kind = kind;
    s_seen_rev = 0;
}

void ui_show_session(uint32_t sid) { ui_home_select_sid(sid, "attention"); }

void ui_report_add(ui_report_t *r, const char *fmt, ...)
{
    if (r->n_visible >= 8) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->visible[r->n_visible++], sizeof r->visible[0], fmt, ap);
    va_end(ap);
}

void ui_report(ui_report_t *r)
{
    memset(r, 0, sizeof *r);
    r->power = "active";
    ui_home_report(r);
}

/* ───────── toast & glow ───────── */

void ui_toast(const char *text, uint32_t rgb)
{
    lv_label_set_text(s_toast_label, text);
    lv_obj_set_style_text_color(s_toast_label, COL_TEXT, 0);
    lv_obj_set_style_bg_color(s_toast, lv_color_mix(lv_color_hex(rgb), COL_BG, UI_TOAST_TINT_MIX), 0);
    lv_obj_update_layout(s_toast);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, UI_ACTION_Y - 4); /* over the action pill, clear of the key badges */
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    s_toast_until_ms = s_now_ms + TOAST_MS;
    dbg_event("ui", "toast", "\"text\":%s", dbg_q(text));
}

void ui_glow(uint32_t rgb)
{
    ring_set_solid(s_glow_ring, lv_color_mix(lv_color_hex(rgb), COL_BG, 140)); /* dim: ~55 % */
    lv_obj_remove_flag(s_glow, LV_OBJ_FLAG_HIDDEN);
    dbg_event("ui", "glow", "\"rgb\":%u", (unsigned)rgb);
}

void ui_glow_hide(void) { lv_obj_add_flag(s_glow, LV_OBJ_FLAG_HIDDEN); }
bool ui_glow_active(void) { return !lv_obj_has_flag(s_glow, LV_OBJ_FLAG_HIDDEN); }

/* ───────── per-step ───────── */

void ui_tick(uint32_t now_ms)
{
    s_now_ms = now_ms;
    if (!lv_obj_has_flag(s_toast, LV_OBJ_FLAG_HIDDEN) && (int32_t)(now_ms - s_toast_until_ms) >= 0)
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    if (model_rev() != s_seen_rev) {
        s_seen_rev = model_rev();
        ui_home_refresh(now_ms);
    }
    ui_home_tick(now_ms);
}

bool ui_on_gesture(const gesture_evt_t *g)
{
    return ui_home_gesture(g);
}
