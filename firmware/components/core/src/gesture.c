#include "gesture.h"

#include <stdlib.h>
#include <string.h>

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
        "swipe_left", "swipe_right", "btn_a", "btn_b", "btn_a_long", "btn_b_long", "lift", "shake"};
    return (unsigned)k < sizeof names / sizeof *names ? names[k] : "?";
}

static void finish_touch(gesture_t *g, uint32_t t_ms)
{
    uint32_t dur = t_ms - g->t_down;
    g->pressed = false;
    emit(g, G_RELEASE, g->x, g->y, dur);
    if (g->long_fired) return;

    if (g->moved) {
        int dx = g->x - g->x0, dy = g->y - g->y0;
        if (dur > GESTURE_SWIPE_MAX_MS || (abs(dx) < GESTURE_SWIPE_MIN_PX && abs(dy) < GESTURE_SWIPE_MIN_PX)) return;
        gesture_kind_t k = abs(dx) > abs(dy) ? (dx > 0 ? G_SWIPE_RIGHT : G_SWIPE_LEFT)
                                             : (dy > 0 ? G_SWIPE_DOWN : G_SWIPE_UP);
        emit(g, k, g->x0, g->y0, dur);
        g->have_tap = false;
        return;
    }

    bool second = g->have_tap && (g->t_down - g->t_tap_up) <= GESTURE_DOUBLE_TAP_MS &&
                  abs(g->x0 - g->x_tap) <= 40 && abs(g->y0 - g->y_tap) <= 40;
    if (second) {
        g->have_tap = false;
        emit(g, G_DOUBLE_TAP, g->x0, g->y0, dur);
    } else {
        g->have_tap = true;
        g->t_tap_up = t_ms;
        g->x_tap = g->x0;
        g->y_tap = g->y0;
        emit(g, G_TAP, g->x0, g->y0, dur);
    }
}

void gesture_touch(gesture_t *g, bool down, int x, int y, uint32_t t_ms)
{
    if (down && !g->pressed) {
        g->pressed = true;
        g->moved = g->long_fired = false;
        g->t_down = t_ms;
        g->x0 = g->x = (int16_t)x;
        g->y0 = g->y = (int16_t)y;
        emit(g, G_PRESS, x, y, 0);
    } else if (down) {
        g->x = (int16_t)x;
        g->y = (int16_t)y;
        if (abs(x - g->x0) > GESTURE_TAP_SLOP_PX || abs(y - g->y0) > GESTURE_TAP_SLOP_PX) g->moved = true;
    } else if (g->pressed) {
        finish_touch(g, t_ms);
    }
}

void gesture_button(gesture_t *g, int btn, bool down, uint32_t t_ms)
{
    if (btn < 0 || btn > 1) return;
    if (down) {
        g->btn[btn].down = true;
        g->btn[btn].long_fired = false;
        g->btn[btn].t_down = t_ms;
    } else if (g->btn[btn].down) {
        g->btn[btn].down = false;
        if (!g->btn[btn].long_fired)
            emit(g, btn ? G_BTN_B : G_BTN_A, 0, 0, t_ms - g->btn[btn].t_down);
    }
}

void gesture_tick(gesture_t *g, uint32_t t_ms)
{
    if (g->pressed && !g->moved && !g->long_fired && t_ms - g->t_down >= GESTURE_LONG_PRESS_MS) {
        g->long_fired = true;
        g->have_tap = false;
        emit(g, G_LONG_PRESS, g->x0, g->y0, GESTURE_LONG_PRESS_MS);
    }
    for (int b = 0; b < 2; b++) {
        if (g->btn[b].down && !g->btn[b].long_fired && t_ms - g->btn[b].t_down >= GESTURE_LONG_PRESS_MS) {
            g->btn[b].long_fired = true;
            emit(g, b ? G_BTN_B_LONG : G_BTN_A_LONG, 0, 0, GESTURE_LONG_PRESS_MS);
        }
    }
}
