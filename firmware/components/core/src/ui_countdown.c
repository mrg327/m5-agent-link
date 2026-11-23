/* The answer window's countdown (ANSWER-WINDOW.md §5) in Sigil (GLYPHS.md rule 4: a countdown is an
 * arc that drains). One thin arc just inside the index ring: a whole circle when the window opens,
 * its end running back toward 12 o'clock as the time to answer on the watch runs out, the way a
 * hold ring fills from there. Shown with the request it belongs to (ASK, and its card on the deck),
 * nowhere else. No text: the arc is the whole readout.
 *
 * Geometry: the band r 213..216 (centre line 214.5, 3 px), round caps. That band is free on every
 * screen: the content stays inside r 205, the key hints reach r 207 and the index ring starts at r
 * 218. The hold progress draws over the index ring itself (r 218..233), so the two never meet.
 * Chrome grey; white in the last COUNTDOWN_LATE_MS, so the end is not a surprise.
 *
 * Redraw discipline (UI-V2 §8a): it is a ui_arc (ui_kit.h), which moves in whole degrees and
 * invalidates only the piece between the old end and the new one, so a minute's countdown costs a
 * few small redraws a second. */
#include "ui_countdown.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define CD_R 215 /* ui_arc: its outer edge is r + w / 2 = 216 */
#define CD_W 3
#define COUNTDOWN_LATE_MS 10000

static lv_obj_t *s_arc;
static int s_pm = -1; /* what is left of the window, per mille; -1 = hidden */
static uint32_t s_rid, s_left_ms;

void ui_countdown_init(lv_obj_t *screen)
{
    s_arc = ui_arc_create(screen, UI_CX, UI_CX, CD_R, CD_W);
    lv_obj_add_flag(s_arc, LV_OBJ_FLAG_HIDDEN);
}

void ui_countdown_show(const attention_t *a, uint32_t now_ms)
{
    uint32_t left = a ? model_attention_left_ms(a, now_ms) : UINT32_MAX;
    if (!a || a->answer_sent || !a->win_ms || left == UINT32_MAX) {
        if (s_pm >= 0) {
            s_pm = -1;
            s_rid = 0;
            lv_obj_add_flag(s_arc, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    int pm = (int)((uint64_t)left * 1000 / a->win_ms);
    if (pm > 1000) pm = 1000;
    s_left_ms = left;
    /* change-only: a step of less than a degree, in the same colour, redraws nothing */
    ui_arc_set(s_arc, pm, left <= COUNTDOWN_LATE_MS ? COL_TEXT : COL_TEXT2, lv_color_black());
    if (s_pm < 0) lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_HIDDEN); /* appears: one full redraw */
    s_pm = pm;
    s_rid = a->rid;
}

int ui_countdown_left_s(void) { return s_pm < 0 ? -1 : (int)((s_left_ms + 999) / 1000); }

void ui_countdown_report(ui_report_t *r)
{
    if (s_pm < 0) return;
    ui_report_add(r, "countdown %u s", (unsigned)((s_left_ms + 999) / 1000));
}
