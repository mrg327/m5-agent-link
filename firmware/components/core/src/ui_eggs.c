/* The easter eggs on screen: see ui_eggs.h. The slip moves the clock label by a style translate
 * (its box and the deck's drift stay as they are), so a frame repaints the label's own box. */
#include "ui_eggs.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "eggs.h"
#include "persona.h"
#include "prefs.h"
#include "ui_internal.h"
#include "ui_living.h"
#include "ui_mote.h"
#include "ui_theme.h"

#define CLOCK_SET 100000u /* the RTC reads less until it is set */

static eggs_t s_e;
static lv_obj_t *s_slipped; /* the clock the slip moved, while it is off its place */

static void slip_to(lv_obj_t *clock, int dx)
{
    if (s_slipped && s_slipped != clock) { /* a clock that went away goes back first */
        lv_obj_set_style_translate_x(s_slipped, 0, 0);
        s_slipped = NULL;
    }
    if (!clock || (dx == 0 && s_slipped == NULL)) return;
    if (lv_obj_get_style_translate_x(clock, 0) != dx) lv_obj_set_style_translate_x(clock, dx, 0);
    s_slipped = dx ? clock : NULL;
}

/* The clock on show reads the local time already (it moves on at its own 500 ms tick). */
static bool reads(lv_obj_t *clock, uint32_t local)
{
    if (!clock) return false;
    char hm[8];
    snprintf(hm, sizeof hm, "%02u:%02u", (unsigned)(local / 3600 % 24),
             (unsigned)(local / 60 % 60));
    return strcmp(lv_label_get_text(clock), hm) == 0;
}

void ui_eggs_tick(uint32_t now_ms, lv_obj_t *clock)
{
    /* a one-shot, never while anything needs you, never on a dark display, not with reduced
     * motion: the gate says all of it */
    bool may = (ui_living_allow(PF_EGGS) & PA_ONESHOT) != 0;
    uint32_t unix_now = theme_now_unix();
    uint32_t local = unix_now > CLOCK_SET
                         ? unix_now + (uint32_t)((int32_t)model_settings()->tz_off_min * 60)
                         : 0;
    if (eggs_slip_due(&s_e, local, may && reads(clock, local), now_ms)) {
        dbg_event("fun", "egg", "\"egg\":\"clock-slip\",\"hm\":%u",
                  (unsigned)(local / 3600 % 24 * 100 + local / 60 % 60));
    }
    if (!may) s_e.slipping = false; /* an arrival cuts it */
    slip_to(clock, clock ? eggs_slip_dx(&s_e, now_ms) : 0);

    if (eggs_century_due(&s_e, may && ui_mode() == MODE_HOME, now_ms)) {
        bool mote =
            ui_mote_shown(); /* the Mote loops when it is on the face, else the sigil spins */
        if (mote) {
            ui_mote_loop();
        } else {
            ui_living_spin(now_ms);
        }
        dbg_event("fun", "egg", "\"egg\":\"century\",\"n\":%u,\"by\":%s",
                  (unsigned)prefs_finishes(), dbg_q(mote ? "mote" : "sigil"));
    }
}

void ui_eggs_on_session(const session_t *before, const session_t *now, uint32_t present)
{
    /* a finish the watch saw happen: not a session first met finished, not a re-sent state */
    if (!before || !(present & SF_STATE) || now->state != SESS_DONE) return;
    if (before->state == SESS_DONE) return;
    uint32_t n = eggs_count(prefs_finishes());
    prefs_set_finishes(n, eggs_save(n));
    eggs_finished(&s_e, n, (uint32_t)lv_tick_get());
    if (eggs_is_century(n)) dbg_event("fun", "century", "\"n\":%u", (unsigned)n);
}
