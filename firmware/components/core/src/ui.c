/* Navigation between screens, toast, alert glow, report for the debug channel. */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define TOAST_MS 1400
#define SCREEN_ANIM_MS 250

const ui_actions_t *g_ui_actions;
static ui_screen_t *const s_screens[] = {&g_glance, &g_list, &g_detail};
static const char *const s_screen_names[] = {"glance", "list", "detail"};
static screen_id_t s_current = SCR_GLANCE;
static bool s_link_up, s_animate = true;
static uint32_t s_sel_sid;
static bool s_has_sel;
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
    lv_obj_set_style_border_color(s_toast, COL_BORDER, 0);
    lv_obj_set_style_border_width(s_toast, 2, 0);
    lv_obj_set_style_radius(s_toast, 24, 0);
    lv_obj_set_style_pad_hor(s_toast, 18, 0);
    lv_obj_set_style_pad_ver(s_toast, 8, 0);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    s_toast_label = theme_label(s_toast, 20, COL_TEXT);
    lv_label_set_long_mode(s_toast_label, LV_LABEL_LONG_MODE_CLIP);
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
    ui_glance_init();
    ui_list_init();
    ui_detail_init();
    create_overlays();
    s_current = SCR_GLANCE;
    lv_screen_load(g_glance.root);
    g_glance.on_show();
    dbg_event("ui", "screen", "\"screen\":\"glance\"");
}

void ui_set_animations(bool enabled) { s_animate = enabled; }
bool ui_animations_allowed(void) { return s_animate; }

screen_id_t ui_current(void) { return s_current; }
bool ui_link_connected(void) { return s_link_up; }

void ui_set_link_state(bool connected)
{
    if (s_link_up == connected) return;
    s_link_up = connected;
    s_seen_rev = 0; /* force a refresh */
}

void ui_select(uint32_t sid) { s_sel_sid = sid; s_has_sel = true; }
bool ui_selected(uint32_t *sid)
{
    if (s_has_sel && sid) *sid = s_sel_sid;
    return s_has_sel;
}

void ui_goto(screen_id_t id, ui_nav_t nav)
{
    if (id == s_current) return;
    static const lv_screen_load_anim_t anims[] = {LV_SCREEN_LOAD_ANIM_MOVE_LEFT, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                                                  LV_SCREEN_LOAD_ANIM_MOVE_TOP, LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM};
    s_current = id;
    s_screens[id]->on_show();
    /* Edge-to-edge slides copy strips instead of blending, which is the cheap kind of transition. */
    lv_screen_load_anim(s_screens[id]->root, s_animate ? anims[nav] : LV_SCREEN_LOAD_ANIM_NONE,
                        s_animate ? SCREEN_ANIM_MS : 0, 0, false);
    ui_report_t r;
    ui_report(&r);
    dbg_event("ui", "screen", "\"screen\":\"%s\",\"sel_sid\":%u", r.screen, (unsigned)(r.has_sel_sid ? r.sel_sid : 0));
}

void ui_show_glance(void) { ui_goto(SCR_GLANCE, NAV_BACK); }

void ui_show_session(uint32_t sid)
{
    ui_select(sid);
    if (s_current == SCR_DETAIL) {
        g_detail.on_show();
        return;
    }
    ui_goto(SCR_DETAIL, NAV_FORWARD);
}

bool ui_hit(const lv_obj_t *obj, int x, int y, int slop)
{
    lv_area_t a;
    lv_obj_get_coords((lv_obj_t *)obj, &a);
    return x >= a.x1 - slop && x <= a.x2 + slop && y >= a.y1 - slop && y <= a.y2 + slop;
}

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
    snprintf(r->screen, sizeof r->screen, "%s", s_screen_names[s_current]);
    r->has_sel_sid = s_has_sel && s_current != SCR_GLANCE;
    r->sel_sid = s_sel_sid;
    s_screens[s_current]->report(r);
}

/* ───────── toast & glow ───────── */

void ui_toast(const char *text, uint32_t rgb)
{
    lv_label_set_text(s_toast_label, text);
    lv_obj_set_style_text_color(s_toast_label, lv_color_hex(rgb), 0);
    lv_obj_update_layout(s_toast);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, 372);
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
        for (size_t i = 0; i < 3; i++) s_screens[i]->refresh(); /* cheap: hidden screens only update data */
    }
    s_screens[s_current]->tick(now_ms);
}

bool ui_on_gesture(const gesture_evt_t *g)
{
    return s_screens[s_current]->gesture(g);
}
