/* Scripted input. A script is expanded up front into a timeline of primitive events (mouse, keys,
 * IMU, frames, checks) that are replayed in real time through the same paths as real input.
 *
 *   wait MS | tap X Y | dtap X Y | hold X Y MS | swipe X1 Y1 X2 Y2 [MS] | btn A|B [long]
 *   lift | shake | connect | disconnect | rx HEX | ui | shot FILE.png
 *   expect_screen NAME | expect_label TEXT | quit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "sim.h"

typedef enum { E_PTR, E_BTN, E_MOTION, E_CONNECT, E_RX, E_UI, E_SHOT, E_EXPECT_SCREEN, E_EXPECT_LABEL, E_QUIT } ev_kind_t;

typedef struct {
    uint32_t at_ms;
    ev_kind_t kind;
    int a, b, c;
    char *text;    /* file, expected string, or hex */
} ev_t;

static ev_t *s_ev;
static size_t s_n, s_cap, s_next;
static uint32_t s_t0;
static bool s_started, s_failed;

bool sim_script_failed(void) { return s_failed; }

static void add(uint32_t at, ev_kind_t kind, int a, int b, int c, const char *text)
{
    if (s_n == s_cap) {
        s_cap = s_cap ? s_cap * 2 : 64;
        s_ev = realloc(s_ev, s_cap * sizeof *s_ev);
    }
    s_ev[s_n++] = (ev_t){at, kind, a, b, c, text ? strdup(text) : NULL};
}

static void swipe(uint32_t *t, int x1, int y1, int x2, int y2, int ms)
{
    const int steps = 8;
    add(*t, E_PTR, 1, x1, y1, NULL);
    for (int i = 1; i <= steps; i++) add(*t + (uint32_t)(ms * i / steps), E_PTR, 1, x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps, NULL);
    add(*t + (uint32_t)ms + 5, E_PTR, 0, x2, y2, NULL);
    *t += (uint32_t)ms + 120;
}

static void tap(uint32_t *t, int x, int y)
{
    add(*t, E_PTR, 1, x, y, NULL);
    add(*t + 50, E_PTR, 0, x, y, NULL);
    *t += 100;
}

static bool parse_line(char *line, uint32_t *t)
{
    char cmd[24] = "", arg[512] = "";
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    if (sscanf(line, "%23s", cmd) != 1 || cmd[0] == '#') return true;
    char *rest = line + strlen(cmd);
    while (*rest == ' ') rest++;
    rest[strcspn(rest, "\r\n")] = 0;
    snprintf(arg, sizeof arg, "%s", rest);

    if (!strcmp(cmd, "wait") && sscanf(rest, "%d", &a) == 1) *t += (uint32_t)a;
    else if (!strcmp(cmd, "tap") && sscanf(rest, "%d %d", &a, &b) == 2) tap(t, a, b);
    else if (!strcmp(cmd, "dtap") && sscanf(rest, "%d %d", &a, &b) == 2) {
        tap(t, a, b);
        *t += 0;
        tap(t, a, b);
        *t += 150;
    } else if (!strcmp(cmd, "hold") && sscanf(rest, "%d %d %d", &a, &b, &c) == 3) {
        add(*t, E_PTR, 1, a, b, NULL);
        add(*t + (uint32_t)c, E_PTR, 0, a, b, NULL);
        *t += (uint32_t)c + 150;
    } else if (!strcmp(cmd, "swipe")) {
        int n = sscanf(rest, "%d %d %d %d %d", &a, &b, &c, &d, &e);
        if (n < 4) return false;
        swipe(t, a, b, c, d, n == 5 ? e : 160);
    } else if (!strcmp(cmd, "btn") && (rest[0] == 'A' || rest[0] == 'B')) {
        bool lng = strstr(rest, "long") != NULL;
        add(*t, E_BTN, rest[0] == 'B', 1, 0, NULL);
        add(*t + (lng ? 900u : 60u), E_BTN, rest[0] == 'B', 0, 0, NULL);
        *t += (lng ? 900u : 60u) + 120;
    } else if (!strcmp(cmd, "lift")) {
        add(*t, E_MOTION, HAL_MOTION_LIFT, 0, 0, NULL);
        *t += 100;
    } else if (!strcmp(cmd, "shake")) {
        add(*t, E_MOTION, HAL_MOTION_SHAKE, 0, 0, NULL);
        *t += 100;
    } else if (!strcmp(cmd, "connect") || !strcmp(cmd, "disconnect")) {
        add(*t, E_CONNECT, cmd[0] == 'c', 0, 0, NULL);
        *t += 50;
    } else if (!strcmp(cmd, "rx") && arg[0]) {
        add(*t, E_RX, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "ui")) {
        add(*t, E_UI, 0, 0, 0, NULL);
        *t += 50;
    } else if (!strcmp(cmd, "shot") && arg[0]) {
        add(*t + 100, E_SHOT, 0, 0, 0, arg); /* let the last frame render */
        *t += 150;
    } else if (!strcmp(cmd, "expect_screen") && arg[0]) {
        add(*t, E_EXPECT_SCREEN, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "expect_label") && arg[0]) {
        add(*t, E_EXPECT_LABEL, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "quit")) {
        add(*t, E_QUIT, 0, 0, 0, NULL);
    } else {
        return false;
    }
    return true;
}

bool sim_script_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot open script %s\n", path);
        return false;
    }
    char line[1024];
    uint32_t t = 300; /* let the first frame render */
    for (int no = 1; fgets(line, sizeof line, f); no++) {
        if (!parse_line(line, &t)) {
            fprintf(stderr, "%s:%d: bad command: %s", path, no, line);
            fclose(f);
            return false;
        }
    }
    fclose(f);
    add(t + 200, E_QUIT, 0, 0, 0, NULL);
    return true;
}

static void send_hex(const char *hex)
{
    uint8_t buf[4096];
    size_t n = 0;
    for (; hex[0] && hex[1] && n < sizeof buf; hex += 2) {
        unsigned v;
        if (sscanf(hex, "%2x", &v) != 1) break;
        buf[n++] = (uint8_t)v;
    }
    sim_link_inject_rx(buf, n);
}

static void fail(const char *what, const char *want)
{
    fprintf(stderr, "SCRIPT FAIL: %s '%s' (screen=%s)\n", what, want, core_screen_name());
    core_dump_ui_state();
    s_failed = true;
}

static void run(const ev_t *e)
{
    switch (e->kind) {
    case E_PTR: sim_input_pointer(e->a, e->b, e->c); break;
    case E_BTN: sim_input_button(e->a, e->b); break;
    case E_MOTION: sim_input_motion((hal_motion_kind_t)e->a); break;
    case E_CONNECT: sim_link_set_connected(e->a); break;
    case E_RX: send_hex(e->text); break;
    case E_UI: core_dump_ui_state(); break;
    case E_SHOT:
        if (!sim_save_png(e->text)) fail("cannot write", e->text);
        else sim_log("screenshot", "\"file\":\"%s\"", e->text);
        break;
    case E_EXPECT_SCREEN:
        if (strcmp(core_screen_name(), e->text)) fail("expected screen", e->text);
        break;
    case E_EXPECT_LABEL:
        if (!core_ui_has_label(e->text)) fail("expected label", e->text);
        break;
    case E_QUIT: s_next = s_n; break;
    }
}

bool sim_script_poll(uint32_t now_ms)
{
    if (!s_started) {
        s_started = true;
        s_t0 = now_ms;
    }
    while (s_next < s_n && now_ms - s_t0 >= s_ev[s_next].at_ms && !s_failed) {
        const ev_t *e = &s_ev[s_next++];
        run(e);
        if (e->kind == E_QUIT) break;
    }
    return s_next < s_n && !s_failed;
}
