/* MODE_TOY: the host of the toys (ui_toy.h, EXPERIMENTS.md "Toys"). It owns what every toy shares:
 * the root each one draws on, raw keys while it asks for them, and the pause when it leaves the
 * screen, so a request can take over at once and the toy is found as it was. */
#include "ui_toy.h"

#include <stdio.h>

#include "debug.h"
#include "persona.h"
#include "ui_decide.h"
#include "ui_living.h"

/* The toys built into the firmware, in LABS order. */
static const ui_toy_t *const s_builtin[] = {&ui_decide_toy};

static const ui_toy_t *s_toys[UI_TOY_MAX];
static lv_obj_t *s_roots[UI_TOY_MAX];
static int s_n;
static lv_obj_t *s_host; /* the mode's root: every toy's root is on it */
static int s_cur = -1;   /* selected, open or paused */
static bool s_open, s_paused;
static uint32_t s_now;

static void make_root(int i)
{
    lv_obj_t *r = s_roots[i] = lv_obj_create(s_host);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    s_toys[i]->init(r);
}

int ui_toy_register(const ui_toy_t *toy)
{
    if (!toy || s_n >= UI_TOY_MAX) return -1;
    int i = s_n++;
    s_toys[i] = toy;
    if (s_host) make_root(i); /* after ui_init (the simulator's stub) */
    return i;
}

int ui_toy_count(void) { return s_n; }
const ui_toy_t *ui_toy_get(int i) { return i >= 0 && i < s_n ? s_toys[i] : NULL; }
int ui_toy_paused(void) { return s_paused ? s_cur : -1; }

unsigned ui_toy_allow(void)
{
    persona_ctx_t c = *ui_living_ctx();
    c.display_on = true;   /* a toy on screen is looked at; a glow does not hide it */
    c.user_started = true; /* the user opened it: buzz and sound are the toy gate's call */
    return persona_allow(PF_TOYS, &c);
}

lv_obj_t *ui_toy_init(lv_obj_t *screen)
{
    s_host = lv_obj_create(screen);
    lv_obj_remove_style_all(s_host);
    lv_obj_set_size(s_host, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_host, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_host, LV_OBJ_FLAG_HIDDEN);
    for (size_t k = 0; k < sizeof s_builtin / sizeof *s_builtin; k++) {
        if (s_builtin[k]) { ui_toy_register(s_builtin[k]); }
    }
    for (int i = 0; i < s_n; i++) {
        if (!s_roots[i]) make_root(i);
    }
    return s_host;
}

void ui_toy_select(int i)
{
    if (i < 0 || i >= s_n) return;
    if (i != s_cur) s_paused = false; /* another toy: the paused one is no longer offered */
    s_cur = i;
}

void ui_toy_open(uint32_t now_ms)
{
    s_now = now_ms;
    if (s_cur < 0) return;
    const ui_toy_t *t = s_toys[s_cur];
    lv_obj_remove_flag(s_host, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_roots[s_cur], LV_OBJ_FLAG_HIDDEN);
    ui_home_rim_mode(true); /* the rim is the toy's: no session picking under it */
    g_ui_actions->raw_keys(t->raw_keys);
    dbg_event("ui", "toy", "\"name\":%s,\"open\":true,\"paused\":%s", dbg_q(t->name),
              s_paused ? "true" : "false");
    s_open = true;
    s_paused = false;
    t->open(now_ms);
}

void ui_toy_close(void)
{
    if (!s_open) return;
    const ui_toy_t *t = s_toys[s_cur];
    s_open = false;
    s_paused = true;
    t->pause(s_now);
    g_ui_actions->toy_interrupt();
    g_ui_actions->raw_keys(false);
    lv_obj_add_flag(s_roots[s_cur], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_host, LV_OBJ_FLAG_HIDDEN);
    dbg_event("ui", "toy", "\"name\":%s,\"open\":false", dbg_q(t->name));
}

bool ui_toy_act(nav_action_t a, const gesture_evt_t *g)
{ return s_open && s_toys[s_cur]->act(a, g); }

void ui_toy_tick(uint32_t now_ms)
{
    s_now = now_ms;
    if (s_open) s_toys[s_cur]->tick(now_ms);
}

void ui_toy_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "toy");
    if (s_open) s_toys[s_cur]->report(r);
}
