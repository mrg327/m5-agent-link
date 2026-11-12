#include "motion.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    bool have;
    int x, y, z;       /* last sample */
    int fx, fy, fz;    /* mean of the last two samples: the pose */
    uint32_t last_burst_ms;
    bool burst_seen;
    uint32_t last_big_ms; /* last sample-to-sample change over STILL_MG */
    bool big_seen;

    bool face_down;
    uint32_t down_since, up_since;
    bool down_cand, up_cand;

    bool armed;
    uint32_t arm_since;
    bool arm_cand;
    uint32_t view_since;
    bool view_cand;
    uint32_t cooldown_until;
    bool cooldown;
    bool raise;

    bool flipped;
    uint32_t flip_since;
    int flip_cand; /* 0 none, 1 towards flipped, 2 towards upright */
} motion_t;

static motion_t s;

void motion_reset(void) { memset(&s, 0, sizeof s); }

static bool after(uint32_t now, uint32_t since, uint32_t ms) { return (int32_t)(now - since) >= (int32_t)ms; }

/* A change over STILL_MG after `since`: the watch moved while a pose was being timed, so the timing starts over. */
static bool moved_since(uint32_t since) { return s.big_seen && (int32_t)(s.last_big_ms - since) > 0; }

static void update_face_down(uint32_t now)
{
    if (!s.face_down) {
        if (s.fz <= MOTION_FACE_DOWN_Z) {
            if (!s.down_cand || moved_since(s.down_since)) {
                s.down_cand = true;
                s.down_since = now;
            }
            if (after(now, s.down_since, MOTION_FACE_DOWN_MS)) {
                s.face_down = true;
                s.up_cand = false;
            }
        } else {
            s.down_cand = false;
        }
    } else if (s.fz > MOTION_FACE_UP_Z) {
        if (!s.up_cand) {
            s.up_cand = true;
            s.up_since = now;
        }
        if (after(now, s.up_since, MOTION_FACE_UP_MS)) {
            s.face_down = false;
            s.down_cand = false;
        }
    } else {
        s.up_cand = false;
    }
}

static void update_raise(uint32_t now)
{
    /* Armed once the screen has been turned away from the viewer for a moment. */
    if (!s.armed) {
        if (s.fz < MOTION_ARM_Z) {
            if (!s.arm_cand) {
                s.arm_cand = true;
                s.arm_since = now;
            }
            if (after(now, s.arm_since, 300)) {
                s.armed = true;
                s.arm_cand = false;
            }
        } else {
            s.arm_cand = false;
        }
        return;
    }
    if (s.fz >= MOTION_VIEW_Z || (s.view_cand && s.fz >= MOTION_VIEW_HOLD_Z)) {
        if (!s.view_cand) {
            s.view_cand = true;
            s.view_since = now;
        }
        bool burst = s.burst_seen && !after(now, s.last_burst_ms, MOTION_RAISE_BURST_MS);
        bool cooled = !s.cooldown || after(now, s.cooldown_until, 0);
        if (after(now, s.view_since, MOTION_RAISE_DWELL_MS) && burst && cooled && !s.face_down) {
            s.raise = true;
            s.armed = false;
            s.view_cand = false;
            s.cooldown = true;
            s.cooldown_until = now + MOTION_RAISE_COOLDOWN_MS;
        }
    } else {
        s.view_cand = false;
    }
}

static void update_flip(uint32_t now)
{
    int want = 0;
    if (abs(s.fz) <= MOTION_FLIP_FLAT_Z) {
        if (!s.flipped && s.fy >= MOTION_FLIP_Y) want = 1;
        else if (s.flipped && s.fy <= MOTION_UNFLIP_Y) want = 2;
    }
    if (!want) {
        s.flip_cand = 0;
        return;
    }
    if (s.flip_cand != want || moved_since(s.flip_since)) {
        s.flip_cand = want;
        s.flip_since = now;
    }
    if (after(now, s.flip_since, MOTION_FLIP_MS)) {
        s.flipped = want == 1;
        s.flip_cand = 0;
    }
}

void motion_sample(int x, int y, int z, uint32_t now)
{
    if (!s.have) {
        s.fx = x;
        s.fy = y;
        s.fz = z;
    } else {
        int d = abs(x - s.x) + abs(y - s.y) + abs(z - s.z);
        if (d > MOTION_STILL_MG) {
            s.last_big_ms = now;
            s.big_seen = true;
        }
        if (d > MOTION_RAISE_BURST_MG) {
            s.last_burst_ms = now;
            s.burst_seen = true;
        }
        s.fx = (x + s.x) / 2;
        s.fy = (y + s.y) / 2;
        s.fz = (z + s.z) / 2;
    }
    s.x = x;
    s.y = y;
    s.z = z;
    s.have = true;
    update_face_down(now);
    update_raise(now);
    update_flip(now);
}

void motion_lift(uint32_t now)
{
    if (s.face_down) return;
    s.raise = true;
    s.cooldown = true;
    s.cooldown_until = now + MOTION_RAISE_COOLDOWN_MS;
}

bool motion_take_raise(void)
{
    bool r = s.raise;
    s.raise = false;
    return r;
}

bool motion_face_down(void) { return s.face_down; }
bool motion_auto_flipped(void) { return s.flipped; }

bool motion_pose(int *x, int *y, int *z)
{
    if (x) *x = s.fx;
    if (y) *y = s.fy;
    if (z) *z = s.fz;
    return s.have;
}
