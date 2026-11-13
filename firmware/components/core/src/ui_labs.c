/* The LABS screen (EXPERIMENTS.md): a swipe up on the deck opens the experiments, features that sit outside
 * the agent workflow and may change or go. A list on the wheel like STATUS: a tap or A opens the selected one,
 * scrub and B move the selection, a swipe down or right goes back. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "nav.h"
#include "ui_icons.h"
#include "ui_internal.h"
#include "ui_theme.h"
#include "ui_wheel.h"

#define TITLE_Y 112
#define WHEEL_Y 226
#define DESC_Y 316

enum { LAB_TRACKPAD, LAB_COUNT };

static lv_obj_t *s_root, *s_title, *s_wheel, *s_desc, *s_badge[2], *s_key[2], *s_word[2];
static uint32_t s_now, s_last_input;
static int s_scrub_last;
static char s_desc_txt[96];

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Why an experiment cannot start, or NULL when it can. */
static const char *blocked(int id)
{
    if (id == LAB_TRACKPAD) {
        if (!ui_link_connected()) return "Offline";
        if (!model_pointer()) return "agentlinkd on this PC has no trackpad (needs 0.20)";
    }
    return NULL;
}

static void render(void)
{
    int sel = wheel_sel(s_wheel);
    const char *why = blocked(sel);
    char buf[96];
    if (sel == LAB_TRACKPAD) snprintf(buf, sizeof buf, "%s", why ? why : "Use the watch as this PC's trackpad");
    else buf[0] = 0;
    theme_set_text(s_desc, s_desc_txt, sizeof s_desc_txt, buf);
    theme_set_text_color(s_desc, why ? lv_color_hex(RGB_AMBER) : COL_TEXT2);
    show(s_badge[0], why == NULL);
}

void ui_labs_refresh(uint32_t now)
{
    s_now = now;
    render();
}

void ui_labs_open(uint32_t now)
{
    s_now = s_last_input = now;
    wheel_item_t items[LAB_COUNT] = {{"Trackpad", 0, false, LAB_TRACKPAD, ICON_COMPUTER}};
    wheel_set(s_wheel, items, LAB_COUNT, 0);
    wheel_set_shown(s_wheel, true);
    ui_home_rim_mode(true); /* a rim drag dials through the list */
    render();
    show(s_root, true);
    dbg_event("ui", "labs", "\"items\":%d,\"pointer\":%s", LAB_COUNT, model_pointer() ? "true" : "false");
}

void ui_labs_close(void) { show(s_root, false); }

static void select_item(int i, const char *via, void (*haptic)(void))
{
    i = ((i % LAB_COUNT) + LAB_COUNT) % LAB_COUNT;
    if (i == wheel_sel(s_wheel)) return;
    wheel_select(s_wheel, i);
    render();
    dbg_event("ui", "item", "\"idx\":%d,\"via\":\"%s\"", i, via);
    if (haptic) haptic();
}

static void commit(int sel)
{
    const char *why = blocked(sel);
    dbg_event("ui", "labs_open", "\"idx\":%d,\"ok\":%s", sel, why ? "false" : "true");
    if (why) {
        ui_toast(sel == LAB_TRACKPAD && ui_link_connected() ? "Update agentlinkd" : why, RGB_AMBER);
        g_ui_actions->reject();
        return;
    }
    if (sel == LAB_TRACKPAD) {
        ui_go_trackpad();
        g_ui_actions->tick();
    }
}

bool ui_labs_act(nav_action_t a, const gesture_evt_t *g)
{
    if (g->kind != G_LIFT && g->kind != G_SHAKE) s_last_input = s_now;
    switch (a) {
    case NAV_COMMIT: {
        int hit = g->kind == G_TAP ? wheel_hit(s_wheel, g->x, g->y) : -1;
        if (hit >= 0) select_item(hit, "tap", NULL);
        commit(wheel_sel(s_wheel));
        return true;
    }
    case NAV_HOLD_LONG:
        if (wheel_hit(s_wheel, g->x, g->y) < 0) ui_go_back();
        return true;
    case NAV_SCRUB: {
        int d = nav_list_scrub(&s_scrub_last, g->kind, g->x);
        if (d) select_item(wheel_sel(s_wheel) + d, "scrub", g_ui_actions->detent);
        return true;
    }
    case NAV_NEXT: select_item(wheel_sel(s_wheel) + 1, "btn", g_ui_actions->tick); return true;
    default: return false;
    }
}

bool ui_labs_tick(uint32_t now)
{
    s_now = now;
    return now - s_last_input > NAV_LABS_TIMEOUT_MS;
}

void ui_labs_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "labs");
    ui_report_add(r, "Experiments");
    int n = wheel_count(s_wheel), sel = wheel_sel(s_wheel);
    for (int k = 0; k < n; k++) ui_report_add(r, "%s%s", k == 0 ? "> " : "", wheel_item(s_wheel, (sel + k) % n)->label);
    if (s_desc_txt[0]) ui_report_add(r, "%s", s_desc_txt);
}

static lv_obj_t *centred(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, int cy, int w, int lines)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(font));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, UI_CX - w / 2, cy - lv_font_get_line_height(font) / 2);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ui_labs_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_title = centred(s_root, FONT_HEAD, COL_TEXT, TITLE_Y, theme_chord(TITLE_Y, 30), 1);
    lv_label_set_text(s_title, "Experiments");
    s_wheel = wheel_create_at(s_root, WHEEL_LAYOUT_LIST(WHEEL_Y));
    wheel_set_shown(s_wheel, false);
    s_desc = centred(s_root, FONT_BODY, COL_TEXT2, DESC_Y, theme_chord(DESC_Y + 24, 20), 2);
    lv_label_set_long_mode(s_desc, LV_LABEL_LONG_MODE_WRAP);
    static const char *const words[2] = {"Open", "Next"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    show(s_badge[1], false); /* one experiment: nothing to step to */
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
