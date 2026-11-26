/* The Mote on the idle face: see ui_mote.h. A step that moves the light by a degree repaints the
 * boxes round the head and the tail's three joints, old and new (a few hundred px); a change of
 * look (mode, brightness, tail, a jump) repaints the whole light, in 10° chunks of its track. */
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
#include "ui_theme.h"

#define WARM_RGB 0xFFE2B8  /* warm white */
#define AMBER_RGB 0xE69F00 /* the night */
#define TAIL_SEGS 3
#define BOX_HALF (MOTE_TAIL_W / 2 + 3) /* a joint's box: the stroke, anti-aliasing, a margin */
#define CHUNK_DEG 10
#define CLOCK_SET 100000u /* the RTC reads less until it is set */

static lv_obj_t *s_obj;
static mote_t s_m;
static mote_out_t s_out; /* what is drawn */
static uint32_t s_cheers, s_loops;
static uint32_t s_unix, s_unix_ms; /* the last RTC second seen and when: the sub-second clock */

static const uint8_t s_seg_pct[TAIL_SEGS] = {100, 55, 25};

/* ───────── geometry, in screen coordinates ───────── */

static void centre(int *cx, int *cy)
{
    lv_area_t c;
    lv_obj_get_coords(s_obj, &c);
    *cx = c.x1 + UI_CX;
    *cy = c.y1 + UI_CX;
}

static void on_track(int deg, int *x, int *y)
{
    int cx, cy;
    centre(&cx, &cy);
    float a = (float)deg * 0.017453292f;
    *x = cx + (int)lroundf((float)MOTE_TRACK_R * sinf(a));
    *y = cy - (int)lroundf((float)MOTE_TRACK_R * cosf(a));
}

static void head_xy(const mote_out_t *o, int *x, int *y)
{
    on_track(o->deg, x, y);
    *x += o->dx;
    *y += o->dy;
}

/* The tail's k-th joint: 0 at the head, TAIL_SEGS at its end. */
static int joint(const mote_out_t *o, int k) { return (int)o->deg - o->tail_deg * k / TAIL_SEGS; }

static void inval_box(int x, int y, int half)
{
    lv_area_t a = {x - half, y - half, x + half, y + half};
    lv_obj_invalidate_area(s_obj, &a);
}

/* The track between a0 and a1 (degrees, a0 < a1), in chunks so no box grows large. */
static void inval_track(int a0, int a1)
{
    for (int c = a0; c < a1; c += CHUNK_DEG) {
        int e = c + CHUNK_DEG < a1 ? c + CHUNK_DEG : a1;
        int x0, y0, x1, y1;
        on_track(c, &x0, &y0);
        on_track(e, &x1, &y1);
        lv_area_t a = {LV_MIN(x0, x1) - BOX_HALF, LV_MIN(y0, y1) - BOX_HALF,
                       LV_MAX(x0, x1) + BOX_HALF, LV_MAX(y0, y1) + BOX_HALF};
        lv_obj_invalidate_area(s_obj, &a);
    }
}

/* Everything a look covers; joints_only: the head and the joints (a one-degree step). */
static void inval(const mote_out_t *o, bool joints_only)
{
    if (o->mode == MOTE_GONE) return;
    int x, y;
    head_xy(o, &x, &y);
    inval_box(x, y, o->size / 2 + 2);
    if (!o->tail_deg) return;
    if (!joints_only) {
        inval_track(joint(o, TAIL_SEGS), (int)o->deg);
        return;
    }
    for (int k = 1; k <= TAIL_SEGS; k++) {
        on_track(joint(o, k), &x, &y);
        inval_box(x, y, BOX_HALF);
    }
}

static int deg_step(int a, int b)
{
    int d = ((a - b) % 360 + 360) % 360;
    return d > 180 ? 360 - d : d;
}

static void set_out(const mote_out_t *o)
{
    if (!memcmp(o, &s_out, sizeof *o)) return;
    mote_out_t was = s_out;
    was.deg = o->deg;
    bool step = o->mode == s_out.mode && deg_step(o->deg, s_out.deg) <= 1 &&
                !memcmp(o, &was, sizeof *o); /* only the angle moved, by a degree */
    inval(&s_out, step);
    s_out = *o;
    inval(&s_out, step);
}

/* ───────── drawing ───────── */

static void mote_draw(lv_event_t *e)
{
    const mote_out_t *o = &s_out;
    if (o->mode == MOTE_GONE) return;
    lv_layer_t *l = lv_event_get_layer(e);
    uint32_t rgb = o->amber ? AMBER_RGB : WARM_RGB;
    int cx, cy;
    centre(&cx, &cy);
    for (int k = 0; k < TAIL_SEGS && o->tail_deg; k++) {
        int a1 = joint(o, k), a0 = joint(o, k + 1);
        if (a1 <= a0) continue;
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = cx;
        d.center.y = cy;
        d.radius = (uint16_t)(MOTE_TRACK_R + MOTE_TAIL_W / 2);
        d.width = MOTE_TAIL_W;
        d.rounded = k == TAIL_SEGS - 1; /* the end tapers; the joints meet flat */
        d.color = theme_dim(rgb, o->lum * s_seg_pct[k] / 100);
        d.start_angle = (lv_value_precise_t)(((a0 + 270) % 360 + 360) % 360);
        d.end_angle = (lv_value_precise_t)(((a1 + 270) % 360 + 360) % 360);
        lv_draw_arc(l, &d);
    }
    int x, y, r = o->size / 2;
    head_xy(o, &x, &y);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = theme_dim(rgb, o->lum);
    lv_area_t a = {x - r, y - r, x + r - 1, y + r - 1};
    lv_draw_rect(l, &d, &a);
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
    static const char *const s_names[] = {"gone", "hover",  "cheer", "loop",
                                          "lap",  "settle", "ember"};
    return (unsigned)m < sizeof s_names / sizeof s_names[0] ? s_names[m] : "?";
}

/* The fleet, the local clock and the gate, as mote.c takes them. */
static void read_moment(mote_in_t *in, uint32_t now, bool face, int error_deg)
{
    uint32_t unix_now = theme_now_unix();
    if (unix_now != s_unix) {
        s_unix = unix_now;
        s_unix_ms = now;
    }
    uint32_t local = unix_now + (uint32_t)((int32_t)model_settings()->tz_off_min * 60);
    uint32_t sub = now - s_unix_ms > 999 ? 999 : now - s_unix_ms;
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
    in->working = (uint8_t)(n.running > 255 ? 255 : n.running);
    in->errors = (uint8_t)(error_deg >= 0 && n.error ? 1 : 0);
    in->error_deg = (int16_t)(error_deg >= 0 ? error_deg : 0);
    in->minute_ms = local % 60 * 1000 + sub;
    in->hour = (uint8_t)(local / 3600 % 24);
    in->usage = gauges_h5(unix_now, &h5) ? h5.pct : 255;
    in->level = mote_level(mote_days(prefs_mote_days()));
    in->cheers = s_cheers;
    in->loops = s_loops;
}

void ui_mote_tick(uint32_t now_ms, bool face, int error_deg)
{
    if (!s_obj) return;
    mote_in_t in;
    read_moment(&in, now_ms, face, error_deg);
    mote_mode_t was = s_out.mode;
    mote_out_t o = mote_step(&s_m, &in, now_ms);
    if (o.mode != was) {
        dbg_event("fun", "mote", "\"mode\":%s,\"deg\":%u,\"tail\":%u,\"lum\":%u,\"amber\":%d",
                  dbg_q(mode_name(o.mode)), (unsigned)o.deg, (unsigned)o.tail_deg, (unsigned)o.lum,
                  o.amber ? 1 : 0);
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
    if (mote_finish_big(&f)) s_cheers++;
}
