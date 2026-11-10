/* Gesture recognizer: raw touch points and button edges in, semantic gestures out.
 * Pure C with an explicit clock so it can be unit tested. Thresholds follow FIRMWARE.md §6. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define GESTURE_LONG_PRESS_MS 600
#define GESTURE_SWIPE_MIN_PX 60
#define GESTURE_SWIPE_MAX_MS 400
#define GESTURE_TAP_SLOP_PX 18
/* Button A waits this long after a release to see whether a second press follows. */
#define GESTURE_BTN_DOUBLE_MS 300
/* Vertical drag in the centre (no screen uses it since SCROLL was removed): one G_DRAG per this much travel, once the
 * finger has left the tap slop (the stream starts where it crossed, so a tap never scrolls). */
#define GESTURE_DRAG_STEP_PX 10
/* A swipe released faster than this is a flick (measured over the last GESTURE_FLICK_WINDOW_MS). */
#define GESTURE_FLICK_PX_PER_S 700
#define GESTURE_FLICK_WINDOW_MS 80
#define GESTURE_HIST 8

/* Rim scrub: a drag that starts in the annulus around the centre (233,233) and sweeps an arc. */
#define GESTURE_CX 233
#define GESTURE_CY 233
#define GESTURE_RIM_IN_PX 160
#define GESTURE_RIM_OUT_PX 260      /* past the glass edge (r 233): a finger riding the bezel reads ~r 225-233 */
#define GESTURE_RIM_TRACK_IN_PX 110 /* once started, the finger may drift this far in or out of the annulus */
#define GESTURE_RIM_TRACK_OUT_PX 340
#define GESTURE_SCRUB_START_DEG 10  /* continuous arc before it counts as a scrub */
#define GESTURE_SCRUB_HYST_DEG 5    /* how far past a sector boundary before switching (capped at a third of a sector) */

typedef enum {
    G_PRESS, G_RELEASE,         /* raw finger down/up, for hold-to-confirm (dur_ms on release) */
    G_TAP,                      /* immediate on release; there is no double-tap any more */
    G_DOUBLE_TAP,               /* never produced here; kept so injected debug input keeps its numbering */
    G_LONG_PRESS,               /* finger held still for GESTURE_LONG_PRESS_MS (dur_ms = hold) */
    G_SWIPE_UP, G_SWIPE_DOWN, G_SWIPE_LEFT, G_SWIPE_RIGHT,
    G_BTN_A, G_BTN_B, G_BTN_A_LONG, G_BTN_B_LONG,
    G_LIFT, G_SHAKE,            /* not produced here; the app feeds IMU events through the same path */
    G_SCRUB_START,              /* x = sector under the finger, y = angle in degrees (0 = 12 o'clock, clockwise) */
    G_SCRUB,                    /* the sector changed (hysteresis applied); x = sector, y = angle */
    G_SCRUB_END,                /* finger lifted after a scrub; x = last sector, y = angle */
    G_BTN_A_DOUBLE,             /* two A presses within GESTURE_BTN_DOUBLE_MS; a single A is delayed by that window */
    G_CHORD,                    /* A and B held together; the individual presses are swallowed */
    G_DRAG,                     /* a finger that started inside the rim moved GESTURE_DRAG_STEP_PX more vertically:
                                   x,y = press point, dy = the step (a multiple of the step size, up < 0) */
    G_DRAG_END                  /* finger lifted after at least one G_DRAG (before any swipe): x,y = press point,
                                   dx,dy = whole travel, vy = vertical release speed in px/s (up < 0) */
} gesture_kind_t;

typedef struct {
    gesture_kind_t kind;
    int16_t x, y;               /* press point (touch), or see G_SCRUB* */
    uint32_t dur_ms;            /* G_SCRUB*: arc swept so far, in degrees; drags and swipes: ms since the press */
    int16_t dx, dy;             /* swipes: whole travel; G_DRAG: travel since the previous G_DRAG */
    int16_t vy;                 /* G_DRAG_END: release velocity over GESTURE_FLICK_WINDOW_MS, px/s */
    bool flick;                 /* swipes: released at flick speed */
} gesture_evt_t;

typedef void (*gesture_cb_t)(const gesture_evt_t *evt, void *ctx);

typedef struct {
    bool down, long_fired, consumed, second;
    uint32_t t_down;
} gesture_btn_t;

typedef struct { int16_t x, y; uint32_t t; } gesture_sample_t;

typedef struct {
    gesture_cb_t cb;
    void *ctx;
    bool pressed, moved, long_fired;
    uint32_t t_down;
    int16_t x0, y0, x, y;
    int sectors;                /* rim sectors, set by the UI; < 2 means scrub still starts but never changes sector */
    bool rim_candidate, scrubbing;
    float last_deg, arc_deg;
    int sector;
    bool centre_start, dragging; /* drag stream: press began inside the rim; at least one G_DRAG went out */
    bool drag_armed;             /* the finger left the tap slop: drag steps count from drag_y */
    int16_t drag_y;              /* y at the last G_DRAG */
    gesture_sample_t hist[GESTURE_HIST]; /* recent samples, for the release speed */
    uint8_t hist_n, hist_i;
    gesture_btn_t btn[2];
    bool a_pending;             /* A released once, waiting for a possible second press */
    uint32_t a_release_ms;
} gesture_t;

void gesture_init(gesture_t *g, gesture_cb_t cb, void *ctx);
/* Call on press, on every move sample while down, and with down=false on release. Only the latest
 * point per poll matters, so a slow or coalesced stream is fine. */
void gesture_touch(gesture_t *g, bool down, int x, int y, uint32_t t_ms);
void gesture_button(gesture_t *g, int btn, bool down, uint32_t t_ms);
/* Call regularly (every core_step) so long presses fire while the finger is still down. */
void gesture_tick(gesture_t *g, uint32_t t_ms);
/* Number of equal sectors around the rim (0 = 12 o'clock, clockwise). Safe to call mid-scrub. */
void gesture_set_sectors(gesture_t *g, int n);
bool gesture_scrubbing(const gesture_t *g);
const char *gesture_name(gesture_kind_t k);
/* Angle of a point around the centre, 0..360 degrees from 12 o'clock, clockwise; radius in px. */
float gesture_angle_deg(int x, int y, float *radius);
