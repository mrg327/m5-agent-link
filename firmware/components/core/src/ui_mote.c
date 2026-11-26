/* The Mote on the idle face: see ui_mote.h. A step that changes the firefly (it moves, or its halo
 * breathes a level) repaints the orb's box, old and new (about 4-6 k px each; LVGL joins the two
 * when they overlap); nothing else on the face is touched. */
#include "ui_mote.h"

#include <math.h>
#include <string.h>

#include "debug.h"
#include "done.h"
#include "gauges.h"
#include "mote.h"
#include "persona.h"
#include "prefs.h"
#include "ui_internal.h"
#include "ui_living.h"
#include "ui_orb.h"
#include "ui_theme.h"

#define CLOCK_SET 100000u /* the RTC reads less until it is set */

static lv_obj_t *s_obj;
static mote_t s_m;
static mote_out_t s_out; /* what is drawn */
static uint32_t s_cheers, s_loops;
static uint32_t s_fin_sid; /* the latest big finish: the Mote goes to its arc */

/* ───────── geometry, in screen coordinates ───────── */

static void head_xy(const mote_out_t *o, int *x, int *y)
{
    lv_area_t c;
    lv_obj_get_coords(s_obj, &c);
    float a = (float)o->deg * 0.017453292f;
    *x = c.x1 + UI_CX + (int)lroundf((float)MOTE_TRACK_R * sinf(a)) + o->dx;
    *y = c.y1 + UI_CX - (int)lroundf((float)MOTE_TRACK_R * cosf(a)) + o->dy;
}

static void inval(const mote_out_t *o)
{
    if (o->mode == MOTE_GONE) return;
    int x, y;
    head_xy(o, &x, &y);
    lv_area_t a;
    ui_orb_area(x, y, &o->look, &a);
    lv_obj_invalidate_area(s_obj, &a);
}

static bool same_out(const mote_out_t *a, const mote_out_t *b)
{
    return a->mode == b->mode && a->night == b->night && a->rgb == b->rgb && a->deg == b->deg &&
           a->dx == b->dx && a->dy == b->dy && a->look.core_r == b->look.core_r &&
           a->look.halo_r == b->look.halo_r && a->look.lum == b->look.lum &&
           a->look.halo_pct == b->look.halo_pct;
}

static void set_out(const mote_out_t *o)
{
    if (same_out(o, &s_out)) return;
    inval(&s_out);
    s_out = *o;
    inval(&s_out);
}

/* ───────── drawing ───────── */

static void mote_draw(lv_event_t *e)
{
    if (s_out.mode == MOTE_GONE) return;
    int x, y;
    head_xy(&s_out, &x, &y);
    ui_orb_draw(lv_event_get_layer(e), x, y, &s_out.look, s_out.rgb);
}

void ui_mote_create(lv_obj_t *parent)
{
    s_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s_obj);
    lv_obj_remove_flag(s_obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_obj, UI_SIZE, UI_SIZE);
    lv_obj_set_pos(s_obj, 0, 0);
    lv_obj_add_event_cb(s_obj, mote_draw, LV_EVENT_DRAW_MAIN, NULL);
    memset(&s_m, 0, sizeof s_m);
    memset(&s_out, 0, sizeof s_out);
}

/* ───────── each tick ───────── */

static const char *mode_name(mote_mode_t m)
{
    static const char *const s_names[] = {"gone",  "hover", "cheer", "loop",
                                          "visit", "rest",  "ember"};
    return (unsigned)m < sizeof s_names / sizeof s_names[0] ? s_names[m] : "?";
}

/* The fleet, the local clock and the gate, as mote.c takes them. */
static void read_moment(mote_in_t *in, bool face, int error_deg, const int16_t *work_deg,
                        int n_work)
{
    uint32_t unix_now = theme_now_unix();
    uint32_t local = unix_now + (uint32_t)((int32_t)model_settings()->tz_off_min * 60);
    /* the gate, once as it is and once with reduced motion set aside: the Mote exists only where
     * it could loop (the dock, nothing waiting, the display lit), and is still with reduced motion
     */
    persona_ctx_t c = *ui_living_ctx();
    unsigned allow = persona_allow(PF_MOTE, &c);
    c.fun &= ~(uint32_t)FUN_REDUCED;
    model_counts_t n = model_counts();
    usage_win_t h5;
    in->face = face && unix_now > CLOCK_SET;
    in->may = (persona_allow(PF_MOTE, &c) & PA_LOOP) != 0;
    in->reduced = !(allow & PA_LOOP);
    if (n_work > MOTE_VISIT_MAX) n_work = MOTE_VISIT_MAX;
    in->n_work = (uint8_t)(n_work > 0 && work_deg ? n_work : 0);
    for (int i = 0; i < in->n_work; i++) in->work_deg[i] = work_deg[i];
    in->errors = (uint8_t)(error_deg >= 0 && n.error ? 1 : 0);
    in->error_deg = (int16_t)(error_deg >= 0 ? error_deg : 0);
    in->hour = (uint8_t)(local / 3600 % 24);
    in->usage = gauges_h5(unix_now, &h5) ? h5.pct : 255;
    in->cheers = s_cheers;
    int fin = s_fin_sid ? ui_home_slot_deg(s_fin_sid) : -1;
    in->fin_at = fin >= 0;
    in->fin_deg = (int16_t)(fin >= 0 ? fin : 0);
    in->loops = s_loops;
}

void ui_mote_tick(uint32_t now_ms, bool face, int error_deg, const int16_t *work_deg, int n_work)
{
    if (!s_obj) return;
    mote_in_t in = {0};
    read_moment(&in, face, error_deg, work_deg, n_work);
    mote_mode_t was = s_out.mode;
    mote_out_t o = mote_step(&s_m, &in, now_ms);
    if (o.mode != was) {
        dbg_event("fun", "mote", "\"mode\":%s,\"deg\":%u,\"lum\":%u,\"halo\":%u,\"night\":%d",
                  dbg_q(mode_name(o.mode)), (unsigned)o.deg, (unsigned)o.look.lum,
                  (unsigned)o.look.halo_r, o.night ? 1 : 0);
    }
    if (s_m.fx == MOTE_CHEER && s_m.fin_here && s_m.fx_ms == now_ms) { /* there: the loop */
        dbg_event("fun", "mote_finish", "\"deg\":%u,\"arc\":%d", (unsigned)o.deg,
                  in.fin_at ? (int)in.fin_deg : -1);
    }
    set_out(&o);
}

bool ui_mote_shown(void) { return s_out.mode != MOTE_GONE; }

void ui_mote_loop(void)
{
    if (ui_mote_shown()) s_loops++;
}

/* ───────── finishes ───────── */

static mote_finish_t finish_of(const session_t *s)
{
    const done_sum_t *d = &s->sum;
    return (mote_finish_t){
        .error = s->state == SESS_ERROR,
        .tests = (int8_t)(d->tests_known ? (d->tests_pass ? 1 : 0) : -1),
        .secs = d->secs_known ? (int32_t)d->secs : -1,
        .files = d->files_known ? (int32_t)d->files : -1,
        .lines = d->lines_known ? (int32_t)(d->added + d->removed) : -1,
    };
}

void ui_mote_on_session(const session_t *before, const session_t *now, uint32_t present)
{
    if (!before || now->state != SESS_DONE) return;
    /* the finish itself, or its summary arriving after it (the first one for this finish) */
    bool entered = (present & SF_STATE) && before->state != SESS_DONE;
    bool late = (present & SF_DONE) && before->state == SESS_DONE && !done_known(&before->sum);
    if (!entered && !late) return;
    mote_finish_t f = finish_of(now);
    uint32_t unix_now = theme_now_unix();
    if (mote_finish_green(&f) && unix_now > CLOCK_SET) {
        uint32_t local = unix_now + (uint32_t)((int32_t)model_settings()->tz_off_min * 60);
        uint32_t w = prefs_mote_days(), nw = mote_note_day(w, local / 86400);
        if (nw != w) {
            prefs_set_mote_days(nw);
            dbg_event("fun", "mote_day", "\"days\":%u,\"level\":%u", (unsigned)mote_days(nw),
                      (unsigned)mote_level(mote_days(nw)));
        }
    }
    if (mote_finish_big(&f)) {
        s_cheers++;
        s_fin_sid = now->sid;
    }
}
