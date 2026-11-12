/* Wrist and desk motion from the accelerometer's gravity samples (docs/design/MOTION.md). Pure logic with an
 * explicit clock, so the simulator and the unit test drive it with scripted poses.
 *
 * What motion may do: wake the display, silence alerts while the watch lies face down, turn the picture
 * by 180 degrees. It never answers, selects, stops, sends or dismisses anything. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* One gravity sample (the accelerometer reading, milli-g, x right / y down the screen / z out of the glass),
 * about every 100 ms. `now_ms` is the app clock. */
void motion_sample(int x_mg, int y_mg, int z_mg, uint32_t now_ms);
/* A lift reported as an event by the hardware or a script: treated as a raise. */
void motion_lift(uint32_t now_ms);
/* Forget everything (tests). */
void motion_reset(void);

/* True once per raise of the wrist (or lift event), then false until the next one. */
bool motion_take_raise(void);
/* True while the watch lies face down and still (entered after FACE_DOWN_MS, left quickly on turning back). */
bool motion_face_down(void);
/* What the automatic orientation wants, with its dwell and hysteresis applied: true = UI turned 180 degrees. */
bool motion_auto_flipped(void);
/* The last pose seen: samples so far, and the filtered gravity vector (mg). */
bool motion_pose(int *x_mg, int *y_mg, int *z_mg);

/* Thresholds (mg unless noted); see MOTION.md. */
#define MOTION_FACE_DOWN_Z (-800)     /* z at or below: face down */
#define MOTION_FACE_UP_Z (-500)       /* z above: no longer face down */
#define MOTION_FACE_DOWN_MS 1500      /* still and face down this long before it counts */
#define MOTION_FACE_UP_MS 300         /* turned back this long before alerts return */
#define MOTION_STILL_MG 120           /* sample-to-sample change below which the watch counts as still */
#define MOTION_ARM_Z 250              /* below: the screen is not toward the viewer (hanging, pocket, face down) */
#define MOTION_VIEW_Z 550             /* at or above: the screen faces up towards the viewer */
#define MOTION_VIEW_HOLD_Z 450        /* a raise in progress is dropped when z falls below */
#define MOTION_RAISE_DWELL_MS 200     /* the viewing pose must hold this long */
#define MOTION_RAISE_BURST_MG 200     /* a change this big between samples must have happened ... */
#define MOTION_RAISE_BURST_MS 1500    /* ... within this long before the raise */
#define MOTION_RAISE_COOLDOWN_MS 1500 /* no second raise sooner than this */
#define MOTION_FLIP_Y 650             /* y at or above (6 o'clock up): rotate the UI */
#define MOTION_UNFLIP_Y (-350)        /* y at or below: back to upright */
#define MOTION_FLIP_FLAT_Z 800        /* |z| above this is lying flat: orientation unchanged */
#define MOTION_FLIP_MS 1500           /* still in the new orientation this long before the UI turns */
