#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "hal.h"
#include "link.h"
#include "link_internal.h"
#include "power.h"
#include "ui.h"

#define SHOT_CHUNK 1024
#define SHOT_CHUNKS_PER_STEP 4
#define Q_SLOTS 4
#define Q_LEN 160

static dbg_level_t s_level = DBG_INFO;
static char s_q[Q_SLOTS][Q_LEN];
static unsigned s_q_next;

static struct { bool active; uint32_t offset, re; } s_shot;

void dbg_set_level(dbg_level_t lvl) { s_level = lvl; }

const char *dbg_q(const char *s)
{
    char *o = s_q[s_q_next++ % Q_SLOTS];
    size_t n = 0;
    o[n++] = '"';
    for (; s && *s && n < Q_LEN - 8; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { o[n++] = '\\'; o[n++] = (char)c; }
        else if (c < 0x20) n += (size_t)snprintf(o + n, 7, "\\u%04x", c);
        else o[n++] = (char)c;
    }
    o[n++] = '"';
    o[n] = 0;
    return o;
}

static void emit(dbg_level_t lvl, const char *cat, const char *ev, const char *fmt, va_list ap)
{
    if (lvl > s_level) return;
    char line[384];
    int n = snprintf(line, sizeof line, "{\"ts_us\":%llu,\"cat\":\"%s\",\"ev\":\"%s\"", (unsigned long long)hal_time_us(),
                     cat, ev);
    if (fmt && n > 0 && (size_t)n < sizeof line - 8) {
        n += snprintf(line + n, sizeof line - (size_t)n, ",");
        n += vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    }
    if (n < 0 || (size_t)n >= sizeof line - 2) n = (int)sizeof line - 3; /* truncated: still emit something parseable */
    line[n] = '}';
    line[n + 1] = 0;
    hal_debug_line(line);
}

void dbg_event_at(dbg_level_t lvl, const char *cat, const char *ev, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(lvl, cat, ev, fmt, ap);
    va_end(ap);
}

void dbg_event(const char *cat, const char *ev, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(DBG_INFO, cat, ev, fmt, ap);
    va_end(ap);
}

static void current_report(ui_report_t *r)
{
    ui_report(r);
    r->fps = app_fps();
    r->power = power_name();
}

void dbg_dump_ui_state(void)
{
    ui_report_t r;
    current_report(&r);
    char vis[8 * 52 + 8] = "";
    size_t n = 0;
    for (size_t i = 0; i < r.n_visible; i++)
        n += (size_t)snprintf(vis + n, sizeof vis - n, "%s%s", i ? "," : "", dbg_q(r.visible[i]));
    dbg_event("ui", "state", "\"screen\":\"%s\",\"power\":\"%s\",\"sel_sid\":%u,\"visible\":[%s]", r.screen, r.power,
              (unsigned)(r.has_sel_sid ? r.sel_sid : 0), vis);
}

void dbg_on_ui_state_req(uint32_t re)
{
    ui_report_t r;
    current_report(&r);
    dbg_event("debug", "ui_state_req", "\"re\":%u", (unsigned)re);
    link_send_ui_state(&r, re);
}

void dbg_on_screenshot_req(uint32_t re)
{
    dbg_event("debug", "screenshot_req", "\"re\":%u", (unsigned)re);
    s_shot.active = true;
    s_shot.offset = 0;
    s_shot.re = re;
}

void dbg_on_inject(int input_event, bool has_xy, int x, int y)
{
    static const gesture_kind_t map[] = {G_TAP, G_DOUBLE_TAP, G_LONG_PRESS, G_SWIPE_UP, G_SWIPE_DOWN, G_SWIPE_LEFT,
                                         G_SWIPE_RIGHT, G_BTN_A, G_BTN_B, G_BTN_A_LONG, G_BTN_B_LONG, G_LIFT, G_SHAKE};
    if (input_event < 0 || input_event >= (int)(sizeof map / sizeof map[0])) return;
    gesture_evt_t g = {.kind = map[input_event], .x = (int16_t)(has_xy ? x : 233), .y = (int16_t)(has_xy ? y : 233)};
    if (g.kind == G_LONG_PRESS) g.dur_ms = 1200; /* an injected long press counts as a completed hold */
    dbg_event("debug", "inject", "\"kind\":\"%s\",\"x\":%d,\"y\":%d", gesture_name(g.kind), g.x, g.y);
    app_inject_gesture(&g);
}

void dbg_tick(uint32_t now_ms)
{
    (void)now_ms;
    if (!s_shot.active) return;
    const hal_board_info_t *bi = hal_board_info();
    for (int i = 0; i < SHOT_CHUNKS_PER_STEP && s_shot.active; i++) {
        uint8_t buf[SHOT_CHUNK];
        size_t n = hal_display_snapshot(s_shot.offset, buf, sizeof buf);
        bool last = n < sizeof buf;
        if (!link_send_screenshot_chunk(bi->width, bi->height, s_shot.offset, buf, n, last, s_shot.re)) {
            s_shot.active = false;
            return;
        }
        s_shot.offset += (uint32_t)n;
        if (last) {
            s_shot.active = false;
            dbg_event("debug", "screenshot_done", "\"bytes\":%u", (unsigned)s_shot.offset);
        }
    }
}
