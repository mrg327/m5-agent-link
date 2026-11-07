/* Scripted input. A script is expanded up front into a timeline of primitive events (mouse, keys,
 * IMU, frames, checks) that are replayed in real time through the same paths as real input.
 *
 *   wait MS | tap X Y | hold X Y MS | swipe X1 Y1 X2 Y2 [MS] | btn A|B [long|double] | chord
 *   scrub DEG0 DEG1 [MS [RADIUS]]   drag along the rim, degrees clockwise from 12 o'clock
 *   session SID work|input|ask|error|done|idle|doneseen REPO   add or update a session (input = permission, ask = question)
 *   drop SID | lift | shake | connect | disconnect | rx HEX | ui | shot FILE.png
 *   expect_screen NAME | expect_label TEXT | expect_no_label TEXT | quit
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "model.h"
#include "ui_theme.h"
#include "sim.h"

typedef enum { E_PTR, E_BTN, E_MOTION, E_CONNECT, E_RX, E_UI, E_SHOT, E_EXPECT_SCREEN, E_EXPECT_LABEL, E_EXPECT_NO_LABEL, E_SESSION, E_DROP, E_QUIT } ev_kind_t;

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

/* A finger dragging along an arc, one sample every 8 ms. */
static void scrub(uint32_t *t, int d0, int d1, int ms, int radius)
{
    int steps = ms / 8 > 2 ? ms / 8 : 2;
    for (int i = 0; i <= steps; i++) {
        float rad = (float)(d0 + (d1 - d0) * i / steps) * 3.14159265f / 180.0f;
        int x = 233 + (int)lroundf((float)radius * sinf(rad)), y = 233 - (int)lroundf((float)radius * cosf(rad));
        add(*t + (uint32_t)(ms * i / steps), E_PTR, 1, x, y, NULL);
        if (i == steps) add(*t + (uint32_t)ms + 8, E_PTR, 0, x, y, NULL);
    }
    *t += (uint32_t)ms + 150;
}

static void btn(uint32_t *t, int b, uint32_t down_at, uint32_t up_at)
{
    add(*t + down_at, E_BTN, b, 1, 0, NULL);
    add(*t + up_at, E_BTN, b, 0, 0, NULL);
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
    else if (!strcmp(cmd, "hold") && sscanf(rest, "%d %d %d", &a, &b, &c) == 3) {
        add(*t, E_PTR, 1, a, b, NULL);
        add(*t + (uint32_t)c, E_PTR, 0, a, b, NULL);
        *t += (uint32_t)c + 150;
    } else if (!strcmp(cmd, "swipe")) {
        int n = sscanf(rest, "%d %d %d %d %d", &a, &b, &c, &d, &e);
        if (n < 4) return false;
        swipe(t, a, b, c, d, n == 5 ? e : 160);
    } else if (!strcmp(cmd, "btn") && (rest[0] == 'A' || rest[0] == 'B')) {
        bool lng = strstr(rest, "long") != NULL, dbl = strstr(rest, "double") != NULL;
        int bi = rest[0] == 'B';
        if (dbl) {
            btn(t, bi, 0, 60);
            btn(t, bi, 160, 220);
            *t += 220 + 600; /* a double press settles at once; leave room for a stray single */
        } else {
            btn(t, bi, 0, lng ? 900u : 60u);
            *t += (lng ? 900u : 60u) + 450; /* a single A is delayed by the double-press window */
        }
    } else if (!strcmp(cmd, "chord")) {
        btn(t, 0, 0, 200);
        btn(t, 1, 40, 220);
        *t += 220 + 450;
    } else if (!strcmp(cmd, "scrub") && sscanf(rest, "%d %d %d %d", &a, &b, &c, &d) >= 2) {
        int n = sscanf(rest, "%d %d %d %d", &a, &b, &c, &d);
        scrub(t, a, b, n >= 3 ? c : 400, n >= 4 ? d : 200);
    } else if (!strcmp(cmd, "session") && arg[0]) {
        add(*t, E_SESSION, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "drop") && sscanf(rest, "%d", &a) == 1) {
        add(*t, E_DROP, a, 0, 0, NULL);
        *t += 50;
    } else if (!strcmp(cmd, "expect_no_label") && arg[0]) {
        add(*t, E_EXPECT_NO_LABEL, 0, 0, 0, arg);
        *t += 50;
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
    for (size_t i = 1; i < s_n; i++) { /* stable: overlapping primitives (a chord) are added out of order */
        ev_t k = s_ev[i];
        size_t j = i;
        for (; j > 0 && s_ev[j - 1].at_ms > k.at_ms; j--) s_ev[j] = s_ev[j - 1];
        s_ev[j] = k;
    }
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

static void run_session(const char *spec)
{
    unsigned sid;
    char st[16], repo[40];
    if (sscanf(spec, "%u %15s %39s", &sid, st, repo) != 3) {
        s_failed = true;
        return;
    }
    uint32_t now = theme_now_unix();
    session_t d = {.sid = sid, .agent = (agent_t)(sid % 3), .state = SESS_WORK, .t0 = now - 3600 + sid, .tst = now - 40 - 53 * sid};
    if (!strcmp(st, "input") || !strcmp(st, "ask")) {
        d.state = SESS_INPUT;
        d.sub = st[0] == 'a';
    } else if (!strcmp(st, "error")) d.state = SESS_ERROR;
    else if (!strcmp(st, "done") || !strcmp(st, "doneseen")) {
        d.state = SESS_DONE;
        d.seen = st[4] == 's';
    } else if (!strcmp(st, "idle")) d.state = SESS_IDLE;
    snprintf(d.repo, sizeof d.repo, "%s", repo);
    model_upsert(&d, SF_AGENT | SF_STATE | SF_SUB | SF_REPO | SF_T0 | SF_TST | SF_SEEN);
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
    case E_EXPECT_NO_LABEL:
        if (core_ui_has_label(e->text)) fail("unexpected label", e->text);
        break;
    case E_SESSION: run_session(e->text); break;
    case E_DROP: model_remove((uint32_t)e->a); break;
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
