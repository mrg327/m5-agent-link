/* Motion for the toys (EXPERIMENTS.md "Toys"): a flick and a still hold, from the 10 Hz gravity
 * samples (motion_pose()). No board emits HAL_MOTION_SHAKE, so a toy computes its own.
 *
 * Both read only the size of a change and the z axis, never the sign or the order of x and y, so
 * they work whatever the board's x/y axes turn out to be (unverified on the watch).
 * - A jolt: the gravity vector moved more than TOY_JOLT_MG between two samples (its length, so a
 *   flick any way counts). One per TOY_JOLT_GAP_MS, so a shake is one throw.
 * - A still hold: face up (z at least TOY_FACE_UP_Z) and still (each sample within TOY_STILL_MG of
 *   the last, summed over the axes), after the watch moved since the toy armed it. A watch left on
 *   the desk never holds by itself.
 *
 * Pure, with an explicit clock. Motion acts only inside a toy, never on ASK. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A flick moves gravity 600 mg or more between two samples; motion_pose() averages the last two,
 * so it shows there at about half that */
#define TOY_JOLT_MG 300
#define TOY_JOLT_GAP_MS 600
#define TOY_STILL_MG 120
#define TOY_FACE_UP_Z 700

typedef struct {
    bool have;  /* a sample was seen */
    bool armed; /* moved since toy_motion_rearm(): a still hold may count */
    int x, y, z;
    uint32_t still_from; /* face up and still since */
    bool still;
    uint32_t jolt_ms;
    bool jolted; /* jolt_ms is valid */
} toy_motion_t;

/** @brief Forget every sample; the next still hold needs a move first. */
void toy_motion_reset(toy_motion_t *m);
/** @brief The next still hold needs a move first (after one was used). */
void toy_motion_rearm(toy_motion_t *m);
/**
 * @brief Feed one gravity sample (mg, screen axes, any signs).
 * @return true when it is a jolt (a flick or the start of a shake).
 */
bool toy_motion_feed(toy_motion_t *m, int x_mg, int y_mg, int z_mg, uint32_t now_ms);
/** @brief How long the watch has been held face up and still, 0 when it is not (or not armed). */
uint32_t toy_motion_still_ms(const toy_motion_t *m, uint32_t now_ms);
