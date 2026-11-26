/* The Mote on the pairing passkey screen: see ui_mote_pair.h. The orb sits at 12 o'clock where it
 * rests on the idle face (r 175: y 58). Its box reaches y 30-86 still and y 14-102 at the flare's
 * peak, clear of the Bluetooth glyph (from y 131) and the digits (from y 235). The screen is lit,
 * so the orb is at full brightness (the idle face's 45 % is the ambient level). */
#include "ui_mote_pair.h"

#include <string.h>

#include "debug.h"
#include "mote.h"
#include "persona.h"
#include "ui_internal.h"
#include "ui_living.h"
#include "ui_orb.h"

#define PAIR_MOTE_X UI_CX
#define PAIR_MOTE_Y (UI_CX - MOTE_TRACK_R)
#define PAIR_MOTE_LUM 100

static lv_obj_t *s_obj;
static mote_look_t s_look; /* what is drawn; core_r 0: nothing */
static bool s_flaring;
static uint32_t s_flare_ms;

static void inval(void)
{
    if (!s_look.core_r) return;
    lv_area_t a;
    ui_orb_area(PAIR_MOTE_X, PAIR_MOTE_Y, &s_look, &a);
    lv_obj_invalidate_area(s_obj, &a);
}

static void set_look(mote_look_t l)
{
    if (l.core_r == s_look.core_r && l.halo_r == s_look.halo_r && l.lum == s_look.lum &&
        l.halo_pct == s_look.halo_pct) {
        return;
    }
    inval();
    s_look = l;
    inval();
}

static void on_draw(lv_event_t *e)
{
    if (s_look.core_r) {
        ui_orb_draw(lv_event_get_layer(e), PAIR_MOTE_X, PAIR_MOTE_Y, &s_look, MOTE_RGB);
    }
}

/* The gate: while the digits wait for you, a pose at most (pairing counts as needing you); after
 * it, a one-shot unless something else needs you, reduced motion is on or the switch is off. */
static unsigned allow(bool waiting)
{
    persona_ctx_t c = *ui_living_ctx();
    if (waiting && !c.needs_you) c.needs_you = 1;
    c.user_started = false;
    c.alerting = false;
    return persona_allow(PF_MOTE, &c);
}

static mote_look_t still(void) { return mote_look(100, PAIR_MOTE_LUM, 0); }

void ui_mote_pair_create(lv_obj_t *parent)
{
    s_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s_obj);
    lv_obj_remove_flag(s_obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_obj, UI_SIZE, UI_SIZE);
    lv_obj_set_pos(s_obj, 0, 0);
    lv_obj_add_event_cb(s_obj, on_draw, LV_EVENT_DRAW_MAIN, NULL);
    memset(&s_look, 0, sizeof s_look);
    s_flaring = false;
}

void ui_mote_pair_wait(void) { s_flaring = false; }

bool ui_mote_pair_done(uint32_t now_ms)
{
    if (!s_obj || !s_look.core_r || !(allow(false) & PA_ONESHOT)) return false;
    s_flaring = true;
    s_flare_ms = now_ms;
    dbg_event("fun", "mote_pair", "\"act\":\"flare\"");
    return true;
}

bool ui_mote_pair_tick(uint32_t now_ms, bool shown)
{
    if (!s_obj) return false;
    const mote_look_t none = {0};
    if (!shown) {
        s_flaring = false;
        set_look(none);
        return false;
    }
    if (s_flaring) {
        uint32_t t = now_ms - s_flare_ms;
        if (t >= MOTE_FLARE_MS || !(allow(false) & PA_ONESHOT)) { /* over, or an alert cut it */
            s_flaring = false;
            set_look(none);
            return true;
        }
        set_look(mote_flare(still(), PAIR_MOTE_LUM, t));
        return false;
    }
    bool was = s_look.core_r != 0;
    set_look((allow(true) & PA_POSE) ? still() : none);
    if (was != (s_look.core_r != 0)) {
        dbg_event("fun", "mote_pair", "\"act\":%s", dbg_q(s_look.core_r ? "still" : "gone"));
    }
    return false;
}
