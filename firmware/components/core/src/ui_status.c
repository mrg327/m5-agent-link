/* The STATUS screen (UI-V2 §4.5): the watch's own state. Link and battery read as two header lines,
 * then the rows on the wheel: Quiet (timed, device-side), Sounds (the alert mode), and three rows that
 * only inform (privacy, session counts, firmware). The row list and the quiet cycle come from nav.c;
 * this file names, colours and places the rows and runs the one the user commits. Nothing here is
 * destructive, so a tap commits like A does. The index ring keeps showing the sessions.
 * Capsules look: a round-capped battery gauge, circular key caps, a thin capsule divider. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "hal.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_theme.h"
#include "ui_wheel.h"

#define LINK_Y 78     /* 16 px: transport · host (amber while offline) */
#define BATT_Y 104    /* 24 px percent beside a capsule gauge, centred as one row */
#define GAUGE_W 64
#define GAUGE_H 10
#define DIVIDER_Y 152
#define HINT_Y 364
#define RENDER_MS 1000 /* the battery and the quiet clock are re-read this often while open */
#define STATUS_WHEEL ((wheel_layout_t){246, 34, 66}) /* five rows, clear of the divider and the badges */

static lv_obj_t *s_root, *s_link, *s_batt_row, *s_batt, *s_gauge, *s_gauge_fill, *s_divider, *s_wheel, *s_hint;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static uint32_t s_now, s_last_input, s_last_render;
static status_item_t s_items[ST_COUNT];
static int s_n;
static char s_link_txt[64], s_batt_txt[16], s_labels[ST_COUNT][28], s_word_txt[16];

static void set_txt(lv_obj_t *l, char *cache, size_t cap, const char *t)
{
    if (strcmp(cache, t) == 0) return;
    snprintf(cache, cap, "%s", t);
    lv_label_set_text(l, t);
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Words as well as glyphs: every row says its state in text. */
static void row_label(const status_item_t *it, char *dst, size_t cap)
{
    const settings_t *st = model_settings();
    uint32_t now = theme_now_unix();
    switch (it->id) {
    case ST_QUIET:
        if (!model_quiet_active(now)) snprintf(dst, cap, "Quiet: off");
        else if (now > 100000) { /* the wall clock is set: say when it ends, in local time */
            uint32_t local = st->quiet_until + (uint32_t)((int32_t)st->tz_off_min * 60);
            snprintf(dst, cap, "Quiet until %02u:%02u", (unsigned)(local / 3600 % 24), (unsigned)(local / 60 % 60));
        } else {
            snprintf(dst, cap, "Quiet: on");
        }
        break;
    case ST_SOUNDS:
        snprintf(dst, cap, "%s", st->alert == ALERT_FULL ? "Sounds: on" : st->alert == ALERT_QUIET ? "Sounds: off" : "Silent");
        break;
    case ST_PRIVACY:
        snprintf(dst, cap, "Privacy: %s", st->privacy == PRIVACY_META ? "meta" : st->privacy == PRIVACY_FULL ? "full" : "preview");
        break;
    case ST_SESSIONS: {
        unsigned n = (unsigned)model_count();
        model_counts_t c = model_counts();
        if (n == 0) snprintf(dst, cap, "No sessions");
        else if (c.waiting) snprintf(dst, cap, "%u session%s%s%u waiting", n, n == 1 ? "" : "s", theme_sep(), (unsigned)c.waiting);
        else if (c.error) snprintf(dst, cap, "%u session%s%s%u error%s", n, n == 1 ? "" : "s", theme_sep(), (unsigned)c.error, c.error == 1 ? "" : "s");
        else snprintf(dst, cap, "%u session%s", n, n == 1 ? "" : "s");
        break;
    }
    default: snprintf(dst, cap, "Firmware %s", hal_board_info()->fw_version); break;
    }
}

/* The A badge names what a commit on the selected row will do; information rows hide it. */
static void update_badge(void)
{
    int sel = wheel_sel(s_wheel);
    const status_item_t *it = sel < s_n ? &s_items[sel] : NULL;
    const settings_t *st = model_settings();
    const char *word = NULL;
    if (it && it->id == ST_QUIET) {
        int step = nav_quiet_step(st->quiet_until, theme_now_unix());
        word = step == 1 ? "QUIET 30 MIN" : step == 2 ? "QUIET 1 H" : "QUIET OFF";
    } else if (it && it->id == ST_SOUNDS) {
        word = st->alert == ALERT_FULL ? "SOUNDS OFF" : "SOUNDS ON";
    }
    show(s_badge[0], word != NULL);
    if (word && strcmp(word, s_word_txt)) {
        snprintf(s_word_txt, sizeof s_word_txt, "%s", word);
        theme_key_badge_set_word(s_badge[0], s_word[0], word, UI_BADGE_A_DEG, UI_BADGE_R);
    }
}

static void render(void)
{
    s_last_render = s_now;
    char buf[64];
    uint32_t rgb;
    ui_home_link_text(buf, sizeof buf, false, &rgb); /* the battery has its own row here */
    set_txt(s_link, s_link_txt, sizeof s_link_txt, buf);
    lv_obj_set_style_text_color(s_link, lv_color_hex(rgb), 0);

    hal_battery_t b = hal_battery_read();
    if (b.percent <= 100) {
        snprintf(buf, sizeof buf, "%u %%%s", b.percent, b.charging ? "  " LV_SYMBOL_CHARGE : "");
        uint32_t c = b.charging ? RGB_ACCENT : b.percent <= 15 ? RGB_ERROR : b.percent <= 30 ? RGB_AMBER : RGB_SUCCESS;
        int w = GAUGE_W * b.percent / 100;
        lv_obj_set_width(s_gauge_fill, w < GAUGE_H ? (b.percent ? GAUGE_H : 0) : w);
        lv_obj_set_style_bg_color(s_gauge_fill, lv_color_hex(c), 0);
        show(s_gauge, true);
    } else {
        snprintf(buf, sizeof buf, "Battery ?");
        show(s_gauge, false);
    }
    set_txt(s_batt, s_batt_txt, sizeof s_batt_txt, buf);
    lv_obj_update_layout(s_batt_row);
    lv_obj_align(s_batt_row, LV_ALIGN_TOP_MID, 0, BATT_Y);

    wheel_item_t w[ST_COUNT];
    for (int i = 0; i < s_n; i++) {
        row_label(&s_items[i], s_labels[i], sizeof s_labels[i]);
        w[i] = (wheel_item_t){s_labels[i], s_items[i].actionable ? RGB_ACCENT : RGB_IDLE, false, (int)s_items[i].id};
    }
    wheel_set(s_wheel, w, s_n, wheel_count(s_wheel) == s_n ? wheel_sel(s_wheel) : 0);
    update_badge();
}

void ui_status_refresh(uint32_t now)
{
    s_now = now;
    render();
}

void ui_status_open(uint32_t now)
{
    s_now = s_last_input = now;
    s_n = nav_status_items(s_items, ST_COUNT);
    wheel_set(s_wheel, NULL, 0, 0);
    render();
    if (g_ui_actions->sectors) g_ui_actions->sectors(s_n); /* a rim scrub steps through the rows */
    show(s_root, true);
    dbg_event("ui", "status", "\"rows\":%d,\"quiet\":%s", s_n, model_quiet_active(theme_now_unix()) ? "true" : "false");
}

void ui_status_close(void) { show(s_root, false); }

static void select_item(int i, const char *via, void (*haptic)(void))
{
    int n = wheel_count(s_wheel);
    if (n == 0) return;
    i = ((i % n) + n) % n;
    if (i == wheel_sel(s_wheel)) return;
    wheel_select(s_wheel, i);
    update_badge();
    dbg_event("ui", "item", "\"idx\":%d,\"id\":%d,\"via\":\"%s\"", i, wheel_item(s_wheel, i)->id, via);
    if (haptic) haptic();
}

/* Runs the selected row. The screen stays open afterwards: the row's new text is the confirmation,
 * with the haptic; no toast, so the rows stay readable. */
static void commit(const status_item_t *it)
{
    settings_t *st = model_settings();
    uint32_t now = theme_now_unix();
    switch (it->id) {
    case ST_QUIET: {
        int step = nav_quiet_step(st->quiet_until, now);
        st->quiet_until = nav_quiet_next(st->quiet_until, now);
        dbg_event("ui", "quiet_set", "\"step\":%d,\"until\":%u", step, (unsigned)st->quiet_until);
        model_settings_changed();
        g_ui_actions->quiet_toggled();
        break;
    }
    case ST_SOUNDS:
        st->alert = st->alert == ALERT_FULL ? ALERT_QUIET : ALERT_FULL;
        dbg_event("ui", "sounds_set", "\"alert\":%d", (int)st->alert);
        model_settings_changed();
        g_ui_actions->tick();
        break;
    default:
        g_ui_actions->reject(); /* an information row: nothing to do */
        return;
    }
    render();
}

bool ui_status_act(nav_action_t a, const gesture_evt_t *g)
{
    if (g->kind != G_LIFT && g->kind != G_SHAKE) s_last_input = s_now;
    int sel = wheel_sel(s_wheel);
    switch (a) {
    case NAV_COMMIT:
        if (sel < s_n) commit(&s_items[sel]);
        return true;
    case NAV_SCRUB:
        if (g->kind != G_SCRUB_END) select_item(g->x, "scrub", g_ui_actions->detent);
        return true;
    case NAV_NEXT: select_item(sel + 1, "btn", g_ui_actions->tick); return true;
    case NAV_PREV: select_item(sel - 1, "btn", g_ui_actions->tick); return true;
    default: return false;
    }
}

/* Returns true when the screen wants to close: STATUS has an idle timeout like AGENT. */
bool ui_status_tick(uint32_t now)
{
    s_now = now;
    if (now - s_last_render >= RENDER_MS) render();
    return now - s_last_input > NAV_STATUS_TIMEOUT_MS;
}

void ui_status_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "status");
    if (s_link_txt[0]) ui_report_add(r, "%s", s_link_txt);
    if (s_batt_txt[0]) ui_report_add(r, "%s", s_batt_txt);
    int sel = wheel_sel(s_wheel);
    for (int i = 0; i < s_n; i++) ui_report_add(r, "%s%s", i == sel ? "> " : "", s_labels[i]);
}

static lv_obj_t *line(lv_obj_t *parent, int px, lv_color_t col, int y, int w)
{
    lv_obj_t *l = theme_label(parent, px, col);
    lv_obj_set_size(l, w, lv_font_get_line_height(theme_font(px)));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, UI_CX - w / 2, y);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *capsule(lv_obj_t *parent, int w, int h, lv_color_t col)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, col, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *ui_status_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_link = line(s_root, 16, COL_MUTED, LINK_Y, 280);

    /* "82 %  [====  ]": the percent and a round-capped gauge, laid out as one centred row */
    s_batt_row = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_batt_row);
    lv_obj_set_size(s_batt_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_batt_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_batt_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_batt_row, 14, 0);
    lv_obj_remove_flag(s_batt_row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_batt = theme_label(s_batt_row, 24, COL_TEXT);
    lv_obj_set_width(s_batt, LV_SIZE_CONTENT);
    lv_label_set_text(s_batt, "");
    s_gauge = capsule(s_batt_row, GAUGE_W, GAUGE_H, COL_BORDER);
    s_gauge_fill = capsule(s_gauge, 0, GAUGE_H, COL_MUTED);
    lv_obj_set_pos(s_gauge_fill, 0, 0);

    s_divider = capsule(s_root, 180, 3, COL_BORDER);
    lv_obj_set_pos(s_divider, UI_CX - 90, DIVIDER_Y);
    s_wheel = wheel_create_at(s_root, STATUS_WHEEL);
    s_hint = line(s_root, 16, COL_MUTED, HINT_Y, 300);
    lv_label_set_text(s_hint, "swipe up to go back");
    static const char *const words[2] = {"QUIET 30 MIN", "NEXT"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    snprintf(s_word_txt, sizeof s_word_txt, "%s", words[0]);
    lv_obj_set_style_text_color(s_word[0], lv_color_hex(RGB_ACCENT), 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
