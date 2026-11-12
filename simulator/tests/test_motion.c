/* motion.c against scripted poses at 100 ms per sample (what the watch's IMU task delivers). */
#include <stdio.h>

#include "motion.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

static uint32_t t;
static int raises;

/* hold a pose for `ms` */
static void pose(int x, int y, int z, int ms)
{
    for (int i = 0; i < ms / 100; i++) {
        motion_sample(x, y, z, t);
        if (motion_take_raise()) raises++;
        t += 100;
    }
}

int main(void)
{
    /* desk, face up: nothing happens, however long */
    motion_reset();
    pose(0, 0, 1000, 5000);
    EXPECT(!motion_face_down() && !motion_auto_flipped() && raises == 0, "desk face up is quiet");

    /* a bump on the desk: no raise, pose unchanged */
    pose(150, 80, 1100, 100);
    pose(0, 0, 1000, 3000);
    EXPECT(raises == 0, "a bump on the desk is not a raise");

    /* face down: only after 1.5 s of stillness, and back after 0.3 s */
    pose(0, 0, -1000, 1200);
    EXPECT(!motion_face_down(), "not face down yet at 1.2 s");
    pose(0, 0, -1000, 800);
    EXPECT(motion_face_down(), "face down after 1.5 s still");
    raises = 0;
    pose(0, 0, 1000, 200);
    EXPECT(motion_face_down(), "still face down 0.2 s into the turn back");
    pose(0, 0, 1000, 600);
    EXPECT(!motion_face_down(), "restored once turned back");
    EXPECT(raises == 1, "turning it over to face up wakes the display once");

    /* shaken while face down never settles into silence */
    motion_reset();
    t = 0;
    for (int i = 0; i < 40; i++) pose(i % 2 ? 400 : -400, 0, i % 2 ? -900 : -1000, 100);
    EXPECT(!motion_face_down(), "a face-down watch that keeps moving is not silenced");

    /* hanging then raised to read: one raise; again immediately: none (cooldown, disarmed) */
    motion_reset();
    t = 0;
    raises = 0;
    pose(0, -700, 150, 1000);
    pose(0, -300, 800, 1000);
    EXPECT(raises == 1, "hanging to reading raises once");
    pose(0, -700, 150, 300);
    pose(0, -300, 800, 600);
    EXPECT(raises == 1, "no second raise inside the cooldown/arming time");
    pose(0, -700, 150, 2000);
    pose(0, -300, 800, 600);
    EXPECT(raises == 2, "armed again after hanging");

    /* swinging past the reading angle for less than the dwell is not a raise */
    motion_reset();
    t = 0;
    raises = 0;
    pose(0, -700, 150, 1000);
    pose(0, -300, 800, 100);
    pose(0, -700, 150, 1000);
    EXPECT(raises == 0, "a swing shorter than the dwell is ignored");

    /* a slow tilt with no burst is not a raise */
    motion_reset();
    t = 0;
    raises = 0;
    pose(0, -700, 150, 1000);
    for (int z = 150; z <= 800; z += 50) pose(0, -400, z, 400);
    EXPECT(raises == 0, "a slow tilt is not a raise");

    /* a scripted lift event raises, but not when face down */
    motion_reset();
    t = 0;
    raises = 0;
    motion_lift(t);
    EXPECT(motion_take_raise(), "lift event raises");
    pose(0, 0, -1000, 2000);
    motion_lift(t);
    EXPECT(!motion_take_raise(), "lift event ignored while face down");

    /* orientation: 6 o'clock up for 1.5 s flips; mid-way stays; upright for 1.5 s flips back; flat never changes it */
    motion_reset();
    t = 0;
    pose(0, -1000, 0, 2000);
    EXPECT(!motion_auto_flipped(), "upright stays upright");
    pose(0, 1000, 0, 1000);
    EXPECT(!motion_auto_flipped(), "not flipped before the dwell");
    pose(0, 1000, 0, 1000);
    EXPECT(motion_auto_flipped(), "flipped after 1.5 s the other way up");
    pose(0, 100, 600, 3000);
    EXPECT(motion_auto_flipped(), "mid-way pose keeps it (hysteresis)");
    pose(0, 0, 1000, 3000);
    EXPECT(motion_auto_flipped(), "lying flat keeps it");
    pose(0, -1000, 0, 2000);
    EXPECT(!motion_auto_flipped(), "upright for 1.5 s flips back");
    /* a restless wearer never flips */
    motion_reset();
    t = 0;
    for (int i = 0; i < 60; i++) pose(i % 2 ? 300 : -300, 900, 100, 100);
    EXPECT(!motion_auto_flipped(), "no flip while the watch keeps moving");

    printf(failures ? "motion: %d failure(s)\n" : "motion: ok\n", failures);
    return failures ? 1 : 0;
}
