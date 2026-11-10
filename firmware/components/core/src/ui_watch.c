/* The WATCH screen (UI-V2 §4.7): a live monitor for one session on the round display. Header, the
 * previous steps as muted rows with a status mark, the current step in the centre (tool large,
 * argument below), a footer with the step count and the time in the working state. When the session
 * is not working the centre says what state it is in instead.
 *
 * The screen follows the deck's selection: the rim and B move the deck's selection through
 * ui_home_act(), and the watched session follows it. The index ring keeps showing the sessions, with
 * the watched one selected, in its state colour. The step timeline itself is built by the model
 * (steps.c) from the session deltas; this file only draws it. Nothing here is destructive and
 * nothing times out. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_home_internal.h"
#include "ui_theme.h"

#define HEADER_Y 80
#define ROWS 3
#define ROW_Y0 118
#define ROW_DY 24
#define ROW_X 78
#define ROW_W 310
#define MARK_W 22
#define TOOL_Y 200
#define ARG_Y 236
#define ARG_W 330
#define FOOTER_Y 306
#define HINT_Y 364

static lv_obj_t *s_root, *s_header, *s_tool, *s_arg, *s_footer, *s_hint;
static lv_obj_t *s_mark[ROWS], *s_row[ROWS];
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static uint32_t s_sid, s_now;
static char s_header_txt[64], s_tool_txt[48], s_arg_txt[48], s_footer_txt[40];
static char s_row_txt[ROWS][STEP_TEXT_MAX + 8];
static const char *s_mark_txt[ROWS];

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void place_lines(lv_obj_t *l, int x, int y, int w, int lines, lv_text_align_t align)
{
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_pos(l, x, y);
}

static bool working(const session_t *s) { return s->state == SESS_WORK || s->state == SESS_START; }

/* "Tool · argument" (the daemon's sum) split at the first separator; a bare tool name has no argument. */
/* Copy at most cap - 1 bytes of src[0..len): the labels truncate with dots anyway. */
static void copy_cut(char *dst, size_t cap, const char *src, size_t len)
{
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

static void split_step(const char *text, char *tool, size_t tcap, char *arg, size_t acap)
{
    const char *sep = strstr(text, " \xC2\xB7 ");
    if (sep) {
        copy_cut(tool, tcap, text, (size_t)(sep - text));
        copy_cut(arg, acap, sep + 4, strlen(sep + 4));
    } else {
        copy_cut(tool, tcap, text, strlen(text));
        arg[0] = 0;
    }
}

static void clock_text(char *dst, size_t cap, uint32_t secs)
{
    if (secs < 3600) snprintf(dst, cap, "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
    else snprintf(dst, cap, "%u:%02u:%02u", (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60), (unsigned)(secs % 60));
}

/* Time in the current state, frozen at the moment the link dropped, like the deck's card. */
static bool time_in_state(const session_t *s, uint32_t *secs)
{
    uint32_t now = theme_now_unix(), from = s->tst ? s->tst : s->t0;
    if (!now || !from) return false;
    now -= ui_link_down_ms() / 1000;
    *secs = now >= from ? now - from : 0;
    return true;
}

static void step_mark(const step_t *st, const char **sym, uint32_t *rgb)
{
    switch (st->state) {
    case STEP_OK: *sym = LV_SYMBOL_OK; *rgb = RGB_DONE; break;
    case STEP_FAIL: *sym = LV_SYMBOL_CLOSE; *rgb = RGB_ERROR; break;
    default: *sym = LV_SYMBOL_PLAY; *rgb = RGB_ACCENT; break;
    }
}

static void render_footer(const session_t *s)
{
    char buf[40], t[16];
    uint32_t secs;
    unsigned n = (unsigned)s->n;
    buf[0] = 0;
    if (working(s) && time_in_state(s, &secs)) {
        clock_text(t, sizeof t, secs);
        snprintf(buf, sizeof buf, "%u step%s%s%s", n, n == 1 ? "" : "s", theme_sep(), t);
    } else if (n > 0 || working(s)) {
        snprintf(buf, sizeof buf, "%u step%s", n, n == 1 ? "" : "s");
    }
    theme_set_text(s_footer, s_footer_txt, sizeof s_footer_txt, buf);
}

static void render(void)
{
    const session_t *s = model_find(s_sid);
    if (!s) return;
    char buf[64], host[24];
    theme_host_prefix(host, sizeof host, s);
    const char *base = theme_basename(s->repo);
    snprintf(buf, sizeof buf, "%s%s%s%s", host, theme_agent_name(s->agent), theme_sep(), base[0] ? base : "(no folder)");
    theme_set_text(s_header, s_header_txt, sizeof s_header_txt, buf);

    const steps_t *t = &s->steps;
    char tool[48] = "", arg[48] = "";
    uint32_t rgb = theme_session_rgb(s);
    uint32_t tool_rgb = rgb;
    int prior = t->count; /* how many timeline rows precede the centre */
    switch (s->state) {
    case SESS_WORK:
    case SESS_START: {
        const char *cur = s->summary[0] ? s->summary : s->tool[0] ? s->tool : t->count ? t->s[t->count - 1].text : "";
        if (cur[0]) split_step(cur, tool, sizeof tool, arg, sizeof arg);
        else snprintf(tool, sizeof tool, "%s", theme_state_word(s));
        tool_rgb = cur[0] ? 0xe6edf3 : rgb;
        if (prior > 0) prior--; /* the newest step is the centre */
        break;
    }
    case SESS_DONE: {
        uint32_t secs;
        char ago[16] = "";
        if (time_in_state(s, &secs)) {
            theme_elapsed(ago, sizeof ago, secs);
            snprintf(tool, sizeof tool, "Finished%s%s ago", theme_sep(), ago);
        } else {
            snprintf(tool, sizeof tool, "Finished");
        }
        break;
    }
    case SESS_ERROR: snprintf(tool, sizeof tool, "Error"); break;
    case SESS_INPUT:
        snprintf(tool, sizeof tool, "Waiting for you");
        snprintf(arg, sizeof arg, "tap to answer");
        break;
    default: snprintf(tool, sizeof tool, "%s", theme_state_word(s)); break;
    }
    theme_set_text(s_tool, s_tool_txt, sizeof s_tool_txt, tool);
    theme_set_text_color(s_tool, lv_color_hex(tool_rgb));
    theme_set_text(s_arg, s_arg_txt, sizeof s_arg_txt, arg);

    /* the rows sit against the centre: with fewer steps the top slots stay empty */
    if (prior > ROWS) prior = ROWS;
    int first = t->count - (working(s) ? 1 : 0) - prior;
    for (int slot = 0; slot < ROWS; slot++) {
        int k = slot - (ROWS - prior);
        if (k < 0) {
            show(s_mark[slot], false);
            show(s_row[slot], false);
            s_row_txt[slot][0] = 0;
            s_mark_txt[slot] = "";
            continue;
        }
        const step_t *st = &t->s[first + k];
        const char *sym;
        uint32_t mark_rgb;
        step_mark(st, &sym, &mark_rgb);
        const char *mark = st->state == STEP_OK ? "ok" : st->state == STEP_FAIL ? "fail" : "run";
        if (s_mark_txt[slot] != mark) {
            lv_label_set_text(s_mark[slot], sym);
            theme_set_text_color(s_mark[slot], lv_color_hex(mark_rgb));
            s_mark_txt[slot] = mark;
        }
        char shown[STEP_TEXT_MAX + 8];
        theme_display_text(shown, sizeof shown, st->text);
        theme_set_text(s_row[slot], s_row_txt[slot], sizeof s_row_txt[slot], shown);
        show(s_mark[slot], true);
        show(s_row[slot], true);
    }
    render_footer(s);

    int n_sessions = ui_home_slot_count();
    show(s_badge[1], n_sessions > 1);
    show(s_hint, !ui_toast_active());
}

uint32_t ui_watch_sid(void) { return s_sid; }

/* The deck's selection moved (rim, B): the watch follows it. */
static void follow_deck(void)
{
    uint32_t sel = ui_home_sel_sid();
    if (sel && sel != s_sid) {
        s_sid = sel;
        dbg_event("ui", "watch", "\"sid\":%u,\"via\":\"deck\"", (unsigned)sel);
        render();
    }
}

void ui_watch_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_now = now;
    s_header_txt[0] = s_tool_txt[0] = s_arg_txt[0] = s_footer_txt[0] = 0;
    ui_home_refresh(now); /* the index ring shows the sessions again, the watched one selected */
    if (g_ui_actions->sectors) g_ui_actions->sectors(ui_home_slot_count());
    render();
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    dbg_event("ui", "watch", "\"sid\":%u,\"via\":\"open\"", (unsigned)sid);
}

void ui_watch_close(void)
{
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_sid = 0;
}

/* Returns false when the watched session is gone (or ended): the caller leaves the screen. */
bool ui_watch_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = model_find(s_sid);
    if (!s || s->state == SESS_ENDED) return false;
    if (ui_home_sel_sid() != s_sid) ui_home_select_sid(s_sid, "watch"); /* the deck re-selected on its own */
    render();
    return true;
}

bool ui_watch_act(nav_action_t a, const gesture_evt_t *g)
{
    switch (a) {
    case NAV_FOCUS: /* the deck's default action: Done also acknowledges */
    case NAV_PICK_RIM:
    case NAV_SCRUB:
    case NAV_NEXT:
    case NAV_PREV: {
        bool used = ui_home_act(a, g);
        follow_deck();
        return used;
    }
    default: return false;
    }
}

void ui_watch_tick(uint32_t now)
{
    s_now = now;
    const session_t *s = model_find(s_sid);
    if (!s) return;
    render_footer(s);
    show(s_hint, !ui_toast_active());
}

void ui_watch_report(ui_report_t *r)
{
    r->has_sel_sid = model_find(s_sid) != NULL;
    r->sel_sid = s_sid;
    snprintf(r->screen, sizeof r->screen, "watch");
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    for (int i = 0; i < ROWS; i++)
        if (!lv_obj_has_flag(s_row[i], LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "[%s] %s", s_mark_txt[i], s_row_txt[i]);
    if (s_tool_txt[0]) ui_report_add(r, "%s", s_tool_txt);
    if (s_arg_txt[0]) ui_report_add(r, "%s", s_arg_txt);
    if (s_footer_txt[0]) ui_report_add(r, "%s", s_footer_txt);
}

lv_obj_t *ui_watch_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_header = theme_label(s_root, 16, COL_MUTED);
    place_lines(s_header, UI_CX - 140, HEADER_Y, 280, 1, LV_TEXT_ALIGN_CENTER);
    for (int i = 0; i < ROWS; i++) {
        int y = ROW_Y0 + i * ROW_DY;
        s_mark[i] = theme_label(s_root, 16, COL_MUTED);
        lv_label_set_long_mode(s_mark[i], LV_LABEL_LONG_MODE_CLIP);
        place_lines(s_mark[i], ROW_X, y, MARK_W, 1, LV_TEXT_ALIGN_LEFT);
        s_row[i] = theme_label(s_root, 16, COL_MUTED);
        place_lines(s_row[i], ROW_X + MARK_W, y, ROW_W - MARK_W, 1, LV_TEXT_ALIGN_LEFT);
        show(s_mark[i], false);
        show(s_row[i], false);
    }
    s_tool = theme_label(s_root, 24, COL_TEXT);
    place_lines(s_tool, UI_CX - 165, TOOL_Y, 330, 1, LV_TEXT_ALIGN_CENTER);
    s_arg = theme_label(s_root, 20, COL_MUTED);
    place_lines(s_arg, UI_CX - ARG_W / 2, ARG_Y, ARG_W, 2, LV_TEXT_ALIGN_CENTER);
    s_footer = theme_label(s_root, 20, COL_MUTED);
    place_lines(s_footer, UI_CX - 125, FOOTER_Y, 250, 1, LV_TEXT_ALIGN_CENTER);
    s_hint = theme_label(s_root, 16, COL_MUTED);
    place_lines(s_hint, UI_CX - 150, HINT_Y, 300, 1, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(s_hint, "swipe up to go back");
    static const char *const words[2] = {"FOCUS", "NEXT"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
