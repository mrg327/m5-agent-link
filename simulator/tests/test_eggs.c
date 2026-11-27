/* eggs.c: the easter eggs' triggers and frames (issue 20261126-7770). */
#include <stdio.h>

#include "eggs.h"

static int failures;
#define EXPECT(cond, msg)                                  \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

#define DAY0 1793923200u /* a local midnight */
#define AT(h, m, s) (DAY0 + (h) * 3600u + (m) * 60u + (s))

static void test_slip_times(void)
{
    EXPECT(!eggs_slip_time(AT(13, 36, 59)), "13:36 is not");
    EXPECT(eggs_slip_time(AT(13, 37, 0)) && eggs_slip_time(AT(13, 37, 59)), "13:37 is");
    EXPECT(!eggs_slip_time(AT(13, 38, 0)), "13:38 is not");
    EXPECT(eggs_slip_time(AT(4, 4, 30)), "04:04 is");
    EXPECT(!eggs_slip_time(AT(16, 4, 0)) && !eggs_slip_time(AT(1, 37, 0)), "16:04, 01:37 are not");
}

static void test_slip_once_a_minute(void)
{
    eggs_t e = {0};
    int fired = 0;
    for (uint32_t s = 0; s < 180; s++) { /* 13:36:00 .. 13:38:59, a check a second */
        if (eggs_slip_due(&e, AT(13, 36, 0) + s, true, s * 1000)) fired++;
    }
    EXPECT(fired == 1, "13:37 slips once");
    /* not allowed (a request, reduced motion, no clock on show): not then, but later that minute */
    e = (eggs_t){0};
    EXPECT(!eggs_slip_due(&e, AT(4, 4, 0), false, 0), "not while it may not");
    EXPECT(eggs_slip_due(&e, AT(4, 4, 30), true, 30000), "later in the minute it may");
    EXPECT(!eggs_slip_due(&e, AT(4, 4, 31), true, 31000), "and only once");
    EXPECT(eggs_slip_due(&e, AT(4, 4, 0) + 86400, true, 0), "the next day again");
    e = (eggs_t){0};
    EXPECT(!eggs_slip_due(&e, 13 * 3600 + 37 * 60, true, 0), "no clock: never");
}

static void test_slip_frames(void)
{
    eggs_t e = {0};
    EXPECT(eggs_slip_dx(&e, 0) == 0, "no slip: at rest");
    eggs_slip_due(&e, AT(13, 37, 0), true, 1000);
    EXPECT(eggs_slip_dx(&e, 1000) == EGGS_SLIP_PX, "3 px right");
    EXPECT(eggs_slip_dx(&e, 1089) == EGGS_SLIP_PX, "for one 90 ms frame");
    EXPECT(eggs_slip_dx(&e, 1090) == 0, "then back");
    EXPECT(eggs_slip_dx(&e, 1180) == EGGS_SLIP_PX, "twice");
    EXPECT(eggs_slip_dx(&e, 1270) == 0 && !e.slipping, "and done in 270 ms");
    EXPECT(eggs_slip_dx(&e, 1300) == 0, "stays at rest");
}

static void test_centuries(void)
{
    uint32_t n = 0;
    int c = 0;
    for (int i = 0; i < 1500; i++) {
        n = eggs_count(n);
        c += eggs_is_century(n);
    }
    EXPECT(c == 2, "the 100th and the 1000th only");
    EXPECT(eggs_save(5) && eggs_save(100) && !eggs_save(99) && eggs_save(1000), "saved every 5");
    EXPECT(eggs_count(UINT32_MAX) == UINT32_MAX, "the count stops");
    /* a reboot after 99 finishes: the store holds 95, the count goes on from there, and the
     * century plays once, on the finish that brings it to 100 */
    uint32_t stored = 0;
    n = 0;
    for (int i = 0; i < 99; i++) {
        n = eggs_count(n);
        if (eggs_save(n)) stored = n;
    }
    EXPECT(stored == 95, "95 stored at 99");
    n = stored; /* reboot */
    int fired = 0;
    for (int i = 0; i < 10; i++) {
        n = eggs_count(n);
        fired += eggs_is_century(n);
        if (eggs_save(n)) stored = n;
    }
    EXPECT(fired == 1 && stored >= 100, "it survives a reboot, and plays once");
}

static void test_century_waits(void)
{
    eggs_t e = {0};
    eggs_finished(&e, 99, 0);
    EXPECT(!eggs_century_due(&e, true, 10), "99: nothing");
    eggs_finished(&e, 100, 1000);
    EXPECT(!eggs_century_due(&e, false, 2000), "not while something needs you");
    EXPECT(eggs_century_due(&e, true, 5000), "then, once it may");
    EXPECT(!eggs_century_due(&e, true, 6000), "once");
    eggs_finished(&e, 1000, 10000);
    EXPECT(!eggs_century_due(&e, false, 10000 + EGGS_CENTURY_WAIT_MS), "still waiting");
    EXPECT(!eggs_century_due(&e, true, 10001 + EGGS_CENTURY_WAIT_MS), "lapsed after 10 minutes");
}

static void test_spin(void)
{
    EXPECT(eggs_spin_deg(0) == 0, "starts at rest");
    EXPECT(eggs_spin_deg(EGGS_SPIN_MS / 2) == 180, "half way at half time");
    EXPECT(eggs_spin_deg(EGGS_SPIN_MS / 8) < 10, "eases in");
    EXPECT(eggs_spin_deg(EGGS_SPIN_MS - 100) > 350, "eases out");
    EXPECT(eggs_spin_deg(EGGS_SPIN_MS) == 0, "ends at rest (a full turn)");
    int prev = 0;
    for (uint32_t t = 0; t < EGGS_SPIN_MS; t += 30) {
        int d = eggs_spin_deg(t);
        EXPECT(d >= prev && d <= 360, "one way, one turn");
        prev = d;
    }
}

int main(void)
{
    test_slip_times();
    test_slip_once_a_minute();
    test_slip_frames();
    test_centuries();
    test_century_waits();
    test_spin();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("test_eggs: ok\n");
    return 0;
}
