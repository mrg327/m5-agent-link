/* Scripted input. A script is expanded up front into a timeline of primitive events (mouse, keys,
 * IMU, frames, checks) that are replayed in real time through the same paths as real input.
 *
 *   wait MS | tap X Y | hold X Y MS | press X Y | release | swipe X1 Y1 X2 Y2 [MS] | btn A|B [long|double|held] | chord
 *   stroke X,Y X,Y ...              KEYS: one finger stroke through the points, a sample every 33 ms, then lifted
 *   word LETTERS                    KEYS: a swipe through the centres of those layer-0 letters
 *   key CHAR|space|bksp|layer       KEYS: a tap on that key (layer 0, then the symbols layer)
 *   cands TYPED|- [err CODE] WORD...   KEYS: a daemon kb_cands for the open keyboard (TYPED = index of the typed word)
 *   scrub DEG0 DEG1 [MS [RADIUS]]   drag along the rim, degrees clockwise from 12 o'clock
 *   drag X Y DY [MS]                a slow vertical drag in the centre (default 500 ms: steps, no swipe); a flick is a fast `swipe`
 *   session SID work|input|ask|error|done|idle|doneseen REPO   add or update a session (input = permission, ask = question)
 *   job SID run|done|fail REPO ELAPSED_S ETA_S EXIT [command line...]   a terminal job (JOBS.md); ETA_S 0 = no estimate
 *   gauge SID CTX|- [MODEL|- [MODE|- [PR [STATE]]]]   session readouts (GAUGES.md, proto 0.10): context %
 *                                   (- = unknown, 255 on the wire), model (_ for a space), mode default|plan|
 *                                   accept_edits|auto|dont_ask|bypass, pull request number (0 = none) and its
 *                                   state none|pending|approved|changes|draft
 *   usage H5|- H5_RESET_S H5_OUT_S|- WK|- WK_RESET_S [AGE_S]   a daemon `usage` (GAUGES.md): percent, seconds
 *                                   until the reset and until it runs out at the current pace (- = not before
 *                                   the reset), relative to now; the reading is AGE_S old (default 0)
 *   remote SID HOST [NAME]          set host / host name on a session (host 0 = local)
 *   drop SID | lift | shake | connect [usb|ble] | disconnect [usb|ble] | rx [usb|ble] HEX | ui | shot FILE.png
 *   attention RID SID approval|question|error|done ACTS GRANT_MASK TITLE|TEXT|OPT|OPT...
 *                                   a daemon `attention`; ACTS = comma list of approve,deny,focus,reply,ack
 *   result ok | result err stale|denied|unsupported|no_window|timeout|internal
 *                                   a daemon `result` answering the last command the device sent
 *   result pos LINES AT_BOTTOM [TOTAL|- [PREVIEW TEXT...]]   a scroll result with scroll_pos
 *   cfg privacy meta|preview|full   a daemon `cfg` changing the device's privacy setting
 *   step SID N [ok|fail] [TOOL · ARG...]   a delta with a new step count n (and its result ok): the step timeline
 *   snooze SID UNIX|off             a delta with snz (snoozed until UNIX, 0 = off)
 *   notify SID on|off               a delta with ntf (notify when done armed)
 *   rtc UNIX                        set the RTC (0 = clock unknown)
 *   clear RID [answered_pc|answered_dev|expired|cancelled]
 *   expect_screen NAME | expect_label TEXT | expect_no_label TEXT | quit
 *   px_reset | expect_px_max PX     redraw budget: pixels flushed since px_reset must stay within PX
 *                                   (the log line also reports core_step host CPU as cpu_us, not asserted)
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "agentlink_codec_encode.h"
#include "app.h"
#include "core.h"
#include "keys.h"
#include "link_internal.h"
#include "model.h"
#include "ui_theme.h"
#include "voice.h"
#include "sim.h"

static char s_script_dir[512] = ".";

typedef enum { E_PTR, E_BTN, E_MOTION, E_CONNECT, E_RX, E_UI, E_SHOT, E_EXPECT_SCREEN, E_EXPECT_LABEL, E_EXPECT_NO_LABEL, E_SESSION, E_JOB, E_GAUGE, E_USAGE, E_REMOTE, E_DROP, E_ATTENTION, E_RESULT, E_CLEAR, E_CFG, E_RTC, E_DELTA, E_PX_RESET, E_EXPECT_PX_MAX, E_MIC, E_TRANSCRIPT, E_CANDS, E_QUIT } ev_kind_t;

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
static uint32_t s_px0;
static uint64_t s_cpu0;

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

/* A finger moving straight down (dy > 0) or up, one sample every 16 ms, then lifted where it stopped. */
static void drag(uint32_t *t, int x, int y, int dy, int ms)
{
    int steps = ms / 16 > 2 ? ms / 16 : 2;
    for (int i = 0; i <= steps; i++) add(*t + (uint32_t)(ms * i / steps), E_PTR, 1, x, y + dy * i / steps, NULL);
    add(*t + (uint32_t)ms + 16, E_PTR, 0, x, y + dy, NULL);
    *t += (uint32_t)ms + 150;
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

#define STROKE_MAX 200

/* One finger stroke: down on the first point, one sample per 33 ms (the LVGL read rate), up on the last. */
static void stroke(uint32_t *t, const int (*p)[2], int n)
{
    for (int i = 0; i < n; i++) add(*t + 33u * (uint32_t)i, E_PTR, 1, p[i][0], p[i][1], NULL);
    add(*t + 33u * (uint32_t)n, E_PTR, 0, p[n - 1][0], p[n - 1][1], NULL);
    *t += 33u * (uint32_t)n + 150;
}

static bool key_centre(int cp, int *x, int *y)
{
    for (int layer = 0; layer < 2; layer++) {
        keys_key_t k[KEYS_MAX_KEYS];
        int n = keys_layout(layer, k, KEYS_MAX_KEYS);
        for (int i = 0; i < n; i++)
            if (k[i].cp == cp) {
                *x = k[i].x + k[i].w / 2;
                *y = k[i].y + k[i].h / 2;
                return true;
            }
    }
    return false;
}

/* A swipe through the layer-0 letters of a word, three samples between neighbouring keys. */
static bool word_stroke(uint32_t *t, const char *w)
{
    static int p[STROKE_MAX][2];
    int n = 0, px = 0, py = 0;
    for (; *w && *w != ' ' && *w != '\r' && *w != '\n'; w++) {
        int x, y;
        if (!key_centre((unsigned char)*w, &x, &y)) return false;
        if (n)
            for (int j = 1; j <= 3 && n < STROKE_MAX; j++) {
                p[n][0] = px + (x - px) * j / 4;
                p[n++][1] = py + (y - py) * j / 4;
            }
        if (n < STROKE_MAX) {
            p[n][0] = x;
            p[n++][1] = y;
        }
        px = x;
        py = y;
    }
    if (n < 1) return false;
    stroke(t, (const int (*)[2])p, n);
    return true;
}

static void btn(uint32_t *t, int b, uint32_t down_at, uint32_t up_at)
{
    add(*t + down_at, E_BTN, b, 1, 0, NULL);
    add(*t + up_at, E_BTN, b, 0, 0, NULL);
}

static int s_last_x, s_last_y; /* where `press` put the finger, for `release` */

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
    } else if (!strcmp(cmd, "press") && sscanf(rest, "%d %d", &a, &b) == 2) {
        add(*t, E_PTR, 1, a, b, NULL); /* finger down; stays down until `release` */
        s_last_x = a;
        s_last_y = b;
        *t += 20;
    } else if (!strcmp(cmd, "release")) {
        add(*t, E_PTR, 0, s_last_x, s_last_y, NULL);
        *t += 150;
    } else if (!strcmp(cmd, "swipe")) {
        int n = sscanf(rest, "%d %d %d %d %d", &a, &b, &c, &d, &e);
        if (n < 4) return false;
        swipe(t, a, b, c, d, n == 5 ? e : 160);
    } else if (!strcmp(cmd, "btn") && (rest[0] == 'A' || rest[0] == 'B')) {
        bool held = strstr(rest, "held") != NULL; /* past the 1.2 s hold-to-send */
        bool lng = strstr(rest, "long") != NULL || held, dbl = strstr(rest, "double") != NULL;
        int bi = rest[0] == 'B';
        if (dbl) {
            btn(t, bi, 0, 60);
            btn(t, bi, 160, 220);
            *t += 220 + 600; /* a double press settles at once; leave room for a stray single */
        } else {
            uint32_t ms = held ? 1500u : lng ? 900u : 60u;
            btn(t, bi, 0, ms);
            *t += ms + 450; /* a single A is delayed by the double-press window */
        }
    } else if (!strcmp(cmd, "stroke") && arg[0]) {
        static int p[STROKE_MAX][2];
        int n = 0, off = 0;
        while (n < STROKE_MAX && sscanf(rest + off, " %d,%d%n", &p[n][0], &p[n][1], &c) == 2) {
            off += c;
            n++;
        }
        if (n < 1) return false;
        stroke(t, (const int (*)[2])p, n);
    } else if (!strcmp(cmd, "word") && arg[0]) {
        if (!word_stroke(t, arg)) return false;
    } else if (!strcmp(cmd, "key") && arg[0]) {
        int cp = !strcmp(arg, "space") ? 0x20 : !strcmp(arg, "bksp") ? 0x08 : !strcmp(arg, "layer") ? 0x0e : (unsigned char)arg[0];
        if (!key_centre(cp, &a, &b)) return false;
        tap(t, a, b);
    } else if (!strcmp(cmd, "cands") && arg[0]) {
        add(*t, E_CANDS, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "chord")) {
        btn(t, 0, 0, 200);
        btn(t, 1, 40, 220);
        *t += 220 + 450;
    } else if (!strcmp(cmd, "drag") && sscanf(rest, "%d %d %d %d", &a, &b, &c, &d) >= 3) {
        drag(t, a, b, c, sscanf(rest, "%d %d %d %d", &a, &b, &c, &d) == 4 ? d : 500);
    } else if (!strcmp(cmd, "rtc") && sscanf(rest, "%d", &a) == 1) {
        add(*t, E_RTC, a, 0, 0, NULL);
        *t += 50;
    } else if (!strcmp(cmd, "cfg") && arg[0]) {
        add(*t, E_CFG, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "scrub") && sscanf(rest, "%d %d %d %d", &a, &b, &c, &d) >= 2) {
        int n = sscanf(rest, "%d %d %d %d", &a, &b, &c, &d);
        scrub(t, a, b, n >= 3 ? c : 400, n >= 4 ? d : 200);
    } else if (!strcmp(cmd, "session") && arg[0]) {
        add(*t, E_SESSION, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "job") && arg[0]) {
        add(*t, E_JOB, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "gauge") && arg[0]) {
        add(*t, E_GAUGE, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "usage") && arg[0]) {
        add(*t, E_USAGE, 0, 0, 0, arg);
        *t += 50;
    } else if ((!strcmp(cmd, "step") || !strcmp(cmd, "snooze") || !strcmp(cmd, "notify")) && arg[0]) {
        char line[160];
        snprintf(line, sizeof line, "%s %s", cmd, arg);
        add(*t, E_DELTA, 0, 0, 0, line);
        *t += 50;
    } else if (!strcmp(cmd, "remote") && arg[0]) {
        add(*t, E_REMOTE, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "drop") && sscanf(rest, "%d", &a) == 1) {
        add(*t, E_DROP, a, 0, 0, NULL);
        *t += 50;
    } else if (!strcmp(cmd, "attention") && arg[0]) {
        add(*t, E_ATTENTION, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "result") && arg[0]) {
        add(*t, E_RESULT, 0, 0, 0, arg);
        *t += 50;
    } else if (!strcmp(cmd, "mic") && arg[0]) {
        /* a path relative to the script's own directory */
        char path[700];
        if (arg[0] == '/') snprintf(path, sizeof path, "%s", arg);
        else snprintf(path, sizeof path, "%s/%s", s_script_dir, arg);
        add(*t, E_MIC, 0, 0, 0, path);
        *t += 50;
    } else if (!strcmp(cmd, "transcript")) {
        add(*t, E_TRANSCRIPT, 0, 0, 0, arg); /* answers the device's current recording; "" = nothing heard */
        *t += 50;
    } else if (!strcmp(cmd, "clear") && sscanf(rest, "%d", &a) == 1) {
        add(*t, E_CLEAR, a, 0, 0, arg);
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
        /* optional transport: usb | ble (default: the WS fake link) */
        int kind = !strncmp(arg, "usb", 3) ? HAL_LINK_USB : !strncmp(arg, "ble", 3) ? HAL_LINK_BLE : HAL_LINK_WS;
        add(*t, E_CONNECT, cmd[0] == 'c', kind, 0, NULL);
        *t += 50;
    } else if (!strcmp(cmd, "rx") && arg[0]) {
        int kind = HAL_LINK_WS;
        const char *hex = arg;
        if (!strncmp(arg, "usb ", 4) || !strncmp(arg, "ble ", 4)) {
            kind = arg[0] == 'u' ? HAL_LINK_USB : HAL_LINK_BLE;
            hex += 4;
        }
        add(*t, E_RX, 0, kind, 0, hex);
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
    } else if (!strcmp(cmd, "px_reset")) {
        add(*t, E_PX_RESET, 0, 0, 0, NULL);
    } else if (!strcmp(cmd, "expect_px_max") && sscanf(rest, "%d", &a) == 1) {
        add(*t, E_EXPECT_PX_MAX, a, 0, 0, NULL);
    } else if (!strcmp(cmd, "quit")) {
        add(*t, E_QUIT, 0, 0, 0, NULL);
    } else {
        return false;
    }
    return true;
}

bool sim_script_load(const char *path)
{
    snprintf(s_script_dir, sizeof s_script_dir, "%s", path);
    char *slash = strrchr(s_script_dir, '/');
    if (slash) *slash = 0;
    else snprintf(s_script_dir, sizeof s_script_dir, ".");
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

static void send_hex_kind(int kind, const char *hex)
{
    uint8_t buf[4096];
    size_t n = 0;
    for (; hex[0] && hex[1] && n < sizeof buf; hex += 2) {
        unsigned v;
        if (sscanf(hex, "%2x", &v) != 1) break;
        buf[n++] = (uint8_t)v;
    }
    sim_link_inject_rx_kind(kind, buf, n);
}

static void run_session(const char *spec)
{
    /* session SID STATE REPO [BRANCH|- [summary text...]] */
    unsigned sid;
    char st[16], repo[33], branch[33] = "";
    int n = 0;
    if (sscanf(spec, "%u %15s %32s", &sid, st, repo) != 3) {
        s_failed = true;
        return;
    }
    sscanf(spec, "%*u %*15s %*32s %32s %n", branch, &n);
    if (!strcmp(branch, "-")) branch[0] = 0;
    uint32_t now = theme_now_unix();
    session_t d = {.sid = sid, .agent = (agent_t)(sid % 3), .state = SESS_WORK, .t0 = now - 3600 + sid, .tst = now - 40 - 53 * sid};
    uint32_t present = SF_AGENT | SF_STATE | SF_SUB | SF_REPO | SF_T0 | SF_TST | SF_SEEN;
    if (branch[0]) {
        snprintf(d.branch, sizeof d.branch, "%s", branch);
        present |= SF_BRANCH;
    }
    if (n > 0 && spec[n]) {
        snprintf(d.summary, sizeof d.summary, "%s", spec + n);
        present |= SF_SUMMARY;
    }
    if (!strcmp(st, "input") || !strcmp(st, "ask")) {
        d.state = SESS_INPUT;
        d.sub = st[0] == 'a';
    } else if (!strcmp(st, "error")) d.state = SESS_ERROR;
    else if (!strcmp(st, "done") || !strcmp(st, "doneseen")) {
        d.state = SESS_DONE;
        d.seen = st[4] == 's';
    } else if (!strcmp(st, "idle")) d.state = SESS_IDLE;
    snprintf(d.repo, sizeof d.repo, "%s", repo);
    model_upsert(&d, present);
}

/* job SID run|done|fail REPO ELAPSED_S ETA_S EXIT [command line...]: a terminal job (JOBS.md, proto 0.9).
 * ELAPSED_S is how long it has run (or ran: a done / fail job ended 5 s ago); ETA_S 0 = no estimate. */
static void run_job(const char *spec)
{
    unsigned sid, el, eta, code;
    char st[8], repo[33];
    int n = 0;
    if (sscanf(spec, "%u %7s %32s %u %u %u %n", &sid, st, repo, &el, &eta, &code, &n) < 6) {
        fprintf(stderr, "SCRIPT FAIL: bad job '%s'\n", spec);
        s_failed = true;
        return;
    }
    uint32_t now = theme_now_unix();
    bool ended = strcmp(st, "run") != 0;
    session_t d = {.sid = sid, .agent = AGENT_SHELL, .state = !ended ? SESS_WORK : !strcmp(st, "done") ? SESS_DONE : SESS_ERROR,
                   .eta = eta, .exit_code = (uint8_t)code};
    d.tst = ended ? now - 5 : now - el;
    d.t0 = d.tst - (ended ? el : 0);
    uint32_t present = SF_AGENT | SF_STATE | SF_REPO | SF_T0 | SF_TST | SF_SEEN | SF_ETA | SF_TOOL | SF_BRANCH;
    if (ended) present |= SF_EXIT;
    snprintf(d.repo, sizeof d.repo, "%s", repo);
    snprintf(d.branch, sizeof d.branch, "main");
    if (n > 0 && spec[n]) {
        snprintf(d.summary, sizeof d.summary, "%s", spec + n);
        sscanf(spec + n, "%24s", d.tool);
        present |= SF_SUMMARY;
    }
    model_upsert(&d, present);
}

/* gauge SID CTX|- [MODEL|- [MODE|- [PR [STATE]]]]: a proto 0.10 session delta (GAUGES.md) */
static void run_gauge(const char *spec)
{
    static const char *const modes[] = {"default", "plan", "accept_edits", "auto", "dont_ask", "bypass"};
    static const char *const states[] = {"none", "pending", "approved", "changes", "draft"};
    unsigned sid, pr = 0;
    char ctx[8], mdl[24] = "", mode[16] = "", st[12] = "";
    int n = sscanf(spec, "%u %7s %23s %15s %u %11s", &sid, ctx, mdl, mode, &pr, st);
    if (n < 2) {
        fprintf(stderr, "SCRIPT FAIL: bad gauge '%s'\n", spec);
        s_failed = true;
        return;
    }
    session_t d = {.sid = sid};
    uint32_t present = SF_CTX;
    d.ctx = !strcmp(ctx, "-") ? MODEL_CTX_UNKNOWN : (uint8_t)atoi(ctx);
    if (n >= 3 && strcmp(mdl, "-")) {
        for (char *c = mdl; *c; c++)
            if (*c == '_') *c = ' ';
        snprintf(d.mdl, sizeof d.mdl, "%.16s", mdl);
        present |= SF_MDL;
    }
    if (n >= 4 && strcmp(mode, "-")) {
        for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++)
            if (!strcmp(mode, modes[i])) d.pm = (uint8_t)i;
        present |= SF_PM;
    }
    if (n >= 5) {
        d.pr = pr;
        present |= SF_PR | SF_PRS;
        for (unsigned i = 0; i < sizeof states / sizeof states[0]; i++)
            if (!strcmp(st, states[i])) d.prs = (uint8_t)i;
    }
    model_upsert(&d, present);
}

/* usage H5|- H5_RESET_S H5_OUT_S|- WK|- WK_RESET_S [AGE_S]: a daemon `usage` relative to the clock */
static void run_usage(const char *spec)
{
    char h5[8], h5r[16], h5o[16], wk[8], wkr[16];
    unsigned age = 0;
    if (sscanf(spec, "%7s %15s %15s %7s %15s %u", h5, h5r, h5o, wk, wkr, &age) < 5) {
        fprintf(stderr, "SCRIPT FAIL: bad usage '%s'\n", spec);
        s_failed = true;
        return;
    }
    uint32_t now = theme_now_unix();
    usage_t u = {.at = now - age};
    if (strcmp(h5, "-")) {
        u.h5 = (usage_win_t){true, (uint8_t)atoi(h5), now + (uint32_t)strtoul(h5r, NULL, 10),
                             strcmp(h5o, "-") ? now + (uint32_t)strtoul(h5o, NULL, 10) : 0};
    }
    if (strcmp(wk, "-")) u.wk = (usage_win_t){true, (uint8_t)atoi(wk), now + (uint32_t)strtoul(wkr, NULL, 10), 0};
    link_rx_usage(&u);
}

/* step SID N [ok|fail] [text...] | snooze SID UNIX|off | notify SID on|off: protocol 0.3 session deltas */
static void run_delta(const char *spec)
{
    char cmd[8], w[16] = "";
    unsigned sid, n = 0;
    session_t d = {0};
    uint32_t present = 0;
    if (sscanf(spec, "%7s %u", cmd, &sid) != 2) {
        fprintf(stderr, "SCRIPT FAIL: bad delta '%s'\n", spec);
        s_failed = true;
        return;
    }
    d.sid = sid;
    if (!strcmp(cmd, "step")) {
        int off = 0;
        if (sscanf(spec, "%*s %*u %u %15s %n", &n, w, &off) < 2) {
            if (sscanf(spec, "%*s %*u %u", &n) != 1) {
                fprintf(stderr, "SCRIPT FAIL: bad step '%s'\n", spec);
                s_failed = true;
                return;
            }
        }
        d.n = n;
        present |= SF_N;
        const char *text = off > 0 ? spec + off : "";
        if (!strcmp(w, "ok") || !strcmp(w, "fail")) {
            d.step = !strcmp(w, "ok") ? STEP_OK : STEP_FAIL;
            present |= SF_OK;
        } else if (w[0]) { /* the first word was text, not a result */
            sscanf(spec, "%*s %*u %*u %n", &off);
            text = spec + off;
        }
        if (text[0]) {
            snprintf(d.summary, sizeof d.summary, "%s", text);
            present |= SF_SUMMARY;
        }
    } else if (!strcmp(cmd, "snooze")) {
        sscanf(spec, "%*s %*u %15s", w);
        d.snz = strcmp(w, "off") ? (uint32_t)strtoul(w, NULL, 10) : 0;
        present |= SF_SNZ;
    } else if (!strcmp(cmd, "notify")) {
        sscanf(spec, "%*s %*u %15s", w);
        d.ntf = !strcmp(w, "on");
        present |= SF_NTF;
    }
    model_upsert(&d, present);
}

/* remote SID HOST [NAME]: mark an existing session as running on a remote host (delta, like the wire) */
static void run_remote(const char *spec)
{
    unsigned sid, host;
    char hn[17] = "";
    if (sscanf(spec, "%u %u %16s", &sid, &host, hn) < 2) {
        s_failed = true;
        return;
    }
    session_t d = {.sid = sid, .host = (uint8_t)host};
    uint32_t present = SF_HOST;
    if (hn[0]) {
        snprintf(d.hn, sizeof d.hn, "%s", hn);
        present |= SF_HN;
    }
    model_upsert(&d, present);
}

static void fail(const char *what, const char *want);

/* ── daemon messages built with the firmware's own codec, so scripts need no hand-made hex ── */

static uint32_t s_rx_id = 100; /* ids of scripted daemon messages; the device does not check them */

static struct zcbor_string zs(const char *str) { return (struct zcbor_string){.value = (const uint8_t *)str, .len = strlen(str)}; }

static void inject_env(struct envelope_r *env, const char *what)
{
    static uint8_t buf[1024];
    size_t len = 0;
    if (cbor_encode_envelope(buf, sizeof buf, env, &len) != ZCBOR_SUCCESS) {
        fail("cannot encode", what);
        return;
    }
    sim_link_inject_rx(buf, len);
}

static int enum_of(const char *s, const char *const *names, int n)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(s, names[i])) return i;
    return -1;
}

/* attention RID SID KIND ACTS GRANT_MASK TITLE|TEXT|OPT... */
static void run_attention(char *spec)
{
    static const char *const kinds[] = {"approval", "question", "error", "done"};
    static const char *const acts[] = {"approve", "deny", "focus", "reply", "ack", "dictate"};
    unsigned rid, sid, grant;
    char kind[12], actl[48];
    int n = 0;
    if (sscanf(spec, "%u %u %11s %47s %u %n", &rid, &sid, kind, actl, &grant, &n) != 5 || enum_of(kind, kinds, 4) < 0) {
        fail("bad attention", spec);
        return;
    }
    static struct envelope_r env;
    memset(&env, 0, sizeof env);
    env.envelope_choice = envelope_msg_attention_m_c;
    env.envelope_msg_attention_m.msg_attention_hdr_m.hdr_uint1uint = ++s_rx_id;
    struct attention *a = &env.envelope_msg_attention_m.msg_attention_attention_m;
    a->attention_uint0uint = rid;
    a->attention_uint1uint = sid;
    a->attention_att_kind_m.att_kind_choice = enum_of(kind, kinds, 4);
    for (char *tok = strtok(actl, ","); tok; tok = strtok(NULL, ",")) {
        int k = enum_of(tok, acts, 6);
        if (k >= 0 && a->attention_action_m_l_action_m_count < 6)
            a->attention_action_m_l_action_m[a->attention_action_m_l_action_m_count++].action_choice = k;
    }
    a->attention_uint8uint_present = true;
    a->attention_uint8uint.attention_uint8uint = grant;
    /* the text block is kept in the event, so the zcbor_strings may point into it */
    char *fields = spec + n, *save = NULL;
    int i = 0;
    for (char *f = strtok_r(fields, "|", &save); f; f = strtok_r(NULL, "|", &save), i++) {
        if (i == 0) {
            a->attention_uint3tstr_present = f[0] != 0;
            a->attention_uint3tstr.attention_uint3tstr = zs(f);
        } else if (i == 1) {
            a->attention_uint4tstr_present = f[0] != 0;
            a->attention_uint4tstr.attention_uint4tstr = zs(f);
        } else if (i < 6) {
            a->attention_tstr_l_present = true;
            a->attention_tstr_l.attention_tstr_l_tstr[a->attention_tstr_l.attention_tstr_l_tstr_count++] = zs(f);
        }
    }
    inject_env(&env, "attention");
}

/* result ok | result err CODE | result pos LINES AT_BOTTOM [TOTAL|- [PREVIEW...]]: answers the device's
 * most recent command. */
static void run_result(char *spec)
{
    static const char *const errs[] = {"stale", "denied", "unsupported", "no_window", "timeout", "internal"};
    char word[12], code[16] = "";
    if (sscanf(spec, "%11s %15s", word, code) < 1) {
        fail("bad result", spec);
        return;
    }
    static struct envelope_r env;
    memset(&env, 0, sizeof env);
    env.envelope_choice = envelope_msg_result_m_c;
    struct msg_result *m = &env.envelope_msg_result_m;
    m->msg_result_hdr_m.hdr_uint1uint = ++s_rx_id;
    m->msg_result_hdr_m.hdr_uint2uint_present = true;
    m->msg_result_hdr_m.hdr_uint2uint.hdr_uint2uint = g_link.tx_id; /* the last command sent */
    m->msg_result_result_m.result_uint0bool = !strcmp(word, "ok") || !strcmp(word, "pos");
    if (!strcmp(word, "pos")) {
        unsigned lines, bottom;
        char total[16] = "-";
        int n = 0;
        if (sscanf(spec, "%*s %u %u %15s %n", &lines, &bottom, total, &n) < 2) {
            fail("bad result pos", spec);
            return;
        }
        m->msg_result_result_m.result_scroll_pos_m_present = true;
        struct scroll_pos *p = &m->msg_result_result_m.result_scroll_pos_m.result_scroll_pos_m;
        p->scroll_pos_uint0uint = lines;
        p->scroll_pos_uint1bool = bottom != 0;
        if (total[0] && strcmp(total, "-")) {
            p->scroll_pos_uint2uint_present = true;
            p->scroll_pos_uint2uint.scroll_pos_uint2uint = (uint32_t)atoi(total);
        }
        if (n > 0 && spec[n]) {
            p->scroll_pos_uint3tstr_present = true;
            p->scroll_pos_uint3tstr.scroll_pos_uint3tstr = zs(spec + n); /* points into the event's text */
        }
    } else if (!m->msg_result_result_m.result_uint0bool) {
        int e = enum_of(code, errs, 6);
        m->msg_result_result_m.result_err_m_present = e >= 0;
        m->msg_result_result_m.result_err_m.result_err_m.result_err_choice = e < 0 ? 0 : e;
    }
    inject_env(&env, "result");
}

/* cfg privacy meta|preview|full */
static void run_cfg(const char *spec)
{
    static const char *const privacy[] = {"meta", "preview", "full"};
    char key[12], val[12];
    int v;
    if (sscanf(spec, "%11s %11s", key, val) != 2 || strcmp(key, "privacy") || (v = enum_of(val, privacy, 3)) < 0) {
        fail("bad cfg", spec);
        return;
    }
    static struct envelope_r env;
    memset(&env, 0, sizeof env);
    env.envelope_choice = envelope_msg_cfg_m_c;
    env.envelope_msg_cfg_m.msg_cfg_hdr_m.hdr_uint1uint = ++s_rx_id;
    struct device_cfg *c = &env.envelope_msg_cfg_m.msg_cfg_device_cfg_m;
    c->device_cfg_privacy_m_present = true;
    c->device_cfg_privacy_m.device_cfg_privacy_m.privacy_choice = v;
    inject_env(&env, "cfg");
}

/* The daemon's answer to the device's current recording (voice.c), as it would arrive. */
static void run_transcript(const char *text)
{
    static struct envelope_r env;
    memset(&env, 0, sizeof env);
    env.envelope_choice = envelope_msg_transcript_m_c;
    env.envelope_msg_transcript_m.msg_transcript_hdr_m.hdr_uint1uint = ++s_rx_id;
    struct transcript *t = &env.envelope_msg_transcript_m.msg_transcript_transcript_m;
    t->transcript_uint0uint = voice_aid();
    t->transcript_uint1tstr = (struct zcbor_string){.value = (const uint8_t *)text, .len = strlen(text)};
    t->transcript_uint2uint = 0;
    inject_env(&env, "transcript");
}

/* cands TYPED|- [err CODE] WORD...: what the daemon's decoder answered for the open keyboard */
static void run_cands(char *spec)
{
    static const char *const errs[] = {"stale", "denied", "unsupported", "no_window", "timeout", "internal"};
    static struct envelope_r env;
    memset(&env, 0, sizeof env);
    env.envelope_choice = envelope_msg_kb_cands_m_c;
    env.envelope_msg_kb_cands_m.msg_kb_cands_hdr_m.hdr_uint1uint = ++s_rx_id;
    struct kb_cands *k = &env.envelope_msg_kb_cands_m.msg_kb_cands_kb_cands_m;
    k->kb_cands_uint0uint = keys_kid();
    k->kb_cands_uint1uint = keys_seq() ? keys_seq() - 1 : 0;
    char *save = NULL, *tok = strtok_r(spec, " ", &save);
    if (!tok) {
        fail("bad cands", spec);
        return;
    }
    if (strcmp(tok, "-")) {
        k->kb_cands_uint3uint_present = true;
        k->kb_cands_uint3uint.kb_cands_uint3uint = (uint32_t)atoi(tok);
    }
    for (tok = strtok_r(NULL, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        if (!strcmp(tok, "err") && (tok = strtok_r(NULL, " ", &save)) != NULL) {
            int e = enum_of(tok, errs, 6);
            k->kb_cands_result_err_m_present = e >= 0;
            k->kb_cands_result_err_m.kb_cands_result_err_m.result_err_choice = e < 0 ? 0 : e;
        } else if (k->kb_cands_tstr_l_tstr_count < 4) {
            k->kb_cands_tstr_l_tstr[k->kb_cands_tstr_l_tstr_count++] = zs(tok);
        }
    }
    inject_env(&env, "kb_cands");
}

static void run_clear(int rid, const char *spec)
{
    static const char *const whys[] = {"answered_pc", "answered_dev", "expired", "cancelled"};
    char why[16] = "answered_pc";
    sscanf(spec, "%*d %15s", why);
    int w = enum_of(why, whys, 4);
    static struct envelope_r env;
    memset(&env, 0, sizeof env);
    env.envelope_choice = envelope_msg_clear_m_c;
    env.envelope_msg_clear_m.msg_clear_hdr_m.hdr_uint1uint = ++s_rx_id;
    env.envelope_msg_clear_m.msg_clear_clear_m.clear_uint0uint = (uint32_t)rid;
    env.envelope_msg_clear_m.msg_clear_clear_m.clear_why_m.clear_why_choice = w < 0 ? 0 : w;
    inject_env(&env, "clear");
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
    case E_CONNECT: sim_link_set_connected_kind(e->b, e->a); break;
    case E_RX: send_hex_kind(e->b, e->text); break;
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
    case E_PX_RESET:
        s_px0 = core_px_flushed();
        s_cpu0 = sim_core_cpu_us();
        break;
    case E_EXPECT_PX_MAX: {
        uint32_t px = core_px_flushed() - s_px0;
        sim_log("px", "\"flushed\":%u,\"max\":%d,\"cpu_us\":%llu", (unsigned)px, e->a,
                (unsigned long long)(sim_core_cpu_us() - s_cpu0)); /* informational: host CPU, not asserted */
        if (px > (uint32_t)e->a) {
            char buf[48];
            snprintf(buf, sizeof buf, "%u px", (unsigned)px);
            fail("redraw over budget", buf);
        }
        break;
    }
    case E_SESSION: run_session(e->text); break;
    case E_JOB: run_job(e->text); break;
    case E_GAUGE: run_gauge(e->text); break;
    case E_USAGE: run_usage(e->text); break;
    case E_DELTA: run_delta(e->text); break;
    case E_REMOTE: run_remote(e->text); break;
    case E_DROP: model_remove((uint32_t)e->a); break;
    case E_ATTENTION: run_attention(e->text); break;
    case E_RESULT: run_result(e->text); break;
    case E_CLEAR: run_clear(e->a, e->text); break;
    case E_MIC:
        if (!sim_mic_load(e->text)) fail("cannot load", e->text);
        break;
    case E_TRANSCRIPT: run_transcript(e->text); break;
    case E_CANDS: run_cands(e->text); break;
    case E_CFG: run_cfg(e->text); break;
    case E_RTC:
        hal_rtc_set_unix((uint32_t)e->a);
        app_clock_resync();
        break;
    case E_QUIT: {
        /* LVGL's heap at the end of the script: on the device these are internal-RAM allocations */
        lv_mem_monitor_t m;
        lv_mem_monitor(&m);
        sim_log("mem", "\"lvgl_used\":%u,\"lvgl_peak\":%u", (unsigned)(m.total_size - m.free_size), (unsigned)m.max_used);
        s_next = s_n;
        break;
    }
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
