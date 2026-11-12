/* The WATCH screen (UI-V2 §4.7): a live monitor for one session on the round display. Header, the
 * previous steps as muted rows with a status mark, the current step in the centre (tool large,
 * argument below), a footer with the step and the time in the working state. When the session is not
 * working the centre says what state it is in (icon, word, caption, Answer button) instead.
 *
 * The screen follows the deck's selection: the rim and B move the deck's selection through
 * ui_home_act(), and the watched session follows it. The index ring keeps showing the sessions, with
 * the watched one selected, in its state colour. The step timeline itself is built by the model
 * (steps.c) from the session deltas; this file only draws it. Nothing here is destructive and
 * nothing times out. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "done.h"
#include "ui_home_internal.h"
#include "ui_theme.h"

#define ROWS 3
#define ROW_Y0 108
#define ROW_DY 26
#define MARK_W 24
#define TOOL_Y 210
#define ARG_Y 252
#define FOOTER_Y 322
#define FOOTER_W 352 /* the chord at y 332 less a margin; the done summary's budget */
#define ICON_Y 176
#define WORD_Y 236
#define SUB_Y 278
#define BTN_Y 318
#define BTN_W 156
#define BTN_H 36

static lv_obj_t *s_root, *s_header, *s_tool, *s_arg, *s_footer, *s_icon, *s_word_l, *s_sub, *s_btn, *s_btn_l;
static lv_obj_t *s_mark[ROWS], *s_row[ROWS];
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static uint32_t s_sid, s_now;
static int s_blockx = -1, s_blockw;
static char s_header_txt[64], s_tool_txt[48], s_arg_txt[48], s_footer_txt[96], s_word_txt[24], s_sub_txt[48];
static char s_row_txt[ROWS][STEP_TEXT_MAX + 8];
static const char *s_mark_txt[ROWS];

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static bool working(const session_t *s) { return s->state == SESS_WORK || s->state == SESS_START; }

/* Copy at most cap - 1 bytes of src[0..len): the labels truncate with dots anyway. */
static void copy_cut(char *dst, size_t cap, const char *src, size_t len)
{
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

/* "Tool · argument" (the daemon's sum) split at the first separator; a bare tool name has no argument. */
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
    case STEP_OK: *sym = ICON_DONE; *rgb = RGB_DONE; break;
    case STEP_FAIL: *sym = ICON_DENIED; *rgb = RGB_ERROR; break;
    default: *sym = ICON_WORKING; *rgb = RGB_ACCENT; break;
    }
}

static int measure_caption(const char *text)
{
    lv_point_t sz;
    lv_text_get_size(&sz, text, FONT_CAPTION, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

static void render_footer(const session_t *s)
{
    char buf[96], t[16];
    uint32_t secs;
    unsigned n = (unsigned)s->n;
    buf[0] = 0;
    /* a finished session: the done summary (DONE-SUMMARY.md) in the footer's place, one tone brighter */
    bool sum = (s->state == SESS_DONE || s->state == SESS_ERROR) && s->agent != AGENT_SHELL && done_known(&s->sum);
    if (sum) {
        done_line_t l;
        done_line_build(&s->sum, theme_sep(), FOOTER_W, measure_caption, &l);
        done_line_text(&l, buf, sizeof buf);
    }
    theme_set_text_color(s_footer, sum ? COL_TEXT2 : COL_TEXT3);
    if (working(s)) {
        if (time_in_state(s, &secs)) {
            clock_text(t, sizeof t, secs);
            if (n) snprintf(buf, sizeof buf, "Step %u%s%s", n, theme_sep(), t);
            else snprintf(buf, sizeof buf, "%s", t);
        } else if (n) {
            snprintf(buf, sizeof buf, "Step %u", n);
        }
    }
    theme_set_text(s_footer, s_footer_txt, sizeof s_footer_txt, buf);
}

/* The caption under the state word: how long ago it happened, in the words of the state. */
static void state_caption(const session_t *s, char *dst, size_t cap)
{
    uint32_t secs;
    char t[16];
    dst[0] = 0;
    const char *lead = s->state == SESS_DONE ? "Finished" : s->state == SESS_ERROR ? "Stopped"
                       : s->state == SESS_INPUT ? "Waiting" : s->state == SESS_IDLE ? "No activity for" : "";
    if (!lead[0]) return;
    if (!time_in_state(s, &secs)) {
        if (s->state == SESS_DONE || s->state == SESS_ERROR) snprintf(dst, cap, "%s", lead);
        return;
    }
    theme_elapsed(t, sizeof t, secs);
    if (s->state == SESS_DONE || s->state == SESS_ERROR) snprintf(dst, cap, "%s %s ago", lead, t);
    else snprintf(dst, cap, "%s %s", lead, t);
}

/* The earlier steps are one left-aligned block, centred as a whole on its widest row. */
static void place_rows(int prior)
{
    int maxw = 0;
    for (int i = 0; i < prior; i++) {
        lv_point_t sz;
        lv_text_get_size(&sz, s_row_txt[i], FONT_CAPTION, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x > maxw) maxw = sz.x;
    }
    int cap = theme_chord(ROW_Y0, 24) - MARK_W; /* the top row is the narrowest */
    if (maxw > cap) maxw = cap;
    int w = maxw + MARK_W, x = UI_CX - w / 2;
    if (x == s_blockx && w == s_blockw) return;
    s_blockx = x;
    s_blockw = w;
    for (int i = 0; i < ROWS; i++) {
        lv_obj_set_x(s_mark[i], x);
        lv_obj_set_x(s_row[i], x + MARK_W);
        lv_obj_set_width(s_row[i], maxw + 2);
    }
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
    bool work = working(s);
    char tool[48] = "", arg[48] = "";
    int prior = work ? t->count : 0; /* timeline rows that precede the centre */
    if (work) {
        const char *cur = s->summary[0] ? s->summary : s->tool[0] ? s->tool : t->count ? t->s[t->count - 1].text : "";
        if (cur[0]) split_step(cur, tool, sizeof tool, arg, sizeof arg);
        else snprintf(tool, sizeof tool, "%s", theme_state_word(s));
        if (prior > 0) prior--; /* the newest step is the centre */
    }
    theme_set_text(s_tool, s_tool_txt, sizeof s_tool_txt, tool);
    theme_set_text(s_arg, s_arg_txt, sizeof s_arg_txt, arg);
    show(s_tool, work);
    show(s_arg, work);

    /* the rows start at y 108; with fewer steps the lower slots stay empty */
    if (prior > ROWS) prior = ROWS;
    int first = t->count - (work ? 1 : 0) - prior;
    for (int slot = 0; slot < ROWS; slot++) {
        int k = slot; /* from y 108 down, in order */
        if (k >= prior) {
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
    if (prior) place_rows(prior);
    render_footer(s);

    /* not working: the state's icon, its word, how long ago, and the Answer button when it needs you */
    uint32_t rgb = theme_session_rgb(s);
    char sub[48];
    state_caption(s, sub, sizeof sub);
    show(s_icon, !work);
    show(s_word_l, !work);
    show(s_sub, !work && sub[0]);
    bool needs = !work && s->state == SESS_INPUT;
    show(s_btn, needs);
    if (!work) {
        lv_label_set_text(s_icon, theme_state_glyph(s));
        theme_set_text_color(s_icon, lv_color_hex(rgb));
        theme_set_text(s_word_l, s_word_txt, sizeof s_word_txt, theme_state_word(s));
        theme_set_text(s_sub, s_sub_txt, sizeof s_sub_txt, sub);
        theme_set_bg_color(s_btn, lv_color_hex(rgb));
    }
    show(s_badge[1], ui_home_slot_count() > 1);
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
    s_header_txt[0] = s_tool_txt[0] = s_arg_txt[0] = s_footer_txt[0] = s_word_txt[0] = s_sub_txt[0] = 0;
    s_blockx = -1;
    ui_home_refresh(now); /* the index ring shows the sessions again, the watched one selected */
    ui_home_rim_mode(false);
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
}

void ui_watch_report(ui_report_t *r)
{
    r->has_sel_sid = model_find(s_sid) != NULL;
    r->sel_sid = s_sid;
    snprintf(r->screen, sizeof r->screen, "watch");
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    for (int i = 0; i < ROWS; i++)
        if (!lv_obj_has_flag(s_row[i], LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "[%s] %s", s_mark_txt[i], s_row_txt[i]);
    if (!lv_obj_has_flag(s_tool, LV_OBJ_FLAG_HIDDEN) && s_tool_txt[0]) ui_report_add(r, "%s", s_tool_txt);
    if (!lv_obj_has_flag(s_arg, LV_OBJ_FLAG_HIDDEN) && s_arg_txt[0]) ui_report_add(r, "%s", s_arg_txt);
    if (!lv_obj_has_flag(s_word_l, LV_OBJ_FLAG_HIDDEN) && s_word_txt[0]) ui_report_add(r, "%s", s_word_txt);
    if (!lv_obj_has_flag(s_sub, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_sub_txt);
    if (!lv_obj_has_flag(s_btn, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "Answer");
    if (s_footer_txt[0]) ui_report_add(r, "%s", s_footer_txt);
}

/* A one-font label at a fixed place; long text ends in dots, never on the ring. */
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, int y, int w, int lines, lv_text_align_t al)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_align(l, al, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(f));
    lv_obj_set_pos(l, UI_CX - w / 2, y);
    return l;
}

lv_obj_t *ui_watch_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_header = label(s_root, FONT_CAPTION, COL_TEXT2, UI_Y_CAPTION, 280, 1, LV_TEXT_ALIGN_CENTER);
    for (int i = 0; i < ROWS; i++) {
        int y = ROW_Y0 + i * ROW_DY;
        s_mark[i] = label(s_root, FONT_ICON_S, COL_TEXT2, y, MARK_W, 1, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_y(s_mark[i], y);
        s_row[i] = label(s_root, FONT_CAPTION, COL_TEXT2, y + 2, 250, 1, LV_TEXT_ALIGN_LEFT);
        show(s_mark[i], false);
        show(s_row[i], false);
    }
    s_tool = label(s_root, FONT_TITLE, COL_TEXT, TOOL_Y, theme_chord(TOOL_Y + 20, 24), 1, LV_TEXT_ALIGN_CENTER);
    s_arg = label(s_root, FONT_BODY, COL_TEXT2, ARG_Y, theme_chord(ARG_Y + 50, 24), 2, LV_TEXT_ALIGN_CENTER);
    s_footer = label(s_root, FONT_CAPTION, COL_TEXT3, FOOTER_Y, FOOTER_W, 1, LV_TEXT_ALIGN_CENTER);
    s_icon = label(s_root, FONT_ICON_L, COL_TEXT, ICON_Y, 80, 1, LV_TEXT_ALIGN_CENTER);
    s_word_l = label(s_root, FONT_TITLE, COL_TEXT, WORD_Y, theme_chord(WORD_Y + 20, 24), 1, LV_TEXT_ALIGN_CENTER);
    s_sub = label(s_root, FONT_CAPTION, COL_TEXT2, SUB_Y, 280, 1, LV_TEXT_ALIGN_CENTER);
    /* the button is only the affordance: the tap on the centre already opens ASK */
    s_btn = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_btn);
    lv_obj_set_size(s_btn, BTN_W, BTN_H);
    lv_obj_set_pos(s_btn, UI_CX - BTN_W / 2, BTN_Y);
    lv_obj_set_style_radius(s_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_btn, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_btn, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_btn_l = lv_label_create(s_btn);
    lv_obj_set_style_text_font(s_btn_l, FONT_BODY_STRONG, 0);
    lv_obj_set_style_text_color(s_btn_l, COL_ON_ACCENT, 0);
    lv_label_set_text(s_btn_l, "Answer");
    lv_obj_center(s_btn_l);
    show(s_btn, false);
    static const char *const words[2] = {"Open", "Next"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
