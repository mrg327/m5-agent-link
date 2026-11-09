/* The deck's centre card (UI-V2 §4.1): agent · branch, the folder name, the state word with its glyph,
 * the summary or the request text, the time in state, the action pill, the key badges and the link
 * row. Pure presentation: it reads the model and the deck's selection (ui_home_internal.h) and draws;
 * the slots, the selection and the actions live in ui_home.c. */
#include <stdio.h>
#include <string.h>

#include "hal.h"
#include "ui_home_internal.h"
#include "ui_theme.h"

/* Card rows, top to bottom. Glanceable first: the state word is the largest coloured element and the
 * time in state is demoted below the summary. */
#define AGENT_Y 80    /* 16 px muted: agent · branch */
#define REPO_Y 102    /* 32 px white on one line, or 24 px on two; centred in a row REPO_H tall */
#define REPO_H 64
#define REPO_W 280
#define REPO_PX 32
#define REPO_SMALL_PX 24
#define STATE_Y 168   /* 32 px state colour: glyph + word */
#define SUMMARY_Y 210 /* 20 px muted, two lines: the agent's last summary */
#define SUMMARY_W 300
#define REQ_TITLE_Y 208 /* approval: 20 px title, then 16 px text on two lines, in place of summary + time */
#define REQ_TEXT_Y 234
#define ELAPSED_Y 262 /* 20 px muted: time in this state */
#define HINT_Y 364    /* 16 px: a one-line hint on the empty deck, between the badges and the link row */
#define LINK_Y 392    /* 16 px: link · host · battery, or Offline · stale */

static lv_obj_t *s_agent, *s_repo, *s_state, *s_summary, *s_elapsed, *s_pill, *s_pill_lbl, *s_hint, *s_link;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2]; /* [0] = A, [1] = B */
static lv_obj_t *s_req_title, *s_req_text; /* approval card: what is being approved */
static lv_obj_t *s_centre;

static bool s_ambient, s_offline_dim, s_quiet_shown;

static char s_agent_txt[64], s_repo_txt[40], s_state_txt[40], s_summary_txt[88], s_elapsed_txt[16], s_pill_txt[32],
    s_link_txt[64];
static char s_req_title_txt[48], s_req_text_txt[168];

static void set_txt(lv_obj_t *l, char *cache, size_t cap, const char *t)
{
    if (strcmp(cache, t) == 0) return;
    snprintf(cache, cap, "%s", t);
    lv_label_set_text(l, t);
}

/* Fixed height of `lines` rows: LONG_MODE_DOTS only truncates when the height is fixed, otherwise a
 * long text wraps onto the next row and collides with it. */
static void place_lines(lv_obj_t *l, int y, int w, int lines)
{
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, UI_CX - w / 2, y);
}

static void place(lv_obj_t *l, int y, int w) { place_lines(l, y, w, 1); }

/* The folder name is one 32 px line when it fits, otherwise two 24 px lines, centred in the same row. */
static void set_repo(const char *t)
{
    if (strcmp(s_repo_txt, t) == 0) return;
    const lv_font_t *big = theme_font(REPO_PX), *f = big;
    int lines = 1;
    lv_point_t sz;
    lv_text_get_size(&sz, t, big, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x > REPO_W) {
        f = theme_font(REPO_SMALL_PX);
        lines = 2;
    }
    lv_obj_set_style_text_font(s_repo, f, 0);
    int h = lines * lv_font_get_line_height(f);
    place_lines(s_repo, REPO_Y + (REPO_H - h) / 2, REPO_W, lines);
    set_txt(s_repo, s_repo_txt, sizeof s_repo_txt, t);
}

static bool offline(void) { return !ui_link_connected(); }

/* Link row: transport · host · battery while connected; otherwise how stale the snapshot is. A
 * leading mute glyph says quiet mode is on (STATUS has the details). */
void ui_home_link_text(char *dst, size_t cap, bool with_battery, uint32_t *rgb)
{
    bool quiet = model_quiet_active(theme_now_unix());
    const char *q = quiet ? LV_SYMBOL_MUTE "  " : "";
    s_quiet_shown = quiet;
    int k = ui_link_transport();
    const char *sym = k == HAL_LINK_USB ? LV_SYMBOL_USB : k == HAL_LINK_BLE ? LV_SYMBOL_BLUETOOTH : LV_SYMBOL_WIFI;
    const char *name = k == HAL_LINK_USB ? "USB" : k == HAL_LINK_BLE ? "BLE" : "PC";
    uint32_t down = ui_link_down_ms();
    if (!ui_link_connected() && down && model_count() > 0) {
        char age[16];
        theme_elapsed(age, sizeof age, down / 1000);
        snprintf(dst, cap, "%s%s Offline %s stale %s", q, LV_SYMBOL_WARNING, theme_sep(), age);
        *rgb = RGB_AMBER;
    } else if (k < 0) {
        snprintf(dst, cap, "%s%s Offline", q, LV_SYMBOL_WARNING);
        *rgb = RGB_AMBER;
    } else if (!ui_link_connected()) {
        snprintf(dst, cap, "%s%s %s...", q, sym, name);
        *rgb = RGB_AMBER;
    } else {
        const char *host = model_host();
        hal_battery_t b = hal_battery_read();
        char batt[16] = "";
        if (with_battery && b.percent <= 100)
            snprintf(batt, sizeof batt, "%s%s%u%%", theme_sep(), b.charging ? LV_SYMBOL_CHARGE " " : "", b.percent);
        if (host[0]) snprintf(dst, cap, "%s%s %s%s%.12s%s", q, sym, name, theme_sep(), host, batt);
        else snprintf(dst, cap, "%s%s %s%s", q, sym, name, batt);
        *rgb = RGB_SUCCESS;
    }
}

static void update_link(void)
{
    char buf[64];
    uint32_t link_rgb;
    ui_home_link_text(buf, sizeof buf, true, &link_rgb);
    set_txt(s_link, s_link_txt, sizeof s_link_txt, buf);
    lv_obj_set_style_text_color(s_link, lv_color_hex(link_rgb), 0);
}

/* Time in the current state (UI-V2 §10). While offline the clock is frozen at the moment the link
 * dropped, so a stale card does not keep counting as if it were live. */
static void update_elapsed(const session_t *s)
{
    uint32_t now = theme_now_unix() - ui_link_down_ms() / 1000, from = s->tst ? s->tst : s->t0;
    char buf[16] = "";
    if (from && now >= from) theme_elapsed_long(buf, sizeof buf, now - from);
    set_txt(s_elapsed, s_elapsed_txt, sizeof s_elapsed_txt, buf);
}

/* One opacity for the card: ambient wins, then offline, else full. */
static void apply_dim(void)
{
    lv_obj_set_style_opa(s_centre, s_ambient ? LV_OPA_30 : s_offline_dim ? LV_OPA_50 : LV_OPA_COVER, 0);
}

void ui_home_card_set_ambient(bool ambient)
{
    s_ambient = ambient;
    apply_dim();
}

static void set_badges(bool show_a, bool show_b, uint32_t rgb_a)
{
    bool off = offline();
    lv_obj_set_style_text_color(s_word[0], off ? COL_MUTED : lv_color_hex(rgb_a), 0);
    lv_obj_set_style_text_color(s_word[1], COL_MUTED, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_text_color(s_key[i], off ? COL_MUTED : COL_TEXT, 0);
        if (i == 0 ? show_a : show_b) lv_obj_remove_flag(s_badge[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_badge[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_home_card_update(void)
{
    char buf[64];
    update_link();

    s_offline_dim = offline() && model_count() > 0;
    apply_dim();

    const session_t *s = ui_home_sel_session();
    int n = ui_home_slot_count();
    if (!s || n == 0) {
        set_txt(s_agent, s_agent_txt, sizeof s_agent_txt, "");
        set_repo("No sessions");
        lv_obj_set_style_text_color(s_repo, COL_TEXT, 0);
        set_txt(s_state, s_state_txt, sizeof s_state_txt, "");
        set_txt(s_summary, s_summary_txt, sizeof s_summary_txt, "Waiting for an agent");
        set_txt(s_elapsed, s_elapsed_txt, sizeof s_elapsed_txt, "");
        lv_obj_add_flag(s_req_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_req_text, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_hint, "swipe down for status"); /* the empty deck's one way onward (UI-V2 §4.5) */
        set_badges(false, false, 0);
        return;
    }
    uint32_t rgb = theme_session_rgb(s);
    const home_slot_t *sl = ui_home_sel_slot();
    if (sl->n_merged) {
        snprintf(buf, sizeof buf, "%d sessions", sl->n_merged);
        set_txt(s_agent, s_agent_txt, sizeof s_agent_txt, buf);
        set_repo("Done");
    } else {
        /* agent · branch: two sessions on the same folder stay distinguishable */
        char host[24];
        theme_host_prefix(host, sizeof host, s);
        if (s->branch[0]) snprintf(buf, sizeof buf, "%s%s%s%s", host, theme_agent_name(s->agent), theme_sep(), s->branch);
        else snprintf(buf, sizeof buf, "%s%s", host, theme_agent_name(s->agent));
        set_txt(s_agent, s_agent_txt, sizeof s_agent_txt, buf);
        const char *base = theme_basename(s->repo);
        set_repo(base[0] ? base : "(no folder)");
    }
    lv_obj_set_style_text_color(s_repo, COL_TEXT, 0);
    snprintf(buf, sizeof buf, "%s  %s", theme_state_glyph(s), theme_state_word(s));
    set_txt(s_state, s_state_txt, sizeof s_state_txt, buf);
    lv_obj_set_style_text_color(s_state, lv_color_hex(rgb), 0);
    update_elapsed(s);

    const attention_t *req = ui_home_askable_of(s);
    bool ask = req != NULL;
    if (ask) {
        /* Show what is being asked in place of the summary and the time. Fall back to the tool
         * name when the daemon sent no title (privacy: meta). */
        set_txt(s_req_title, s_req_title_txt, sizeof s_req_title_txt,
                req->title[0] ? req->title : s->tool[0] ? s->tool : req->kind == ATT_APPROVAL ? "Permission request" : "Question");
        set_txt(s_req_text, s_req_text_txt, sizeof s_req_text_txt, req->text);
        set_txt(s_summary, s_summary_txt, sizeof s_summary_txt, "");
        lv_obj_add_flag(s_elapsed, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_req_title, LV_OBJ_FLAG_HIDDEN);
        if (req->text[0]) lv_obj_remove_flag(s_req_text, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_req_text, LV_OBJ_FLAG_HIDDEN);
    } else {
        set_txt(s_summary, s_summary_txt, sizeof s_summary_txt, sl->n_merged ? "" : s->summary);
        lv_obj_remove_flag(s_elapsed, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_req_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_req_text, LV_OBJ_FLAG_HIDDEN);
    }
    /* The pill names the centre tap: on a request it opens the options (answering needs a hold there). */
    const char *pill = ask ? "TAP " LV_SYMBOL_RIGHT " ANSWER" : s->state == SESS_DONE ? "FOCUS + ACK" : "FOCUS";
    set_txt(s_pill_lbl, s_pill_txt, sizeof s_pill_txt, pill);
    lv_color_t pill_col = offline() ? COL_MUTED : lv_color_hex(rgb);
    lv_obj_set_style_text_color(s_pill_lbl, pill_col, 0);
    lv_obj_set_style_bg_color(s_pill, pill_col, 0);
    lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_pill, LV_ALIGN_TOP_MID, 0, UI_ACTION_Y);
    lv_label_set_text(s_hint, ""); /* deny lives only in ASK (UI-V2 §3); the deck answers nothing */
    set_badges(true, n > 1, rgb);
}

/* Every 500 ms: the time in state ticks, the stale age ticks while offline, and the mute glyph goes
 * when quiet runs out. */
void ui_home_card_tick(void)
{
    const session_t *s = ui_home_sel_session();
    const home_slot_t *sl = ui_home_sel_slot();
    if (s && sl && !sl->n_merged) update_elapsed(s);
    if (ui_link_down_ms() || s_quiet_shown != model_quiet_active(theme_now_unix())) update_link();
}

void ui_home_card_report(ui_report_t *r)
{
    const session_t *s = ui_home_sel_session();
    if (s_agent_txt[0]) ui_report_add(r, "%s", s_agent_txt);
    if (s_repo_txt[0]) ui_report_add(r, "%s", s_repo_txt);
    if (s_state_txt[0] && s) ui_report_add(r, "%s", theme_state_word(s));
    if (s_summary_txt[0]) ui_report_add(r, "%s", s_summary_txt);
    if (!lv_obj_has_flag(s_req_title, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_req_title_txt);
    if (!lv_obj_has_flag(s_req_text, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_req_text_txt);
    if (s && !lv_obj_has_flag(s_pill, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_pill_txt);
    if (s_link_txt[0]) ui_report_add(r, "%s", s_link_txt);
    if (s_quiet_shown) ui_report_add(r, "quiet");
}

void ui_home_card_create(lv_obj_t *centre)
{
    s_centre = centre;
    s_agent = theme_label(s_centre, 16, COL_MUTED);
    place(s_agent, AGENT_Y, 280);
    s_repo = theme_label(s_centre, REPO_PX, COL_TEXT);
    place(s_repo, REPO_Y + (REPO_H - lv_font_get_line_height(theme_font(REPO_PX))) / 2, REPO_W);
    s_state = theme_label(s_centre, 32, COL_MUTED);
    place(s_state, STATE_Y, 300);
    s_summary = theme_label(s_centre, 20, COL_MUTED);
    place_lines(s_summary, SUMMARY_Y, SUMMARY_W, 2);
    s_elapsed = theme_label(s_centre, 20, COL_MUTED);
    place(s_elapsed, ELAPSED_Y, 250);
    s_req_title = theme_label(s_centre, 20, COL_TEXT);
    place(s_req_title, REQ_TITLE_Y, 300);
    s_req_text = theme_label(s_centre, 16, COL_MUTED);
    place_lines(s_req_text, REQ_TEXT_Y, 300, 2);
    lv_obj_add_flag(s_req_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_req_text, LV_OBJ_FLAG_HIDDEN);

    s_pill = lv_obj_create(s_centre);
    lv_obj_remove_style_all(s_pill);
    lv_obj_set_size(s_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_pill, UI_PILL_TINT_OPA, 0); /* a filled tint, no border */
    lv_obj_set_style_radius(s_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(s_pill, 18, 0);
    lv_obj_set_style_pad_ver(s_pill, 8, 0);
    lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_pill_lbl = theme_label(s_pill, 20, COL_TEXT);
    lv_label_set_long_mode(s_pill_lbl, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(s_pill_lbl, LV_SIZE_CONTENT);

    s_hint = theme_label(s_centre, 16, COL_MUTED);
    place(s_hint, HINT_Y, 300);
    s_link = theme_label(s_centre, 16, COL_MUTED);
    place(s_link, LINK_Y, 240);

    static const char *const words[2] = {"FOCUS", "NEXT"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_centre, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    lv_obj_t *all[] = {s_agent, s_repo, s_state, s_summary, s_elapsed, s_req_title, s_req_text, s_pill_lbl, s_hint, s_link};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) lv_label_set_text(all[i], "");
}
