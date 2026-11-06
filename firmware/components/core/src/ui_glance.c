/* Glance: status ring (one segment per session), counts, clock. Tap ring segment → session. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"

static lv_obj_t *s_ring, *s_clock, *s_link, *s_dot, *s_hero, *s_caption, *s_row, *s_running, *s_done, *s_hint;
static uint8_t s_sorted[MODEL_MAX_SESSIONS];
static size_t s_n;
static char s_clock_text[8];
static uint32_t s_dot_rgb;
static bool s_pulsing;

static void dot_pulse_cb(void *obj, int32_t v)
{
    lv_color_t base = lv_color_hex(s_dot_rgb);
    lv_obj_set_style_bg_color(obj, lv_color_mix(base, COL_BG, (uint8_t)(90 + v)), 0);
}

static void pulse(bool on)
{
    if (on == s_pulsing) return;
    s_pulsing = on;
    lv_anim_delete(s_dot, dot_pulse_cb);
    if (!on) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_dot);
    lv_anim_set_exec_cb(&a, dot_pulse_cb);
    lv_anim_set_values(&a, 0, 165);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_reverse_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void update_clock(void)
{
    uint32_t now = theme_now_unix();
    char t[8] = "--:--";
    if (now) {
        int32_t local = (int32_t)now + model_settings()->tz_off_min * 60;
        snprintf(t, sizeof t, "%02u:%02u", (unsigned)(local / 3600 % 24), (unsigned)(local / 60 % 60));
    }
    if (strcmp(t, s_clock_text) == 0) return;
    strcpy(s_clock_text, t);
    lv_label_set_text(s_clock, t);
}

static void refresh(void)
{
    s_n = model_sorted(s_sorted);
    lv_color_t colors[MODEL_MAX_SESSIONS];
    for (size_t i = 0; i < s_n; i++) colors[i] = lv_color_hex(theme_state_rgb(model_at(s_sorted[i])->state));
    ring_set(s_ring, colors, (int)s_n);

    model_counts_t c = model_counts();
    uint32_t need = c.waiting + c.error;
    uint32_t hero_rgb = c.error && !c.waiting ? RGB_DANGER : need ? RGB_AMBER : c.running ? RGB_ACCENT : RGB_IDLE;
    char buf[24];
    snprintf(buf, sizeof buf, "%u", (unsigned)need);
    lv_label_set_text(s_hero, buf);
    lv_obj_set_style_text_color(s_hero, need ? lv_color_hex(hero_rgb) : COL_MUTED, 0);
    lv_label_set_text(s_caption, s_n == 0 ? "no sessions" : need ? "need you" : "all quiet");

    snprintf(buf, sizeof buf, "%u working", (unsigned)c.running);
    lv_label_set_text(s_running, buf);
    snprintf(buf, sizeof buf, "%u done", (unsigned)c.done_unseen);
    lv_label_set_text(s_done, buf);

    bool up = ui_link_connected();
    lv_label_set_text(s_link, up ? "" : LV_SYMBOL_WARNING " offline");
    lv_label_set_text(s_hint, s_n ? LV_SYMBOL_UP " swipe up" : "");

    s_dot_rgb = hero_rgb;
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(hero_rgb), 0);
    pulse(need || c.running);
    update_clock();
}

static void on_show(void) { refresh(); }

static void tick(uint32_t now_ms)
{
    (void)now_ms;
    update_clock();
}

static bool gesture(const gesture_evt_t *g)
{
    switch (g->kind) {
    case G_TAP: {
        int seg = ring_hit(s_ring, g->x, g->y);
        if (seg >= 0 && (size_t)seg < s_n) {
            ui_select(model_at(s_sorted[seg])->sid);
            ui_goto(SCR_DETAIL, NAV_FORWARD);
        } else {
            ui_goto(SCR_LIST, NAV_UP);
        }
        return true;
    }
    case G_SWIPE_UP:
    case G_BTN_A:
        ui_goto(SCR_LIST, NAV_UP);
        return true;
    default: return false;
    }
}

static void report(ui_report_t *r)
{
    ui_report_add(r, "%s", lv_label_get_text(s_clock));
    ui_report_add(r, "%s", lv_label_get_text(s_hero));
    ui_report_add(r, "%s", lv_label_get_text(s_caption));
    ui_report_add(r, "%s", lv_label_get_text(s_running));
    ui_report_add(r, "%s", lv_label_get_text(s_done));
    if (!ui_link_connected()) ui_report_add(r, "offline");
}

ui_screen_t g_glance = {.on_show = on_show, .refresh = refresh, .tick = tick, .gesture = gesture, .report = report};

static lv_obj_t *centered(lv_obj_t *l, int y)
{
    lv_obj_set_width(l, 300);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

void ui_glance_init(void)
{
    lv_obj_t *scr = g_glance.root = ui_new_screen();
    s_ring = ring_create(scr);
    s_clock = centered(theme_label(scr, 24, COL_MUTED), 62);
    s_link = centered(theme_label(scr, 16, lv_color_hex(RGB_AMBER)), 96);

    s_dot = lv_obj_create(scr);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_size(s_dot, 14, 14);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_align(s_dot, LV_ALIGN_TOP_MID, 0, 132);

    s_hero = centered(theme_label(scr, 48, COL_TEXT), 156);
    s_caption = centered(theme_label(scr, 20, COL_MUTED), 218);

    s_row = lv_obj_create(scr);
    lv_obj_remove_style_all(s_row);
    lv_obj_set_size(s_row, 300, 28);
    lv_obj_align(s_row, LV_ALIGN_TOP_MID, 0, 264);
    lv_obj_set_flex_flow(s_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_row, 18, 0);
    lv_obj_remove_flag(s_row, LV_OBJ_FLAG_SCROLLABLE);
    s_running = theme_label(s_row, 20, lv_color_hex(RGB_ACCENT));
    s_done = theme_label(s_row, 20, lv_color_hex(RGB_SUCCESS));
    lv_label_set_long_mode(s_running, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_long_mode(s_done, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(s_running, LV_SIZE_CONTENT);
    lv_obj_set_width(s_done, LV_SIZE_CONTENT);

    s_hint = centered(theme_label(scr, 16, lv_color_hex(RGB_IDLE)), 372);
    s_clock_text[0] = 0;
}
