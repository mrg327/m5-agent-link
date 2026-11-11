/* The deck's centre (UI-V3 §4.1). Three looks share one set of labels: the session card (caption, subject,
 * state row, summary or request, exception slot, legends), the empty deck (clock and a line) and the
 * ambient face (clock and one caption, dimmed). Pure presentation: it reads the model and the deck's
 * selection (ui_home_internal.h); slots, selection and actions live in ui_home.c. Redraw discipline
 * (UI-V2 §8a): only change-only setters, and the clock text changes once a minute. */
#include <stdio.h>
#include <string.h>

#include "hal.h"
#include "ui_home_internal.h"
#include "ui_theme.h"

/* Row anchors (centres unless noted), UI-V3 §4.1. */
#define TITLE_Y 146
#define TITLE_W 320
#define STATE_Y 196
#define SUMMARY_TOP 232 /* top of the first line */
#define SUMMARY_W 320
#define SLOT_Y 320 /* spec 318: two px lower so a 3-line request clears the button */
#define REQ_TOP 220 /* a request is one title line and two text lines, 75 px: it starts higher than a summary */
#define CLOCK_Y 200
#define SUB_Y 262
#define BTN_W 160
#define BTN_H 44
#define BATT_LOW 15
#define AMBIENT_GREY 0x737373 /* white at 45 % */

typedef enum { LOOK_CARD, LOOK_EMPTY, LOOK_AMBIENT } look_t;

static lv_obj_t *s_centre, *s_cap, *s_title, *s_srow, *s_sicon, *s_sword, *s_stime, *s_sum, *s_req_title, *s_req_text;
static lv_obj_t *s_chip, *s_chip_icon, *s_chip_txt, *s_btn, *s_btn_lbl, *s_quiet;
static lv_obj_t *s_clock, *s_ready, *s_sub;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2]; /* [0] = A, [1] = B */

static bool s_ambient, s_offline_dim, s_quiet_shown, s_clock_set;
static uint32_t s_minute, s_batt_next;
static look_t s_look = LOOK_CARD;

static char s_cap_txt[64], s_title_txt[40], s_sicon_txt[8], s_sword_txt[24], s_stime_txt[40], s_sum_txt[88], s_chip_icon_txt[8],
    s_chip_txt_txt[32], s_btn_txt[16], s_clock_txt[8], s_sub_txt[64], s_req_title_txt[48], s_req_text_txt[168];

static lv_obj_t *mk(lv_obj_t *parent, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(l, "");
    return l;
}

/* Fixed height of `lines` rows: LONG_MODE_DOTS only truncates when the height is fixed. `top` is the
 * first row's top edge. */
static void put_top(lv_obj_t *l, int top, int w, int lines)
{
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, UI_CX - w / 2, top);
}

static void put_mid(lv_obj_t *l, int cy, int w, int lines)
{
    put_top(l, cy - lines * lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)) / 2, w, lines);
}

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static bool offline(void) { return !ui_link_connected(); }

/* The wall clock in local time; false until it has been set (the RTC reads 0 before that). */
static bool local_clock(unsigned *h, unsigned *m, uint32_t *minute)
{
    uint32_t now = theme_now_unix();
    if (now <= 100000) return false;
    uint32_t local = now + (uint32_t)((int32_t)model_settings()->tz_off_min * 60);
    *h = local / 3600 % 24;
    *m = local / 60 % 60;
    *minute = local / 60;
    return true;
}

/* Link row: transport · host · battery while connected; otherwise how stale the snapshot is. A
 * leading mute glyph says quiet mode is on (STATUS has the details). */
void ui_home_link_text(char *dst, size_t cap, bool with_battery, uint32_t *rgb)
{
    bool quiet = model_quiet_active(theme_now_unix());
    const char *q = quiet ? ICON_QUIET "  " : "";
    int k = ui_link_transport();
    const char *sym = k == HAL_LINK_USB ? ICON_USB : k == HAL_LINK_BLE ? ICON_BLUETOOTH : ICON_LINK;
    const char *name = k == HAL_LINK_USB ? "USB" : k == HAL_LINK_BLE ? "BLE" : "PC";
    uint32_t down = ui_link_down_ms();
    if (!ui_link_connected() && down && model_count() > 0) {
        char age[16];
        theme_elapsed(age, sizeof age, down / 1000);
        snprintf(dst, cap, "%s%s Offline %s stale %s", q, ICON_OFFLINE, theme_sep(), age);
        *rgb = RGB_AMBER;
    } else if (k < 0) {
        snprintf(dst, cap, "%s%s Offline", q, ICON_OFFLINE);
        *rgb = RGB_AMBER;
    } else if (!ui_link_connected()) {
        snprintf(dst, cap, "%s%s %s...", q, sym, name);
        *rgb = RGB_AMBER;
    } else {
        const char *host = model_host();
        hal_battery_t b = hal_battery_read();
        char batt[16] = "";
        if (with_battery && b.percent <= 100)
            snprintf(batt, sizeof batt, "%s%s%u%%", theme_sep(), b.charging ? ICON_BATTERY_CHARGING " " : "", b.percent);
        if (host[0]) snprintf(dst, cap, "%s%s %s%s%.12s%s", q, sym, name, theme_sep(), host, batt);
        else snprintf(dst, cap, "%s%s %s%s", q, sym, name, batt);
        *rgb = RGB_SUCCESS;
    }
}


/* One-line subject: FONT_TITLE when it fits, else FONT_HEAD on two lines, centred on TITLE_Y. */
static void set_title(const char *t)
{
    if (strcmp(s_title_txt, t) == 0) return;
    lv_point_t sz;
    lv_text_get_size(&sz, t, FONT_TITLE, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    bool two = sz.x > TITLE_W;
    lv_obj_set_style_text_font(s_title, two ? FONT_HEAD : FONT_TITLE, 0);
    put_mid(s_title, TITLE_Y, TITLE_W, two ? 2 : 1);
    theme_set_text(s_title, s_title_txt, sizeof s_title_txt, t);
}

/* Exception slot (§4.1), by precedence: Offline chip, low battery chip, the Answer / Review button,
 * a lone quiet glyph, nothing. `btn` is the button's text and colour when the session needs you. */
static void update_slot(const char *btn, uint32_t btn_rgb)
{
    char txt[32] = "";
    const char *icon = "";
    uint32_t rgb = 0;
    uint32_t down = ui_link_down_ms();
    int k = ui_link_transport();
    if (offline()) {
        rgb = RGB_AMBER;
        icon = ICON_OFFLINE;
        if (down && model_count() > 0) {
            char age[16];
            theme_elapsed(age, sizeof age, down / 1000);
            snprintf(txt, sizeof txt, "Offline%s%s", theme_sep(), age);
        } else if (k < 0) {
            snprintf(txt, sizeof txt, "Offline");
        } else {
            icon = k == HAL_LINK_USB ? ICON_USB : k == HAL_LINK_BLE ? ICON_BLUETOOTH : ICON_LINK;
            snprintf(txt, sizeof txt, "Connecting" "\xE2\x80\xA6");
        }
    } else {
        hal_battery_t b = hal_battery_read();
        if (b.percent <= BATT_LOW && !b.charging) {
            rgb = RGB_ERROR;
            icon = ICON_BATTERY_WARN;
            snprintf(txt, sizeof txt, "%u%%", b.percent);
        }
    }
    bool chip = txt[0] != 0 && s_look != LOOK_AMBIENT; /* the ambient face is the clock and one caption */
    s_quiet_shown = model_quiet_active(theme_now_unix());
    bool button = !chip && btn && s_look == LOOK_CARD;
    show(s_chip, chip);
    show(s_btn, button);
    show(s_quiet, !chip && !button && s_quiet_shown && s_look != LOOK_AMBIENT);
    if (chip) {
        theme_set_text(s_chip_icon, s_chip_icon_txt, sizeof s_chip_icon_txt, icon);
        theme_set_text(s_chip_txt, s_chip_txt_txt, sizeof s_chip_txt_txt, txt);
        theme_set_text_color(s_chip_icon, lv_color_hex(rgb));
        theme_set_text_color(s_chip_txt, lv_color_hex(rgb));
    }
    if (button) {
        theme_set_bg_color(s_btn, lv_color_hex(btn_rgb));
        theme_set_text(s_btn_lbl, s_btn_txt, sizeof s_btn_txt, btn);
    }
}

/* Time in the current state (UI-V2 §10). While offline the clock is frozen at the moment the link
 * dropped, so a stale card does not keep counting as if it were live. */
static void update_time(const session_t *s, bool merged)
{
    uint32_t now = theme_now_unix() - ui_link_down_ms() / 1000, from = s->tst ? s->tst : s->t0;
    char buf[40] = "";
    if (!merged && from && now >= from) theme_elapsed_long(buf, sizeof buf, now - from);
    if (!merged && model_session_snoozed(s, theme_now_unix())) {
        size_t n = strlen(buf);
        snprintf(buf + n, sizeof buf - n, "%ssnoozed", n ? theme_sep() : "");
    }
    theme_set_text(s_stime, s_stime_txt, sizeof s_stime_txt, buf);
}

/* One opacity for the centre: ambient without a clock keeps the v2 dimmed card, offline halves it. */
static void apply_dim(void)
{
    bool v2 = s_ambient && s_look == LOOK_CARD;
    theme_set_opa(s_centre, v2 ? LV_OPA_30 : s_offline_dim ? LV_OPA_50 : LV_OPA_COVER);
}

/* Ambient burn-in guard: the centre drifts 1 px each minute, round a 2 x 2 square. */
static void apply_shift(void)
{
    int step = s_ambient ? (int)(s_minute & 3) : 0;
    int x = s_ambient && (step == 1 || step == 2), y = s_ambient && step >= 2;
    if (lv_obj_get_x(s_centre) != x || lv_obj_get_y(s_centre) != y) lv_obj_set_pos(s_centre, x, y);
}

static void set_looks(look_t look)
{
    s_look = look;
    bool card = look == LOOK_CARD;
    show(s_cap, card);
    show(s_title, card);
    show(s_srow, card);
    show(s_clock, look != LOOK_CARD && s_clock_set);
    show(s_ready, look == LOOK_EMPTY && !s_clock_set);
    show(s_sub, !card);
    if (!card) {
        show(s_sum, false);
        show(s_req_title, false);
        show(s_req_text, false);
    }
}

static void draw_clock(void)
{
    unsigned h, m;
    char buf[8] = "";
    s_clock_set = local_clock(&h, &m, &s_minute);
    if (s_clock_set) snprintf(buf, sizeof buf, "%02u:%02u", h, m);
    theme_set_text(s_clock, s_clock_txt, sizeof s_clock_txt, buf);
}

void ui_home_card_set_ambient(bool ambient)
{
    s_ambient = ambient;
    ui_home_card_update();
}

void ui_home_card_update(void)
{
    char buf[64];
    s_offline_dim = offline() && model_count() > 0;
    draw_clock();
    const session_t *s = ui_home_sel_session();
    int n = ui_home_slot_count();
    bool dark = s_ambient && s_clock_set;
    lv_color_t sub_col = dark ? lv_color_hex(AMBIENT_GREY) : COL_TEXT2;

    if (!s || n == 0) {
        set_looks(LOOK_EMPTY);
        theme_set_text(s_sub, s_sub_txt, sizeof s_sub_txt, "No agents running");
        theme_set_text_color(s_sub, sub_col);
        theme_set_text_color(s_clock, dark ? sub_col : COL_TEXT);
        theme_set_text_color(s_ready, dark ? sub_col : COL_TEXT);
        update_slot(NULL, 0);
        for (int i = 0; i < 2; i++) show(s_badge[i], false);
        apply_dim();
        apply_shift();
        return;
    }
    const home_slot_t *sl = ui_home_sel_slot();
    const attention_t *req = ui_home_askable_of(s);
    /* The request decides what the user is told: its kind, not the session's sub-state, names it. */
    const char *word = sl->n_merged ? "Done" : req ? (req->kind == ATT_APPROVAL ? "Needs approval" : "Has a question")
                       : s->state == SESS_INPUT ? "Waiting" : theme_state_word(s);
    if (sl->n_merged) {
        snprintf(buf, sizeof buf, "%d sessions", sl->n_merged);
    } else {
        char host[24];
        theme_host_prefix(host, sizeof host, s); /* a remote session leads with its host */
        if (s->branch[0]) snprintf(buf, sizeof buf, "%s%s%s%s", host, theme_agent_name(s->agent), theme_sep(), s->branch);
        else snprintf(buf, sizeof buf, "%s%s", host, theme_agent_name(s->agent));
    }
    const char *base = sl->n_merged ? "Done" : theme_basename(s->repo);
    if (!base[0]) base = "(no folder)";

    if (dark) { /* ambient: the clock and one caption, "webapp · Working" */
        set_looks(LOOK_AMBIENT);
        snprintf(buf, sizeof buf, "%s%s%s", base, theme_sep(), word);
        theme_set_text(s_sub, s_sub_txt, sizeof s_sub_txt, buf);
        theme_set_text_color(s_sub, sub_col);
        theme_set_text_color(s_clock, sub_col);
        update_slot(NULL, 0);
        for (int i = 0; i < 2; i++) show(s_badge[i], false);
        apply_dim();
        apply_shift();
        return;
    }
    set_looks(LOOK_CARD);
    theme_set_text(s_cap, s_cap_txt, sizeof s_cap_txt, buf);
    set_title(base);
    uint32_t rgb = req ? (req->kind == ATT_APPROVAL ? RGB_PERMISSION : RGB_ASK) : theme_session_rgb(s);
    lv_color_t sc = offline() ? COL_TEXT2 : lv_color_hex(rgb);
    theme_set_text(s_sicon, s_sicon_txt, sizeof s_sicon_txt, req ? (req->kind == ATT_APPROVAL ? ICON_PERMISSION : ICON_QUESTION) : theme_state_glyph(s));
    theme_set_text(s_sword, s_sword_txt, sizeof s_sword_txt, word);
    theme_set_text_color(s_sicon, sc);
    theme_set_text_color(s_sword, sc);
    update_time(s, sl->n_merged != 0);

    if (req) { /* what is being asked, in place of the summary; the tool name when the daemon sent no title */
        theme_set_text(s_req_title, s_req_title_txt, sizeof s_req_title_txt,
                req->title[0] ? req->title : s->tool[0] ? s->tool : req->kind == ATT_APPROVAL ? "Permission request" : "Question");
        theme_set_text(s_req_text, s_req_text_txt, sizeof s_req_text_txt, req->text);
        show(s_sum, false);
        show(s_req_title, true);
        show(s_req_text, req->text[0] != 0);
    } else {
        char sum[sizeof s->summary + 4];
        theme_display_text(sum, sizeof sum, sl->n_merged ? "" : s->summary);
        theme_set_text(s_sum, s_sum_txt, sizeof s_sum_txt, sum);
        show(s_sum, true);
        show(s_req_title, false);
        show(s_req_text, false);
    }
    update_slot(req ? (req->kind == ATT_APPROVAL ? "Review" : "Answer") : NULL, rgb);
    show(s_badge[0], true);
    show(s_badge[1], n > 1);
    apply_dim();
    apply_shift();
}

/* Every 500 ms: the time in state ticks, the offline age ticks, the clock face moves on at the
 * minute, and the quiet glyph goes when quiet runs out. */
void ui_home_card_tick(void)
{
    const session_t *s = ui_home_sel_session();
    const home_slot_t *sl = ui_home_sel_slot();
    unsigned h, m;
    uint32_t minute;
    bool set = local_clock(&h, &m, &minute);
    if (set != s_clock_set || (set && minute != s_minute && (s_look != LOOK_CARD || s_ambient))) {
        ui_home_card_update();
        return;
    }
    if (s && sl && s_look == LOOK_CARD && !sl->n_merged) update_time(s, false);
    uint32_t now_ms = (uint32_t)lv_tick_get();
    bool batt = (int32_t)(now_ms - s_batt_next) >= 0;
    if (batt) s_batt_next = now_ms + 10000;
    if (ui_link_down_ms() || batt || s_quiet_shown != model_quiet_active(theme_now_unix())) {
        if (s_look == LOOK_CARD) ui_home_card_update();
        else update_slot(NULL, 0);
    }
}

void ui_home_card_report(ui_report_t *r)
{
    const session_t *s = ui_home_sel_session();
    if (s_look == LOOK_CARD && s) {
        ui_report_add(r, "%s", s_cap_txt);
        ui_report_add(r, "%s", s_title_txt);
        ui_report_add(r, "%s", s_sword_txt);
        if (s_stime_txt[0]) ui_report_add(r, "%s", s_stime_txt);
        if (!lv_obj_has_flag(s_sum, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_sum_txt);
        if (!lv_obj_has_flag(s_req_title, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_req_title_txt);
        if (!lv_obj_has_flag(s_req_text, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_req_text_txt);
    } else {
        ui_report_add(r, "%s", s_sub_txt);
        if (s_clock_set) ui_report_add(r, "%s", s_clock_txt);
        if (!s) ui_report_add(r, "No sessions");
    }
    if (!lv_obj_has_flag(s_chip, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_chip_txt_txt);
    if (!lv_obj_has_flag(s_btn, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_btn_txt);
    if (s_quiet_shown) ui_report_add(r, "quiet");
}

static lv_obj_t *row(lv_obj_t *parent, int h, int gap)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, h);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, gap, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void content_label(lv_obj_t *l)
{
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(l, LV_SIZE_CONTENT);
}

void ui_home_card_create(lv_obj_t *centre)
{
    s_centre = centre;
    s_cap = mk(s_centre, FONT_CAPTION, COL_TEXT2);
    put_mid(s_cap, UI_Y_CAPTION, theme_chord(UI_Y_CAPTION, 8), 1);
    s_title = mk(s_centre, FONT_TITLE, COL_TEXT);
    put_mid(s_title, TITLE_Y, TITLE_W, 1);

    s_srow = row(s_centre, 34, 8);
    lv_obj_align(s_srow, LV_ALIGN_TOP_MID, 0, STATE_Y - 17);
    s_sicon = mk(s_srow, FONT_ICON_M, COL_TEXT2);
    s_sword = mk(s_srow, FONT_HEAD, COL_TEXT2);
    s_stime = mk(s_srow, FONT_BODY, COL_TEXT2);
    content_label(s_sicon);
    content_label(s_sword);
    content_label(s_stime);

    s_sum = mk(s_centre, FONT_BODY, COL_TEXT2);
    put_top(s_sum, SUMMARY_TOP, SUMMARY_W, 2);
    int lh_s = lv_font_get_line_height(FONT_BODY_STRONG);
    s_req_title = mk(s_centre, FONT_BODY_STRONG, COL_TEXT);
    put_top(s_req_title, REQ_TOP, SUMMARY_W, 1);
    s_req_text = mk(s_centre, FONT_BODY, COL_TEXT2);
    put_top(s_req_text, REQ_TOP + lh_s, SUMMARY_W, 2);

    s_chip = row(s_centre, 24, 6);
    lv_obj_align(s_chip, LV_ALIGN_TOP_MID, 0, SLOT_Y - 12);
    s_chip_icon = mk(s_chip, FONT_ICON_S, COL_TEXT2);
    s_chip_txt = mk(s_chip, FONT_CAPTION, COL_TEXT2);
    content_label(s_chip_icon);
    content_label(s_chip_txt);

    s_btn = lv_obj_create(s_centre);
    lv_obj_remove_style_all(s_btn);
    lv_obj_set_size(s_btn, BTN_W, BTN_H);
    lv_obj_set_pos(s_btn, UI_CX - BTN_W / 2, SLOT_Y - BTN_H / 2);
    lv_obj_set_style_bg_opa(s_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(s_btn, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_btn_lbl = mk(s_btn, FONT_HEAD, COL_ON_ACCENT);
    content_label(s_btn_lbl);
    lv_obj_center(s_btn_lbl);

    s_quiet = mk(s_centre, FONT_ICON_S, COL_TEXT3);
    content_label(s_quiet);
    lv_label_set_text(s_quiet, ICON_QUIET);
    lv_obj_align(s_quiet, LV_ALIGN_TOP_MID, 0, SLOT_Y - lv_font_get_line_height(FONT_ICON_S) / 2);

    s_clock = mk(s_centre, FONT_NUM, COL_TEXT);
    put_mid(s_clock, CLOCK_Y, 300, 1);
    s_ready = mk(s_centre, FONT_TITLE, COL_TEXT);
    put_mid(s_ready, CLOCK_Y, 300, 1);
    lv_label_set_text(s_ready, "Ready");
    s_sub = mk(s_centre, FONT_BODY, COL_TEXT2);
    put_mid(s_sub, SUB_Y, 320, 1);

    static const char *const words[2] = {"Open", "Next"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_centre, i == 0 ? "A" : "B", words[i], i == 0, 0, 0, &s_key[i], &s_word[i]);
    lv_obj_t *hide[] = {s_chip, s_btn, s_quiet, s_clock, s_ready, s_sub, s_req_title, s_req_text};
    for (size_t i = 0; i < sizeof hide / sizeof hide[0]; i++) lv_obj_add_flag(hide[i], LV_OBJ_FLAG_HIDDEN);
}
