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
#include "ui_fun.h"
#include "ui_icons.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_theme.h"
#include "ui_toy.h"

#define SUBJECT_Y 233
/* in the bottom slot: the toast a refused tap shows is the same sign, in the same place */
#define SIGN_Y UI_Y_TOAST

/* The trackpad, the Personality switchboard, then the toys (ui_toy.h) in their table's order. */
enum { LAB_TRACKPAD, LAB_FUN, LAB_TOYS };
#define LAB_MAX (LAB_TOYS + UI_TOY_MAX)

static int lab_count(void) { return LAB_TOYS + ui_toy_count(); }
static const char *lab_name(int id)
{
    if (id == LAB_TRACKPAD) return "Trackpad";
    if (id == LAB_FUN) return "Personality";
    return ui_toy_get(id - LAB_TOYS)->name;
}
static const char *lab_glyph(int id)
{
    if (id == LAB_TRACKPAD) return ICON_CURSOR;
    if (id == LAB_FUN) return ICON_WAVE;
    return ui_toy_get(id - LAB_TOYS)->glyph;
}

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
    /* a toy that could sway an answer waits until nothing is asked (EXPERIMENTS.md "Toys") */
    if (id >= LAB_TOYS && ui_toy_get(id - LAB_TOYS)->not_over_request &&
        ui_pending_attention_sid()) {
        return "Answer the request first";
    }
    return NULL;
}

/* The same reason as a sign: the vocabulary's glyphs for "Offline" and "Out of date". */
static const char *blocked_sign(int id)
{
    if (id >= LAB_TOYS) { return ICON_PERMISSION; /* a request waits */ }
    if (id == LAB_TRACKPAD && ui_link_connected()) return ICON_COMPUTER ICON_UPDATE;
    return ICON_OFFLINE;
}

static void render(void)
{
    int sel = ui_orbit_selected(s_orbit);
    const char *why = blocked(sel);
    ui_orbit_item_t items[LAB_MAX];
    int n = lab_count();
    for (int i = 0; i < n; i++) {
        items[i] = (ui_orbit_item_t){lab_glyph(i), 0, blocked(i) != NULL};
    }
    ui_orbit_set_items(s_orbit, items, n);
    if (sel == LAB_TRACKPAD)
        snprintf(s_desc_txt, sizeof s_desc_txt, "%s",
                 why ? why : "Use the watch as this PC's trackpad");
    else if (sel == LAB_FUN)
        snprintf(s_desc_txt, sizeof s_desc_txt, "Turn the watch's fun on and off");
    else snprintf(s_desc_txt, sizeof s_desc_txt, "%s", why ? why : lab_name(sel));
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
    int paused = ui_toy_paused(); /* the toy left paused comes first */
    render();                     /* the items first, so the selection fits them */
    ui_orbit_select(s_orbit, paused >= 0 ? LAB_TOYS + paused : 0);
    show(s_hint[1], lab_count() > 1); /* one experiment: nothing to step to */
    ui_home_rim_mode(true);           /* a rim drag dials through the experiments */
    render();
    show(s_root, true);
    dbg_event("ui", "labs", "\"items\":%d,\"pointer\":%s", lab_count(),
              model_pointer() ? "true" : "false");
}

void ui_labs_close(void)
{
    ui_fun_close();
    show(s_root, false);
}

bool ui_labs_back(void)
{
    if (!ui_fun_is_open()) return false;
    ui_fun_close();
    show(s_root, true);
    return true;
}

static void select_item(int i, const char *via, void (*haptic)(void))
{
    int n = lab_count();
    i = ((i % n) + n) % n;
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
        if (sel >= LAB_TOYS) {
            ui_toast_sign(why, ICON_PERMISSION, NULL, RGB_AMBER);
        } else {
            ui_toast(sel == LAB_TRACKPAD && ui_link_connected() ? "Update agentlinkd" : why,
                     RGB_AMBER);
        }
        g_ui_actions->reject();
        return;
    }
    if (sel == LAB_TRACKPAD) {
        ui_go_trackpad();
    } else if (sel == LAB_FUN) { /* the switchboard, inside LABS: back returns here */
        show(s_root, false);
        ui_fun_open();
    } else {
        ui_go_toy(sel - LAB_TOYS);
    }
    g_ui_actions->tick();
}

bool ui_labs_act(nav_action_t a, const gesture_evt_t *g)
{
    if (g->kind != G_LIFT && g->kind != G_SHAKE) s_last_input = s_now;
    if (ui_fun_is_open()) {
        bool used = ui_fun_act(a, g);
        if (!ui_fun_is_open()) show(s_root, true); /* held off the discs: back to the list */
        return used;
    }
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
    if (ui_fun_is_open()) {
        ui_fun_report(r);
        return;
    }
    ui_report_add(r, "Experiments");
    int sel = ui_orbit_selected(s_orbit);
    int n = lab_count();
    for (int k = 0; k < n; k++) {
        ui_report_add(r, "%s%s", k == 0 ? "> " : "", lab_name((sel + k) % n));
    }
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
    show(s_sign, false);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    ui_fun_init(screen);
    return s_root;
}
