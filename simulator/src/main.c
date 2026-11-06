/* Desktop simulator: the same core as the firmware, on SDL2 + a WebSocket link to the daemon. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "sim.h"

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--headless] [--no-ws] [--script FILE] [--idle-off SECONDS]\n"
            "  --headless   no window (framebuffer still rendered, screenshots work)\n"
            "  --no-ws      do not connect to ws://127.0.0.1:7878 (scripts can inject frames)\n"
            "  --script F   run scripted input; exits when done (code 1 if an expect failed)\n"
            "  --idle-off S turn the display off after S seconds without input\n",
            argv0);
}

int main(int argc, char **argv)
{
    bool headless = false, no_ws = false;
    const char *script = NULL;
    uint32_t idle_s = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = true;
        else if (!strcmp(argv[i], "--no-ws")) no_ws = true;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--idle-off") && i + 1 < argc) idle_s = (uint32_t)atoi(argv[++i]);
        else return usage(argv[0]), 2;
    }

    sim_display_set_headless(headless);
    sim_link_enable_ws(!no_ws);
    hal_board_init();
    core_init();
    core_set_idle_timeout_ms(idle_s * 1000);
    if (script && !sim_script_load(script)) return 2;

    bool script_running = script != NULL;
    while (!sim_display_quit_requested()) {
        uint32_t now = (uint32_t)(hal_time_us() / 1000);
        sim_display_pump();
        sim_link_poll(now);
        if (script_running && !sim_script_poll(now)) {
            if (sim_script_failed()) return 1;
            if (headless || no_ws) break;
            script_running = false;
        }
        usleep(core_step() * 1000);
    }
    return 0;
}
