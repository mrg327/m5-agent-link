/* LABS > Personality: see ui_fun.h. The switches are the bits of the stored `fun` word
 * (persona.h); this screen only shows and flips them. */
#include "ui_fun.h"

#include "debug.h"
#include "persona.h"
#include "prefs.h"
#include "ui_icons.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define SUBJECT_Y 233

typedef struct {
    const char *name; /* for the report: the glass shows the glyph */
    const char *glyph;
    uint32_t bit;
} fun_item_t;

static const fun_item_t s_items[] = {
    {"Living marks", ICON_WAVE, FUN_TEMPER},    {"Celebrations", ICON_DONE, FUN_CELEBRATE},
    {"Motif", ICON_SOUND, FUN_MOTIF},           {"Mote", ICON_G_DOT, FUN_MOTE},
    {"Hello and close", ICON_CLOCK, FUN_HELLO}, {"Replay", ICON_SKIP, FUN_REPLAY},
    {"Easter eggs", ICON_WILD, FUN_EGGS},       {"Trail", ICON_WEEK, FUN_TRAIL},
    {"Break cup", ICON_TIMER, FUN_CUP},         {"Toys", ICON_TAP, FUN_TOYS},
    {"Reduced motion", ICON_STOP, FUN_REDUCED},
};
#define FUN_ITEMS ((int)(sizeof s_items / sizeof s_items[0]))

static lv_obj_t *s_root, *s_subject, *s_orbit, *s_hint[2];
static int s_scrub_last;

static void render(void)
{
    uint32_t w = prefs_fun();
    ui_orbit_item_t items[FUN_ITEMS];
    for (int i = 0; i < FUN_ITEMS; i++) {
        items[i] = (ui_orbit_item_t){s_items[i].glyph, 0, !(w & s_items[i].bit)};
    }
    ui_orbit_set_items(s_orbit, items, FUN_ITEMS);
    const fun_item_t *it = &s_items[ui_orbit_selected(s_orbit)];
    ui_glyph_set(s_subject, it->glyph, w & it->bit ? COL_TEXT : COL_TEXT3);
}

static void toggle(int i)
{
    uint32_t w = prefs_fun() ^ s_items[i].bit;
    prefs_set_fun(w);
    dbg_event("ui", "fun_toggle", "\"idx\":%d,\"on\":%s", i, w & s_items[i].bit ? "true" : "false");
    render();
    g_ui_actions->tick();
}

static void select_item(int i, const char *via, void (*haptic)(void))
{
    i = ((i % FUN_ITEMS) + FUN_ITEMS) % FUN_ITEMS;
    if (i == ui_orbit_selected(s_orbit)) return;
    ui_orbit_select(s_orbit, i);
    render();
    dbg_event("ui", "item", "\"idx\":%d,\"via\":\"%s\"", i, via);
    if (haptic) haptic();
}

bool ui_fun_act(nav_action_t a, const gesture_evt_t *g)
{
    switch (a) {
    case NAV_COMMIT: {
        int hit = g->kind == G_TAP ? ui_orbit_hit(s_orbit, g->x, g->y) : -1;
        if (hit >= 0 && hit != ui_orbit_selected(s_orbit)) { /* a tap selects, a second toggles */
            select_item(hit, "tap", g_ui_actions->tick);
            return true;
        }
        toggle(ui_orbit_selected(s_orbit));
        return true;
    }
    case NAV_HOLD_LONG:
        if (ui_orbit_hit(s_orbit, g->x, g->y) < 0) ui_fun_close();
        return true;
    case NAV_SCRUB: {
        int d = nav_list_scrub(&s_scrub_last, g->kind, g->x);
        if (d) select_item(ui_orbit_selected(s_orbit) + d, "scrub", g_ui_actions->detent);
        return true;
    }
    case NAV_NEXT:
        select_item(ui_orbit_selected(s_orbit) + 1, "btn", g_ui_actions->tick);
        return true;
    default: {
        int hit = g->kind == G_TAP ? ui_orbit_hit(s_orbit, g->x, g->y) : -1;
        if (hit < 0) return false;
        if (hit == ui_orbit_selected(s_orbit)) {
            toggle(hit);
        } else {
            select_item(hit, "tap", g_ui_actions->tick);
        }
        return true;
    }
    }
}

void ui_fun_open(void)
{
    ui_orbit_select(s_orbit, 0);
    render();
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    dbg_event("ui", "fun", "\"word\":%u", (unsigned)prefs_fun());
}

void ui_fun_close(void) { lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN); }

bool ui_fun_is_open(void) { return !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN); }

void ui_fun_report(ui_report_t *r)
{
    ui_report_add(r, "Personality");
    uint32_t w = prefs_fun();
    int sel = ui_orbit_selected(s_orbit);
    for (int k = 0; k < FUN_ITEMS && r->n_visible < 7; k++) {
        const fun_item_t *it = &s_items[(sel + k) % FUN_ITEMS];
        ui_report_add(r, "%s%s %s", k == 0 ? "> " : "", it->name, w & it->bit ? "on" : "off");
    }
}

lv_obj_t *ui_fun_init(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    /* XL: the hero size holds only the state glyphs, and every switch has a glyph here */
    s_subject = ui_glyph_create(s_root, FONT_ICON_XL, UI_CX, SUBJECT_Y);
    s_orbit = ui_orbit_create(s_root);
    ui_orbit_set_geometry(s_orbit, UI_ORBIT_R,
                          22); /* eight a page: the upper two straddle the hints */
    /* rule 6: A flips the selected switch, B is the next one */
    s_hint[0] = ui_key_hint_create(s_root, UI_KEY_A);
    s_hint[1] = ui_key_hint_create(s_root, UI_KEY_B);
    ui_key_hint_set(s_hint[0], ICON_CHECK);
    ui_key_hint_set(s_hint[1], ICON_NEXT);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
