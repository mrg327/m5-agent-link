/* Gesture recognizer: raw touch points and button edges in, semantic gestures out.
 * Pure C with an explicit clock so it can be unit tested. Thresholds follow FIRMWARE.md §6. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define GESTURE_DOUBLE_TAP_MS 300
#define GESTURE_LONG_PRESS_MS 600
#define GESTURE_SWIPE_MIN_PX 60
#define GESTURE_SWIPE_MAX_MS 400
#define GESTURE_TAP_SLOP_PX 12

typedef enum {
    G_PRESS, G_RELEASE,         /* raw finger down/up, for hold-to-confirm (dur_ms on release) */
    G_TAP, G_DOUBLE_TAP,        /* the second tap of a pair is G_DOUBLE_TAP only (no extra G_TAP) */
    G_LONG_PRESS,               /* finger held still for GESTURE_LONG_PRESS_MS (dur_ms = hold) */
    G_SWIPE_UP, G_SWIPE_DOWN, G_SWIPE_LEFT, G_SWIPE_RIGHT,
    G_BTN_A, G_BTN_B, G_BTN_A_LONG, G_BTN_B_LONG,
    G_LIFT, G_SHAKE             /* not produced here; the app feeds IMU events through the same path */
} gesture_kind_t;

typedef struct {
    gesture_kind_t kind;
    int16_t x, y;
    uint32_t dur_ms;
} gesture_evt_t;

typedef void (*gesture_cb_t)(const gesture_evt_t *evt, void *ctx);

typedef struct {
    gesture_cb_t cb;
    void *ctx;
    bool pressed, moved, long_fired;
    uint32_t t_down;
    int16_t x0, y0, x, y;
    bool have_tap;
    uint32_t t_tap_up;
    int16_t x_tap, y_tap;
    struct { bool down, long_fired; uint32_t t_down; } btn[2];
} gesture_t;

void gesture_init(gesture_t *g, gesture_cb_t cb, void *ctx);
/* Call on press, on every move sample while down, and with down=false on release. */
void gesture_touch(gesture_t *g, bool down, int x, int y, uint32_t t_ms);
void gesture_button(gesture_t *g, int btn, bool down, uint32_t t_ms);
/* Call regularly (every core_step) so long presses fire while the finger is still down. */
void gesture_tick(gesture_t *g, uint32_t t_ms);
const char *gesture_name(gesture_kind_t k);
