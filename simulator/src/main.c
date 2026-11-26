/* Desktop simulator: the same core as the firmware, on SDL2 + a WebSocket link to the daemon. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core.h"
#include "sim.h"

static uint64_t s_cpu_ns;

/* CPU time spent inside core_step() (LVGL render + core), for the scripts' redraw reports. */
uint64_t sim_core_cpu_us(void) { return s_cpu_ns / 1000; }

static uint32_t timed_core_step(void)
{
    struct timespec a, b;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &a);
    uint32_t wait = core_step();
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &b);
    s_cpu_ns +=
        (uint64_t)(b.tv_sec - a.tv_sec) * 1000000000u + (uint64_t)b.tv_nsec - (uint64_t)a.tv_nsec;
    return wait;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--headless] [--no-ws] [--script FILE] [--idle-off SECONDS] [--battery] "
            "[--deep-after SECONDS] [--pref KEY=VALUE]\n"
            "          [--wake timer|button [--tries N] [--slept S]]\n"
            "  --headless   no window (framebuffer still rendered, screenshots work)\n"
            "  --no-ws      do not connect to ws://127.0.0.1:7878 (scripts can inject frames)\n"
            "  --script F   run scripted input; exits when done (code 1 if an expect failed)\n"
            "  --idle-off S turn the display off after S seconds without input\n"
            "  --battery    no USB power, so DEEP is allowed (with no link)\n"
            "  --deep-after S  display-off seconds before DEEP (default 600)\n"
            "  --wake W     boot as if a DEEP ended by W; --tries N = retained retry count\n"
            "  --slept S    with --wake: that DEEP lasted S seconds (power report)\n"
            "  --pref K=V   a stored preference before boot, as NVS would hold it (fun=0x3db)\n",
            argv0);
}

/* Stores each --pref KEY=VALUE before boot, as NVS would hold it, and takes it out of argv; returns
 * the new argc. The key stays in argv, which outlives the run (hal_pref_set keeps the pointer). */
static int take_prefs(int argc, char **argv)
{
    int n = 1;
    for (int i = 1; i < argc; i++) {
        char *eq = i + 1 < argc && !strcmp(argv[i], "--pref") ? strchr(argv[i + 1], '=') : NULL;
        if (!eq) {
            argv[n++] = argv[i];
            continue;
        }
        *eq = '\0';
        hal_pref_set(argv[++i], (uint32_t)strtoul(eq + 1, NULL, 0));
    }
    return n;
}

int main(int argc, char **argv)
{
    bool headless = false, no_ws = false;
    const char *script = NULL;
    uint32_t idle_s = 0, deep_s = 0, tries = 0, slept_s = 0;
    hal_wake_t wake = HAL_WAKE_POWER_ON;
    bool battery = false;
    argc = take_prefs(argc, argv);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = true;
        else if (!strcmp(argv[i], "--no-ws")) no_ws = true;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--idle-off") && i + 1 < argc) idle_s = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--battery")) battery = true;
        else if (!strcmp(argv[i], "--deep-after") && i + 1 < argc)
            deep_s = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tries") && i + 1 < argc) tries = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--slept") && i + 1 < argc) slept_s = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--wake") && i + 1 < argc) {
            const char *w = argv[++i];
            if (!strcmp(w, "timer")) wake = HAL_WAKE_TIMER;
            else if (!strcmp(w, "button")) wake = HAL_WAKE_BUTTON;
            else return usage(argv[0]), 2;
        } else return usage(argv[0]), 2;
    }

    sim_display_set_headless(headless);
    sim_link_enable_ws(!no_ws);
    sim_set_battery(battery);
    sim_set_wake(wake, tries);
    sim_set_deep_slept_ms(slept_s * 1000u);
    hal_board_init();
    core_init();
    core_set_idle_timeout_ms(idle_s * 1000);
    if (deep_s) core_set_deep_after_ms(deep_s * 1000);
    if (script && !sim_script_load(script)) return 2;
    /* Nobody watches a headless scripted run and nothing outside it keeps time, so its clock is
     * virtual: the script's timeline holds however slow the machine is (issue 6eb4). */
    if (script && headless && no_ws && !getenv("AGENTLINK_SIM_REAL_CLOCK")) sim_clock_virtual();

    bool script_running = script != NULL;
    while (!sim_display_quit_requested()) {
        uint32_t now = (uint32_t)(hal_time_us() / 1000);
        sim_display_pump();
        sim_link_poll(now);
        sim_motion_poll(now);
        if (script_running && !sim_script_poll(now)) {
            if (sim_script_failed()) return 1;
            if (headless || no_ws) break;
            script_running = false;
        }
        uint32_t wait = timed_core_step();
        sim_sleep_us((uint64_t)(wait ? wait : 1) * 1000u);
    }
    return 0;
}
