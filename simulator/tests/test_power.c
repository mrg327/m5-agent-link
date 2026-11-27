/* power.c timers (POWER.md): idle display-off, face-down, glow, and a stamp taken later than the
 * step's clock. Issue 20261122-f724: a request that arrives while the link drains stamps the
 * activity with a fresh clock, and power_tick() then runs with the step's earlier one; on the watch
 * every request that arrived on a lit display turned it off. */
#include <stdio.h>
#include <stdlib.h>

#include "hal.h"
#include "power.h"

/* ── stubs ── */
static bool g_panel = true;
static bool g_face_down;
void hal_display_set_power(bool on) { g_panel = on; }
hal_power_state_t hal_power_enter(hal_power_state_t state, uint32_t wake_after_s)
{
    (void)wake_after_s;
    /* DEEP refused, as with USB present */
    return state == HAL_POWER_DEEP ? HAL_POWER_ACTIVE : state;
}
hal_wake_t hal_wake_reason(void) { return (hal_wake_t)0; }
static uint32_t g_retained;
uint32_t hal_retained_get(void) { return g_retained; }
void hal_retained_set(uint32_t v) { g_retained = v; }
static bool g_usb; /* on battery unless a check says otherwise */
hal_battery_t hal_battery_read(void)
{ return (hal_battery_t){.percent = 80, .usb_present = g_usb}; }
bool link_connected(void) { return true; }
bool motion_face_down(void) { return g_face_down; }
void preport_save(uint32_t now_ms) { (void)now_ms; }
void preport_unsave(void) {}
static int g_glows;
static uint32_t g_glow_sid;
static bool g_glow_shown;
void ui_glow(uint32_t rgb, uint32_t sid, const char *icon)
{
    (void)rgb, (void)icon;
    g_glows++;
    g_glow_sid = sid;
    g_glow_shown = true;
}
void ui_glow_hide(void) { g_glow_shown = false; }
void dbg_event(const char *cat, const char *ev, const char *fields_fmt, ...)
{ (void)cat, (void)ev, (void)fields_fmt; }

static int g_fail;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail = 1;                                                  \
        }                                                                \
    } while (0)

/* Desk mode: on USB the default 60 s timeout becomes 10 minutes; an explicit setting stays. */
static void desk_mode(void)
{
    g_usb = true;
    power_activity(400000);
    power_activity(400001);
    power_tick(400001 + 60000);
    CHECK(power_display_on());
    power_tick(400001 + 599999);
    CHECK(power_display_on());
    power_tick(400001 + 600000);
    CHECK(!power_display_on());
    /* an explicit setting stays as set, on USB too */
    power_set_idle_timeout_ms(1000);
    power_activity(1500000);
    power_activity(1500001);
    power_tick(1500001 + 1000);
    CHECK(!power_display_on());
    g_usb = false;
}

/* The glow (issue 20261126-0ca0): 3 s, taken over by a second alert, ended by the touch that only
 * reveals. */
static void glow_timers(void)
{
    /* a request on a dark display glows (no idle check while it glows), then goes dark again */
    power_attention(0xff8800, 2, "", 400000);
    CHECK(power_display_on());
    CHECK(power_glowing() && g_glow_shown && g_glow_sid == 2);
    power_tick(400000);
    CHECK(power_display_on());
    power_tick(402999); /* 3 s (issue 20261126-0ca0) */
    CHECK(power_display_on());
    power_tick(403000);
    CHECK(!power_display_on());
    CHECK(!g_glow_shown);

    /* a second alert while it glows takes the glow over and restarts its 3 s */
    power_attention(0xff8800, 2, "", 500000);
    power_tick(502000);
    int glows = g_glows;
    power_attention(0xf0e442, 3, "", 502000);
    CHECK(g_glows == glows + 1 && g_glow_sid == 3);
    power_tick(504999);
    CHECK(power_display_on() && power_glowing());
    power_tick(505000);
    CHECK(!power_display_on());

    /* a touch during the glow only reveals: it is spent, and the glow ends */
    power_attention(0xff8800, 2, "", 600000);
    CHECK(power_activity(600500));
    CHECK(power_display_on() && !power_glowing() && !g_glow_shown);
    /* on a lit display an alert is only activity: no glow */
    glows = g_glows;
    power_attention(0xff8800, 2, "", 601000);
    CHECK(g_glows == glows && !power_glowing());
}

int main(void)
{
    power_init();
    power_set_idle_timeout_ms(60000);
    power_start(1000);

    /* idle: off once the timeout has passed, not before */
    power_tick(60999);
    CHECK(power_display_on());
    power_tick(61000);
    CHECK(!power_display_on());

    /* a touch wakes it (and is spent on waking) */
    CHECK(power_activity(70000));
    CHECK(power_display_on());
    CHECK(!power_activity(70010)); /* already on: the next touch goes to the UI */

    /* f724: a request stamped 5 ms after this step's clock; the step's own power_tick must not read
     * that as 49 days idle */
    power_attention(0xff8800, 2, "", 80005);
    power_tick(80000);
    CHECK(power_display_on());
    CHECK(g_panel);
    /* and the timeout still counts from that stamp */
    power_tick(80005 + 59999);
    CHECK(power_display_on());
    power_tick(80005 + 60000);
    CHECK(!power_display_on());

    /* the same for a touch stamped after the step's clock */
    power_activity(200003);
    power_tick(200000);
    CHECK(power_display_on());

    /* face down: off after 3 s without input, never at once for a late stamp */
    g_face_down = true;
    power_activity(300010);
    power_tick(300000);
    CHECK(power_display_on());
    power_tick(303010);
    CHECK(!power_display_on());
    g_face_down = false;

    glow_timers();

    if (!g_fail) printf("power timers: ok\n");
    desk_mode();

    return g_fail;
}
