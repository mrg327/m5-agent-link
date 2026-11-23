/* The LABS screen in Sigil (docs/design/GLYPHS.md §5; EXPERIMENTS.md): a swipe up on the deck opens
 * the experiments, features that sit outside the agent workflow and may change or go. The subject
 * is the atom, the experiments orbit it as discs, the selected one large and in the accent. One
 * that cannot start is dim, and why is a sign under the atom: the plug while offline, "computer,
 * download" while the daemon is too old. A tap or A opens the selected one, the dial and B move the
 * selection, a swipe down or right goes back. No word is drawn; the report names the experiments
 * and says why one is blocked, as before (rule 9). */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "nav.h"
#include "ui_icons.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define SUBJECT_Y 233
/* between the key hints: the toast a refused tap shows is the same sign, in the same place */
#define SIGN_Y UI_Y_TOAST

enum { LAB_TRACKPAD, LAB_COUNT };

static const char *const k_name[LAB_COUNT] = {"Trackpad"};
static const char *const k_glyph[LAB_COUNT] = {ICON_CURSOR};

static lv_obj_t *s_root, *s_subject, *s_orbit, *s_sign, *s_hint[2];
static uint32_t s_now, s_last_input;
static int s_scrub_last;
static const char *s_sign_icons = "";
static char s_desc_txt[96]; /* in words, for the report */

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
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

/* The same reason as a sign: the vocabulary's glyphs for "Offline" and "Out of date". */
static const char *blocked_sign(int id)
{
    if (id == LAB_TRACKPAD && ui_link_connected()) return ICON_COMPUTER ICON_UPDATE;
    return ICON_OFFLINE;
}

static void render(void)
{
    int sel = ui_orbit_selected(s_orbit);
    const char *why = blocked(sel);
    ui_orbit_item_t items[LAB_COUNT];
    for (int i = 0; i < LAB_COUNT; i++)
        items[i] = (ui_orbit_item_t){k_glyph[i], 0, blocked(i) != NULL};
    ui_orbit_set_items(s_orbit, items, LAB_COUNT);
    if (sel == LAB_TRACKPAD)
        snprintf(s_desc_txt, sizeof s_desc_txt, "%s",
                 why ? why : "Use the watch as this PC's trackpad");
    else s_desc_txt[0] = 0;
    const char *icons = why ? blocked_sign(sel) : "";
    if (icons != s_sign_icons) { /* literals: the pointer is the identity */
        s_sign_icons = icons;
        if (icons[0]) ui_sign_set(s_sign, icons, NULL, RGB_AMBER);
    }
    show(s_sign, why != NULL);
    show(s_hint[0], why == NULL);
}

void ui_labs_refresh(uint32_t now)
{
    s_now = now;
    render();
}

void ui_labs_open(uint32_t now)
{
    s_now = s_last_input = now;
    ui_orbit_select(s_orbit, 0);
    ui_home_rim_mode(true); /* a rim drag dials through the experiments */
    render();
    show(s_root, true);
    dbg_event("ui", "labs", "\"items\":%d,\"pointer\":%s", LAB_COUNT,
              model_pointer() ? "true" : "false");
}

void ui_labs_close(void) { show(s_root, false); }

static void select_item(int i, const char *via, void (*haptic)(void))
{
    i = ((i % LAB_COUNT) + LAB_COUNT) % LAB_COUNT;
    if (i == ui_orbit_selected(s_orbit)) return;
    ui_orbit_select(s_orbit, i);
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
        int hit = g->kind == G_TAP ? ui_orbit_hit(s_orbit, g->x, g->y) : -1;
        if (hit >= 0) select_item(hit, "tap", NULL);
        commit(ui_orbit_selected(s_orbit));
        return true;
    }
    case NAV_HOLD_LONG:
        if (ui_orbit_hit(s_orbit, g->x, g->y) < 0) ui_go_back();
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
        /* The discs reach past the centre zone (nav.h: r 150), where a tap is no action of this
         * screen: a tap on a disc is still a tap on it. */
        int hit = g->kind == G_TAP ? ui_orbit_hit(s_orbit, g->x, g->y) : -1;
        if (hit < 0) return false;
        select_item(hit, "tap", NULL);
        commit(hit);
        return true;
    }
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
    int sel = ui_orbit_selected(s_orbit);
    for (int k = 0; k < LAB_COUNT; k++)
        ui_report_add(r, "%s%s", k == 0 ? "> " : "", k_name[(sel + k) % LAB_COUNT]);
    if (s_desc_txt[0]) ui_report_add(r, "%s", s_desc_txt);
}

lv_obj_t *ui_labs_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_subject = ui_glyph_create(s_root, FONT_ICON_HERO, UI_CX, SUBJECT_Y);
    ui_glyph_set(s_subject, ICON_LABS, COL_TEXT);
    s_orbit = ui_orbit_create(s_root);
    s_sign = ui_sign_create(s_root, UI_CX, SIGN_Y);
    /* rule 6: A opens the selected experiment (the tick of "select"), B is the next one */
    s_hint[0] = ui_key_hint_create(s_root, UI_KEY_A);
    s_hint[1] = ui_key_hint_create(s_root, UI_KEY_B);
    ui_key_hint_set(s_hint[0], ICON_CHECK);
    ui_key_hint_set(s_hint[1], ICON_NEXT);
    show(s_hint[1], LAB_COUNT > 1); /* one experiment: nothing to step to */
    show(s_sign, false);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
