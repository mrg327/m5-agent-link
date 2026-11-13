/* The STATUS screen, the control centre (UI-V3 §4.6): the clock, one chip row for link and battery
 * (status by exception: amber only when the link is down), and three round toggles: Quiet, Mute, About.
 * nav.c still owns the five v2 rows and the quiet cycle; the toggles are the first two rows plus the
 * Privacy row, which stands for About (it only informs). A tap on a disc selects and commits it, A
 * commits the selected one, scrub and B move the selection. The ring keeps showing the sessions. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "hal.h"
#include "nav.h"
#include "ui_gauges.h"
#include "ui_internal.h"
#include "ui_theme.h"

#define TIME_Y 116
#define CHIP_Y 168
/* With the usage gauges (GAUGES.md) the clock is the title size and higher, the chips follow it, and
 * the two figure rows sit at y 152 and 180 (ui_gauges.c). */
#define TIME_Y_G 84
#define CHIP_Y_G 120
#define TOGGLE_Y 246
#define TOGGLE_D 76
#define TOGGLE_HIT 46 /* tap radius: a little beyond the disc */
#define SEL_RING_D (TOGGLE_D + 2 * 7) /* 2 px ring, 5 px clear of the disc */
#define DESC_Y 316
#define RENDER_MS 1000 /* the clock, battery and quiet end are re-read this often while open */
#define N_TOGGLES 3
enum { T_QUIET, T_MUTE, T_ABOUT };

static const int16_t k_tx[N_TOGGLES] = {143, 233, 323};
static lv_obj_t *s_root, *s_time, *s_chips, *s_link, *s_batt, *s_ring, *s_disc[N_TOGGLES], *s_icon[N_TOGGLES], *s_desc;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static uint32_t s_now, s_last_input, s_last_render;
static status_item_t s_items[ST_COUNT];
static int s_n, s_sel;
static char s_time_txt[24], s_link_txt[64], s_batt_txt[24], s_desc_txt[96], s_word_txt[16];
static bool s_gauges; /* the usage gauges are shown: the top of the screen is laid out for them */

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Toggle i's v2 row (About stands for Privacy). */
static const status_item_t *item_of(int i)
{
    static const status_row_t want[N_TOGGLES] = {ST_QUIET, ST_SOUNDS, ST_PRIVACY};
    for (int k = 0; k < s_n; k++)
        if (s_items[k].id == want[i]) return &s_items[k];
    return NULL;
}

static bool is_on(int i, uint32_t now)
{
    if (i == T_QUIET) return model_quiet_active(now);
    if (i == T_MUTE) return model_settings()->alert != ALERT_FULL;
    return false;
}

/* The icon shows the current state, not the action: a bell while alerts are on, a crossed-out bell while
 * quiet runs; a speaker while sounds are on, a crossed-out speaker while they are off. */
static const char *icon_of(int i, bool on)
{
    if (i == T_QUIET) return on ? ICON_QUIET : ICON_BELL;
    if (i == T_MUTE) return on ? ICON_MUTE : ICON_SOUND;
    return ICON_INFO;
}

static void quiet_line(char *dst, size_t cap, uint32_t now)
{
    const settings_t *st = model_settings();
    if (!model_quiet_active(now)) snprintf(dst, cap, "Quiet: off");
    else if (now > 100000) { /* the wall clock is set: say when it ends, in local time */
        uint32_t local = st->quiet_until + (uint32_t)((int32_t)st->tz_off_min * 60);
        snprintf(dst, cap, "Quiet until %02u:%02u", (unsigned)(local / 3600 % 24), (unsigned)(local / 60 % 60));
    } else {
        snprintf(dst, cap, "Quiet: on");
    }
}

/* What the selected toggle is, in words (state never rests on colour alone). */
static void describe(char *dst, size_t cap, uint32_t now)
{
    const settings_t *st = model_settings();
    if (s_sel == T_QUIET) {
        quiet_line(dst, cap, now);
    } else if (s_sel == T_MUTE) {
        snprintf(dst, cap, "%s", st->alert == ALERT_FULL ? "Sounds: on" : st->alert == ALERT_QUIET ? "Sounds: off" : "Silent");
    } else {
        model_counts_t c = model_counts();
        /* Bounded by MODEL_MAX_SESSIONS; the clamp tells -O2's format check the line fits. */
        unsigned n = (unsigned)model_count() % 100u, waiting = (unsigned)c.waiting % 100u, error = (unsigned)c.error % 100u;
        char a[40];
        if (n == 0) snprintf(a, sizeof a, "No sessions");
        else if (waiting) snprintf(a, sizeof a, "%u session%s%s%u waiting", n, n == 1 ? "" : "s", theme_sep(), waiting);
        else if (error) snprintf(a, sizeof a, "%u session%s%s%u error%s", n, n == 1 ? "" : "s", theme_sep(), error, error == 1 ? "" : "s");
        else snprintf(a, sizeof a, "%u session%s", n, n == 1 ? "" : "s");
        snprintf(dst, cap, "%s\nPrivacy: %s%s%.14s", a, st->privacy == PRIVACY_META ? "meta" : st->privacy == PRIVACY_FULL ? "full" : "preview",
                 theme_sep(), hal_board_info()->fw_version);
    }
}

/* A names what a commit on the selected toggle does; About has no commit, so no legend. */
static void update_badge(void)
{
    const settings_t *st = model_settings();
    const char *word = NULL;
    if (s_sel == T_QUIET) {
        int step = nav_quiet_step(st->quiet_until, theme_now_unix());
        word = step == 1 ? "Quiet 30 min" : step == 2 ? "Quiet 1 h" : "Quiet off";
    } else if (s_sel == T_MUTE) {
        word = st->alert == ALERT_FULL ? "Mute" : "Unmute";
    }
    show(s_badge[0], word != NULL);
    if (word && strcmp(word, s_word_txt)) {
        snprintf(s_word_txt, sizeof s_word_txt, "%s", word);
        theme_key_badge_set_word(s_badge[0], s_word[0], word, UI_BADGE_A_DEG, UI_BADGE_R);
    }
}

static const char *battery_icon(const hal_battery_t *b)
{
    if (b->charging) return ICON_BATTERY_CHARGING;
    return b->percent >= 90 ? ICON_BATTERY_FULL : b->percent >= 60 ? ICON_BATTERY_HIGH : b->percent >= 35 ? ICON_BATTERY_MED
           : b->percent >= 15 ? ICON_BATTERY_LOW : ICON_BATTERY_WARN;
}

/* Link chip: icon + host; amber `Offline · 4m` only while down (a healthy link is not shouted). */
static void render_chips(void)
{
    char buf[64];
    int k = ui_link_transport();
    const char *sym = k == HAL_LINK_USB ? ICON_USB : k == HAL_LINK_BLE ? ICON_BLUETOOTH : ICON_COMPUTER;
    uint32_t down = ui_link_down_ms(), rgb = 0;
    if (!ui_link_connected() && down && model_count() > 0) {
        char age[16];
        theme_elapsed(age, sizeof age, down / 1000);
        snprintf(buf, sizeof buf, "%s %s%s%s", ICON_OFFLINE, "Offline", theme_sep(), age);
        rgb = RGB_AMBER;
    } else if (k < 0) {
        snprintf(buf, sizeof buf, "%s Offline", ICON_OFFLINE);
        rgb = RGB_AMBER;
    } else if (!ui_link_connected()) {
        snprintf(buf, sizeof buf, "%s Connecting\xe2\x80\xa6", sym);
        rgb = RGB_AMBER;
    } else {
        const char *host = model_host();
        if (theme_now_unix() > 100000) /* with the clock unset the title already is the host name: icon only */
            snprintf(buf, sizeof buf, "%s %.16s", sym, host[0] ? host : k == HAL_LINK_USB ? "USB" : k == HAL_LINK_BLE ? "BLE" : "PC");
        else
            snprintf(buf, sizeof buf, "%s", sym);
    }
    theme_set_text(s_link, s_link_txt, sizeof s_link_txt, buf);
    theme_set_text_color(s_link, rgb ? lv_color_hex(rgb) : COL_TEXT2);

    hal_battery_t b = hal_battery_read();
    if (b.percent <= 100) snprintf(buf, sizeof buf, "%s %u%%", battery_icon(&b), b.percent);
    else buf[0] = 0;
    theme_set_text(s_batt, s_batt_txt, sizeof s_batt_txt, buf);
    show(s_batt, buf[0] != 0);
    lv_obj_update_layout(s_chips);
    theme_align(s_chips, LV_ALIGN_TOP_MID, 0, (s_gauges ? CHIP_Y_G : CHIP_Y) - lv_obj_get_height(s_chips) / 2);
}

/* LVGL invalidates on every style write, so write the font only when it changes. The label keeps its centre
 * row (y TIME_Y, or TIME_Y_G under the gauges) whatever the font. */
static void set_time_font(const lv_font_t *f)
{
    if (s_gauges && f == FONT_NUM) f = FONT_TITLE;
    int lh = lv_font_get_line_height(f), y = (s_gauges ? TIME_Y_G : TIME_Y) - lh / 2;
    if (lv_obj_get_style_text_font(s_time, 0) != f) lv_obj_set_style_text_font(s_time, f, 0);
    if (lv_obj_get_height(s_time) != lh) lv_obj_set_height(s_time, lh);
    if (lv_obj_get_y(s_time) != y) lv_obj_set_y(s_time, y);
}

static void render(void)
{
    s_last_render = s_now;
    uint32_t now = theme_now_unix();
    char buf[96];
    s_gauges = ui_gauges_status_render(now);
    if (now > 100000) { /* the clock is set: local time; otherwise the host name stands in */
        uint32_t local = now + (uint32_t)((int32_t)model_settings()->tz_off_min * 60);
        snprintf(buf, sizeof buf, "%u:%02u", (unsigned)(local / 3600 % 24), (unsigned)(local / 60 % 60));
        set_time_font(FONT_NUM);
    } else {
        const char *host = model_host();
        snprintf(buf, sizeof buf, "%.20s", host[0] ? host : "Watch");
        set_time_font(FONT_TITLE);
    }
    theme_set_text(s_time, s_time_txt, sizeof s_time_txt, buf);
    render_chips();

    for (int i = 0; i < N_TOGGLES; i++) {
        bool on = is_on(i, now);
        theme_set_bg_color(s_disc[i], on ? lv_color_hex(RGB_WORKING) : COL_FILL1);
        theme_set_text_color(s_icon[i], on ? COL_ON_ACCENT : COL_TEXT);
        const char *icon = icon_of(i, on);
        if (strcmp(lv_label_get_text(s_icon[i]), icon)) lv_label_set_text(s_icon[i], icon); /* only on a change */
    }
    int rx = k_tx[s_sel] - SEL_RING_D / 2;
    if (lv_obj_get_x(s_ring) != rx) lv_obj_set_x(s_ring, rx);

    describe(buf, sizeof buf, now);
    theme_set_text(s_desc, s_desc_txt, sizeof s_desc_txt, buf);
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
    s_sel = T_QUIET;
    render();
    ui_home_rim_mode(true); /* a rim drag dials through the toggles */
    show(s_root, true);
    dbg_event("ui", "status", "\"rows\":%d,\"quiet\":%s", N_TOGGLES, model_quiet_active(theme_now_unix()) ? "true" : "false");
}

void ui_status_close(void) { show(s_root, false); }

static int s_scrub_last;

static void select_item(int i, const char *via, void (*haptic)(void))
{
    i = ((i % N_TOGGLES) + N_TOGGLES) % N_TOGGLES;
    if (i == s_sel) return;
    s_sel = i;
    render();
    const status_item_t *it = item_of(i);
    dbg_event("ui", "item", "\"idx\":%d,\"id\":%d,\"via\":\"%s\"", i, it ? (int)it->id : -1, via);
    if (haptic) haptic();
}

/* Runs toggle `sel`. The screen stays open afterwards: the new state and the haptic are the
 * confirmation, no toast. */
static void commit(int sel)
{
    settings_t *st = model_settings();
    uint32_t now = theme_now_unix();
    if (sel == T_QUIET) {
        if (!now) { /* the RTC is unset: a timed quiet would never end */
            ui_toast("Set the clock first", RGB_AMBER);
            dbg_event("ui", "quiet_set", "\"step\":0,\"note\":\"no clock\"");
            return;
        }
        int step = nav_quiet_step(st->quiet_until, now);
        st->quiet_until = nav_quiet_next(st->quiet_until, now);
        dbg_event("ui", "quiet_set", "\"step\":%d,\"until\":%u", step, (unsigned)st->quiet_until);
        model_settings_changed();
        g_ui_actions->quiet_toggled();
    } else if (sel == T_MUTE) {
        st->alert = st->alert == ALERT_FULL ? ALERT_QUIET : ALERT_FULL;
        dbg_event("ui", "sounds_set", "\"alert\":%d", (int)st->alert);
        model_settings_changed();
        g_ui_actions->tick();
    } else {
        return; /* About only informs */
    }
    render();
}

/* The toggle under a point, or -1. */
static int toggle_at(int x, int y)
{
    for (int i = 0; i < N_TOGGLES; i++) {
        int dx = x - k_tx[i], dy = y - TOGGLE_Y;
        if (dx * dx + dy * dy <= TOGGLE_HIT * TOGGLE_HIT) return i;
    }
    return -1;
}

bool ui_status_act(nav_action_t a, const gesture_evt_t *g)
{
    if (g->kind != G_LIFT && g->kind != G_SHAKE) s_last_input = s_now;
    switch (a) {
    case NAV_COMMIT: {
        int hit = g->kind == G_TAP ? toggle_at(g->x, g->y) : -1;
        if (hit >= 0) select_item(hit, "tap", NULL);
        commit(hit >= 0 ? hit : s_sel);
        return true;
    }
    case NAV_HOLD_LONG:
        if (toggle_at(g->x, g->y) < 0) ui_go_back();
        return true;
    case NAV_SCRUB: {
        int d = nav_list_scrub(&s_scrub_last, g->kind, g->x);
        if (d) select_item(s_sel + d, "scrub", g_ui_actions->detent);
        return true;
    }
    case NAV_NEXT: select_item(s_sel + 1, "btn", g_ui_actions->tick); return true;
    case NAV_PREV: select_item(s_sel - 1, "btn", g_ui_actions->tick); return true;
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
    if (s_time_txt[0]) ui_report_add(r, "%s", s_time_txt);
    if (s_link_txt[0]) ui_report_add(r, "%s", s_link_txt);
    if (s_batt_txt[0]) ui_report_add(r, "%s", s_batt_txt);
    ui_gauges_status_report(r);
    /* the description's lines; the first carries the selection marker */
    char tmp[sizeof s_desc_txt];
    snprintf(tmp, sizeof tmp, "%s", s_desc_txt);
    char *nl = strchr(tmp, '\n');
    if (nl) *nl = 0;
    ui_report_add(r, "> %s", tmp);
    if (nl) ui_report_add(r, "%s", nl + 1);
    /* the toggles' icons by their Phosphor names, so a script can check that each shows its state;
       last, as the report drops lines once it is full */
    const char *names[N_TOGGLES];
    for (int i = 0; i < N_TOGGLES; i++) {
        const char *ic = lv_label_get_text(s_icon[i]);
        names[i] = !strcmp(ic, ICON_BELL) ? "bell" : !strcmp(ic, ICON_QUIET) ? "bell-slash" : !strcmp(ic, ICON_SOUND) ? "speaker"
                   : !strcmp(ic, ICON_MUTE) ? "speaker-slash" : "info";
    }
    ui_report_add(r, "icons %s %s %s", names[0], names[1], names[2]);
}

static lv_obj_t *centred(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, int cy, int w, int lines)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    int h = lines * lv_font_get_line_height(font);
    lv_obj_set_size(l, w, h);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, UI_CX - w / 2, cy - lv_font_get_line_height(font) / 2);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *circle(lv_obj_t *parent, int d)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *ui_status_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_time = centred(s_root, FONT_NUM, COL_TEXT, TIME_Y, theme_chord(TIME_Y, 30), 1);

    s_chips = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_chips);
    lv_obj_set_size(s_chips, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_chips, 22, 0);
    lv_obj_remove_flag(s_chips, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_link = lv_label_create(s_chips);
    s_batt = lv_label_create(s_chips);
    lv_obj_t *chip[2] = {s_link, s_batt};
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_text_font(chip[i], FONT_CAPTION, 0);
        lv_obj_set_style_text_color(chip[i], COL_TEXT2, 0);
        lv_obj_set_width(chip[i], LV_SIZE_CONTENT);
        lv_label_set_text(chip[i], "");
    }

    for (int i = 0; i < N_TOGGLES; i++) {
        s_disc[i] = circle(s_root, TOGGLE_D);
        lv_obj_set_pos(s_disc[i], k_tx[i] - TOGGLE_D / 2, TOGGLE_Y - TOGGLE_D / 2);
        lv_obj_set_style_bg_opa(s_disc[i], LV_OPA_COVER, 0);
        theme_set_bg_color(s_disc[i], COL_FILL1);
        s_icon[i] = lv_label_create(s_disc[i]);
        lv_obj_set_style_text_font(s_icon[i], FONT_ICON_L, 0);
        lv_obj_set_style_text_color(s_icon[i], COL_TEXT, 0);
        lv_label_set_text(s_icon[i], icon_of(i, false));
        lv_obj_center(s_icon[i]);
    }
    s_ring = circle(s_root, SEL_RING_D);
    lv_obj_set_style_border_width(s_ring, 2, 0);
    lv_obj_set_style_border_color(s_ring, COL_TEXT, 0);
    lv_obj_set_pos(s_ring, k_tx[0] - SEL_RING_D / 2, TOGGLE_Y - SEL_RING_D / 2);

    s_desc = centred(s_root, FONT_BODY, COL_TEXT2, DESC_Y, theme_chord(DESC_Y + 24, 20), 2);
    ui_gauges_status_create(s_root);

    static const char *const words[2] = {"Quiet 30 min", "Next"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    snprintf(s_word_txt, sizeof s_word_txt, "%s", words[0]);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
