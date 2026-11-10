#include "gesture.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void emit(gesture_t *g, gesture_kind_t kind, int x, int y, uint32_t dur)
{
    gesture_evt_t e = {.kind = kind, .x = (int16_t)x, .y = (int16_t)y, .dur_ms = dur};
    g->cb(&e, g->ctx);
}

void gesture_init(gesture_t *g, gesture_cb_t cb, void *ctx)
{
    memset(g, 0, sizeof *g);
    g->cb = cb;
    g->ctx = ctx;
}

const char *gesture_name(gesture_kind_t k)
{
    static const char *const names[] = {
        "press", "release", "tap", "double_tap", "long_press", "swipe_up", "swipe_down",
        "swipe_left", "swipe_right", "btn_a", "btn_b", "btn_a_long", "btn_b_long", "lift", "shake",
        "scrub_start", "scrub", "scrub_end", "btn_a_double", "chord", "drag", "drag_end"};
    return (unsigned)k < sizeof names / sizeof *names ? names[k] : "?";
}

float gesture_angle_deg(int x, int y, float *radius)
{
    float dx = (float)(x - GESTURE_CX), dy = (float)(y - GESTURE_CY);
    if (radius) *radius = sqrtf(dx * dx + dy * dy);
    float deg = atan2f(dy, dx) * 180.0f / (float)M_PI + 90.0f;
    return deg < 0 ? deg + 360.0f : deg >= 360.0f ? deg - 360.0f : deg;
}

static float wrap180(float d)
{
    while (d > 180.0f) d -= 360.0f;
    while (d <= -180.0f) d += 360.0f;
    return d;
}

static int sector_of(const gesture_t *g, float deg)
{
    if (g->sectors < 2) return 0;
    int s = (int)(deg * (float)g->sectors / 360.0f);
    return s >= g->sectors ? g->sectors - 1 : s;
}

void gesture_set_sectors(gesture_t *g, int n)
{
    g->sectors = n;
    if (g->scrubbing) g->sector = sector_of(g, g->last_deg);
}

bool gesture_scrubbing(const gesture_t *g) { return g->scrubbing; }

/* Moves to another sector only once the finger is a few degrees past the boundary, so a finger
 * resting on an edge does not flutter between two segments. The margin is fixed in degrees rather
 * than a fraction of the sector: with 2-4 sessions a fractional margin is 40-60 degrees of lag. */
static void update_sector(gesture_t *g, float deg)
{
    if (g->sectors < 2) return;
    float width = 360.0f / (float)g->sectors;
    float hyst = (float)GESTURE_SCRUB_HYST_DEG < width / 3.0f ? (float)GESTURE_SCRUB_HYST_DEG : width / 3.0f;
    float centre = ((float)g->sector + 0.5f) * width;
    if (fabsf(wrap180(deg - centre)) <= width * 0.5f + hyst) return;
    int next = sector_of(g, deg);
    if (next == g->sector) return;
    g->sector = next;
    emit(g, G_SCRUB, next, (int)deg, (uint32_t)fabsf(g->arc_deg));
}

static void rim_track(gesture_t *g, int x, int y)
{
    float r;
    float deg = gesture_angle_deg(x, y, &r);
    if (r < GESTURE_RIM_TRACK_IN_PX || r > GESTURE_RIM_TRACK_OUT_PX) {
        if (!g->scrubbing) g->rim_candidate = false;
        return;
    }
    g->arc_deg += wrap180(deg - g->last_deg);
    g->last_deg = deg;
    if (g->scrubbing) {
        update_sector(g, deg);
    } else if (fabsf(g->arc_deg) >= GESTURE_SCRUB_START_DEG) {
        g->scrubbing = true;
        g->sector = sector_of(g, deg);
        emit(g, G_SCRUB_START, g->sector, (int)deg, (uint32_t)fabsf(g->arc_deg));
    }
}

static void hist_push(gesture_t *g, int x, int y, uint32_t t)
{
    g->hist[g->hist_i] = (gesture_sample_t){(int16_t)x, (int16_t)y, t};
    g->hist_i = (uint8_t)((g->hist_i + 1) % GESTURE_HIST);
    if (g->hist_n < GESTURE_HIST) g->hist_n++;
}

/* Velocity over the oldest sample still inside the flick window, in px/s; 0 if the finger rested. */
static void release_vel(const gesture_t *g, uint32_t t_ms, float *vx, float *vy)
{
    int best = -1;
    *vx = *vy = 0;
    for (int k = 0; k < g->hist_n; k++) {
        int i = (g->hist_i - 1 - k + 2 * GESTURE_HIST) % GESTURE_HIST;
        if (t_ms - g->hist[i].t > GESTURE_FLICK_WINDOW_MS) break;
        best = i;
    }
    if (best < 0) return;
    uint32_t dt = t_ms - g->hist[best].t;
    if (dt == 0) return;
    *vx = (float)(g->x - g->hist[best].x) * 1000.0f / (float)dt;
    *vy = (float)(g->y - g->hist[best].y) * 1000.0f / (float)dt;
}

/* Drag stream (SCROLL): every GESTURE_DRAG_STEP_PX of vertical travel from a press inside the rim,
 * counted from where the finger left the tap slop. */
static void drag_track(gesture_t *g, int y, uint32_t t_ms)
{
    if (!g->drag_armed) {
        if (!g->moved) return;
        g->drag_armed = true;
        g->drag_y = (int16_t)y;
        return;
    }
    int d = y - g->drag_y;
    if (abs(d) < GESTURE_DRAG_STEP_PX) return;
    int step = d / GESTURE_DRAG_STEP_PX * GESTURE_DRAG_STEP_PX;
    g->drag_y = (int16_t)(g->drag_y + step);
    g->dragging = true;
    gesture_evt_t e = {.kind = G_DRAG, .x = g->x0, .y = g->y0, .dur_ms = t_ms - g->t_down, .dx = 0, .dy = (int16_t)step};
    g->cb(&e, g->ctx);
}

static void finish_touch(gesture_t *g, uint32_t t_ms)
{
    uint32_t dur = t_ms - g->t_down;
    g->pressed = false;
    emit(g, G_RELEASE, g->x, g->y, dur);
    if (g->scrubbing) {
        g->scrubbing = g->rim_candidate = false;
        emit(g, G_SCRUB_END, g->sector, (int)g->last_deg, (uint32_t)fabsf(g->arc_deg));
        return;
    }
    float vx, vy;
    release_vel(g, t_ms, &vx, &vy);
    if (g->dragging) {
        float v = vy < -32000.0f ? -32000.0f : vy > 32000.0f ? 32000.0f : vy;
        gesture_evt_t e = {.kind = G_DRAG_END, .x = g->x0, .y = g->y0, .dur_ms = dur, .dx = (int16_t)(g->x - g->x0),
                           .dy = (int16_t)(g->y - g->y0), .vy = (int16_t)v};
        g->cb(&e, g->ctx);
    }
    if (g->long_fired) return;

    if (g->moved) {
        int dx = g->x - g->x0, dy = g->y - g->y0;
        if (dur > GESTURE_SWIPE_MAX_MS || (abs(dx) < GESTURE_SWIPE_MIN_PX && abs(dy) < GESTURE_SWIPE_MIN_PX)) return;
        gesture_kind_t k = abs(dx) > abs(dy) ? (dx > 0 ? G_SWIPE_RIGHT : G_SWIPE_LEFT)
                                             : (dy > 0 ? G_SWIPE_DOWN : G_SWIPE_UP);
        gesture_evt_t e = {.kind = k, .x = g->x0, .y = g->y0, .dur_ms = dur, .dx = (int16_t)dx, .dy = (int16_t)dy,
                           .flick = sqrtf(vx * vx + vy * vy) >= GESTURE_FLICK_PX_PER_S};
        g->cb(&e, g->ctx);
        return;
    }
    emit(g, G_TAP, g->x0, g->y0, dur);
}

void gesture_touch(gesture_t *g, bool down, int x, int y, uint32_t t_ms)
{
    if (down && !g->pressed) {
        g->pressed = true;
        g->moved = g->long_fired = g->scrubbing = false;
        g->t_down = t_ms;
        g->x0 = g->x = (int16_t)x;
        g->y0 = g->y = (int16_t)y;
        float r;
        g->last_deg = gesture_angle_deg(x, y, &r);
        g->arc_deg = 0;
        g->rim_candidate = r >= GESTURE_RIM_IN_PX && r <= GESTURE_RIM_OUT_PX;
        g->centre_start = r < GESTURE_RIM_IN_PX;
        g->dragging = g->drag_armed = false;
        g->drag_y = (int16_t)y;
        g->hist_n = g->hist_i = 0;
        hist_push(g, x, y, t_ms);
        emit(g, G_PRESS, x, y, 0);
    } else if (down) {
        g->x = (int16_t)x;
        g->y = (int16_t)y;
        hist_push(g, x, y, t_ms);
        if (abs(x - g->x0) > GESTURE_TAP_SLOP_PX || abs(y - g->y0) > GESTURE_TAP_SLOP_PX) g->moved = true;
        if (g->rim_candidate) rim_track(g, x, y);
        else if (g->centre_start) drag_track(g, y, t_ms);
    } else if (g->pressed) {
        finish_touch(g, t_ms);
    }
}

void gesture_button(gesture_t *g, int btn, bool down, uint32_t t_ms)
{
    if (btn < 0 || btn > 1) return;
    gesture_btn_t *b = &g->btn[btn], *o = &g->btn[1 - btn];
    if (down) {
        if (b->down) return;
        b->down = true;
        b->long_fired = b->consumed = b->second = false;
        b->t_down = t_ms;
        if (o->down && !o->long_fired && !o->consumed) {
            b->consumed = o->consumed = true;
            g->a_pending = false;
            emit(g, G_CHORD, 0, 0, 0);
            return;
        }
        if (btn == 0 && g->a_pending) {
            g->a_pending = false;
            b->second = t_ms - g->a_release_ms <= GESTURE_BTN_DOUBLE_MS;
        }
    } else if (b->down) {
        b->down = false;
        if (b->consumed || b->long_fired) return;
        uint32_t dur = t_ms - b->t_down;
        if (btn == 1) {
            emit(g, G_BTN_B, 0, 0, dur);
        } else if (b->second) {
            emit(g, G_BTN_A_DOUBLE, 0, 0, dur);
        } else {
            g->a_pending = true;
            g->a_release_ms = t_ms;
        }
    }
}

void gesture_tick(gesture_t *g, uint32_t t_ms)
{
    if (g->pressed && !g->moved && !g->long_fired && t_ms - g->t_down >= GESTURE_LONG_PRESS_MS) {
        g->long_fired = true;
        emit(g, G_LONG_PRESS, g->x0, g->y0, GESTURE_LONG_PRESS_MS);
    }
    if (g->a_pending && t_ms - g->a_release_ms >= GESTURE_BTN_DOUBLE_MS) {
        g->a_pending = false;
        emit(g, G_BTN_A, 0, 0, 0);
    }
    for (int b = 0; b < 2; b++) {
        if (g->btn[b].down && !g->btn[b].long_fired && !g->btn[b].consumed &&
            t_ms - g->btn[b].t_down >= GESTURE_LONG_PRESS_MS) {
            g->btn[b].long_fired = true;
            emit(g, b ? G_BTN_B_LONG : G_BTN_A_LONG, 0, 0, GESTURE_LONG_PRESS_MS);
        }
    }
}
