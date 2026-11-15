/* The STATUS screen in Sigil (docs/design/GLYPHS.md §5), the control centre: the clock in digits is the subject
 * and the four toggles orbit it, Quiet at 9 o'clock, Mute at 12, Volume at 3, About at 6. Nothing is spelled out:
 *
 *   - a toggle's glyph is its state (a bell while alerts sound, a crossed-out bell while quiet runs; a speaker,
 *     a crossed-out speaker);
 *   - what is left of the quiet time is an arc round the bell that drains, a whole turn for an hour;
 *   - the volume is five steps of an arc round its disc, grey while nothing would be heard;
 *   - the link is the transport's glyph and the computer's own sigil (rule 2: an identity is a mark), or the amber
 *     plug with how long it has been down as a tally; the battery is a ring round the battery glyph;
 *   - About turns the centre into its face: the sessions, those that wait for you and those that failed as
 *     glyphs with their counts, the privacy eye, and the firmware version, the one piece of text;
 *   - A's hint is what a press does to the selected toggle (the glyph it will become), B's is the caret.
 *
 * The usage gauges (ui_gauges.c) keep their two arcs inside the bezel and their two rows above the clock; the
 * link then moves down beside the battery. nav.c still owns the quiet cycle; Volume steps the stored speaker
 * level (prefs.h). A tap on a disc selects and commits it, a tap elsewhere and A commit the selected one, the
 * dial and B move the selection. What every glyph means is said in words in the report (rule 9), as before. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "hal.h"
#include "nav.h"
#include "prefs.h"
#include "ui_gauges.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_theme.h"

/* Geometry: centres. The orbit's radius puts Quiet and Volume straight above the two key hints and About on
 * their row. The gauges' arcs begin at r 180 (a selected disc ends at r 172); their rows are y 139..193 (the
 * disc at 12 o'clock ends at y 141). */
#define ORBIT_R 132
#define ORBIT_PHASE 270 /* the first toggle at 9 o'clock */
/* A value arc keeps one radius: round the small disc (r 30) while it rests, and inside the rim of the large one
 * (r 40) once its toggle is selected, where it is drawn in the dark ink of the disc's glyph. */
#define VAL_R 36
#define VAL_W 4
#define RGB_VAL_TRACK_SEL 0x3d7fa5 /* an unlit step on the selected (accent) disc */
#define RGB_VAL_OFF_SEL 0x2e607d   /* lit steps nothing is heard at, on the selected disc */
#define VOL_START 210   /* the volume dial: 300 degrees, the gap at 6 o'clock */
#define VOL_SPAN 300
#define CLOCK_Y 233
#define TOP_Y 172       /* the slot above the clock: the link, or the gauges' rows */
#define LOW_Y 292       /* the slot under it: the battery, and the link when the gauges have the top */
#define LINK_PX 28      /* the transport's glyph */
#define LINK_GAP 8      /* between it and the sigil or the tally */
#define HOST_R UI_SIGIL_R_MIN
#define BATT_R 21
#define BATT_W 4
#define BATT_D (2 * BATT_R + BATT_W) /* the ring, edge to edge */
#define ROW_GAP 18      /* link and battery side by side in the lower slot */
#define BATT_LOW 15
/* The About face. With the gauges' rows above it, it starts lower. */
#define ABOUT_Y 198     /* the three glyphs */
#define ABOUT_Y_G 222
#define ABOUT_PITCH 58
#define ABOUT_NUM_DY 36 /* their counts */
#define ABOUT_NUM_W 52
#define ABOUT_VER_DY 74 /* the privacy eye and the version */
#define EYE_PX 28
#define RENDER_MS 1000  /* the clock, battery and quiet end are re-read this often while open */
#define N_TOGGLES 4
enum { T_QUIET, T_MUTE, T_VOLUME, T_ABOUT };
enum { A_SESSIONS, A_WAITING, A_ERRORS, A_COUNT };

static lv_obj_t *s_root, *s_orbit, *s_quiet_arc, *s_vol_arc, *s_clock, *s_link, *s_host, *s_age, *s_batt_arc, *s_batt;
static lv_obj_t *s_about[A_COUNT], *s_about_n[A_COUNT], *s_eye, *s_ver, *s_hint[2];
static uint32_t s_now, s_last_input, s_last_render;
static status_item_t s_items[ST_COUNT];
static int s_n, s_ver_w;
static int32_t s_about_at[A_COUNT], s_ver_at;
static bool s_gauges; /* the usage gauges are shown: they have the slot above the clock */
/* On glass, with the labels' caches. */
static char s_clock_txt[8], s_about_txt[A_COUNT][4], s_ver_txt[16];
/* In words, for the report (rule 9): what the glyphs and arcs say. */
static char s_time_txt[24], s_link_txt[64], s_batt_txt[24], s_desc_txt[96];

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Moves a one-row label of `font` so its row is centred on (x, cy); `x` is its left edge. Only when `*at`,
 * where it was last put, differs: LVGL redraws on every position write. */
static void put(lv_obj_t *label, const lv_font_t *font, int x, int cy, int32_t *at)
{
    int32_t key = (int32_t)(x << 12 | cy);
    if (*at == key) return;
    *at = key;
    lv_obj_set_pos(label, x, cy - lv_font_get_line_height(font) / 2);
}

/* Toggle i's v2 row (About stands for Privacy; Volume has none: ST_COUNT matches no row). */
static const status_item_t *item_of(int i)
{
    static const status_row_t want[N_TOGGLES] = {ST_QUIET, ST_SOUNDS, ST_COUNT, ST_PRIVACY};
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

/* The glyph shows the current state, not the action: a bell while alerts are on, a crossed-out bell while
 * quiet runs; a speaker while sounds are on, a crossed-out speaker while they are off. Volume's level is the
 * arc round its disc. */
static const char *icon_of(int i, bool on)
{
    if (i == T_QUIET) return on ? ICON_QUIET : ICON_BELL;
    if (i == T_MUTE) return on ? ICON_MUTE : ICON_SOUND;
    if (i == T_VOLUME) return ICON_WAVE;
    return ICON_INFO;
}

/* The level is what the speaker plays at only while sounds are on; otherwise its arc goes grey. */
static bool volume_heard(void) { return model_settings()->alert == ALERT_FULL && prefs_volume() > 0; }

/* The next level a commit sets: up one step, from the top back to 1 (0 is Mute's job). */
static uint8_t volume_next(void) { return prefs_volume() >= PREFS_VOL_MAX ? 1 : (uint8_t)(prefs_volume() + 1); }

/* What is left of the quiet time, a whole turn for an hour (or for longer, or with the clock unset). */
static int quiet_permille(uint32_t now)
{
    uint32_t until = model_settings()->quiet_until;
    if (!model_quiet_active(now)) return 0;
    uint32_t min = (until - now + 59) / 60;
    return now <= 100000 || min >= 60 ? 1000 : (int)(min * 1000 / 60);
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

/* What the selected toggle is, in words: the report's legend for the glyphs and arcs. */
static void describe(char *dst, size_t cap, int sel, uint32_t now)
{
    const settings_t *st = model_settings();
    if (sel == T_QUIET) {
        quiet_line(dst, cap, now);
    } else if (sel == T_MUTE) {
        snprintf(dst, cap, "%s", st->alert == ALERT_FULL ? "Sounds: on" : st->alert == ALERT_QUIET ? "Sounds: off" : "Silent");
    } else if (sel == T_VOLUME) {
        snprintf(dst, cap, "Volume %u of %u%s", (unsigned)prefs_volume(), (unsigned)PREFS_VOL_MAX,
                 st->alert == ALERT_FULL ? "" : st->alert == ALERT_QUIET ? " (muted)" : " (silent)");
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

/* A's hint is what a commit does to the selected toggle: the glyph the toggle will show. About has no commit,
 * so no hint. */
static void update_hint(int sel)
{
    const settings_t *st = model_settings();
    const char *icon = NULL;
    if (sel == T_QUIET) icon = nav_quiet_step(st->quiet_until, theme_now_unix()) ? ICON_QUIET : ICON_BELL;
    else if (sel == T_MUTE) icon = st->alert == ALERT_FULL ? ICON_MUTE : ICON_SOUND;
    else if (sel == T_VOLUME) icon = ICON_WAVE;
    show(s_hint[0], icon != NULL);
    if (icon) ui_key_hint_set(s_hint[0], icon);
}

static const char *battery_icon(const hal_battery_t *b)
{
    if (b->charging) return ICON_BATTERY_CHARGING;
    return b->percent >= 90 ? ICON_BATTERY_FULL : b->percent >= 60 ? ICON_BATTERY_HIGH : b->percent >= 35 ? ICON_BATTERY_MED
           : b->percent >= 15 ? ICON_BATTERY_LOW : ICON_BATTERY_WARN;
}

/* How the link looks: the transport's glyph and the computer's sigil; amber and without the sigil while it is
 * down (a healthy link is not shouted): the plug and how long as a tally, or the transport's glyph while it
 * connects. */
typedef struct {
    const char *icon, *host;
    uint32_t rgb, age_min;
    bool sigil;
    int w; /* px the glyph and what follows it take */
} link_look_t;

/* Reads the link into `k`, and into s_link_txt in the old chip's words. */
static void link_look(link_look_t *k)
{
    char buf[64];
    int t = ui_link_transport();
    const char *sym = t == HAL_LINK_USB ? ICON_USB : t == HAL_LINK_BLE ? ICON_BLUETOOTH : ICON_COMPUTER;
    uint32_t down = ui_link_down_ms();
    *k = (link_look_t){.icon = sym, .host = model_host()};
    if (!ui_link_connected() && down && model_count() > 0) {
        char age[16];
        theme_elapsed(age, sizeof age, down / 1000);
        snprintf(buf, sizeof buf, "%s %s%s%s", ICON_OFFLINE, "Offline", theme_sep(), age);
        k->rgb = RGB_AMBER;
        k->icon = ICON_OFFLINE;
        k->age_min = down / 60000;
    } else if (t < 0) {
        snprintf(buf, sizeof buf, "%s Offline", ICON_OFFLINE);
        k->rgb = RGB_AMBER;
        k->icon = ICON_OFFLINE;
    } else if (!ui_link_connected()) {
        snprintf(buf, sizeof buf, "%s Connecting\xe2\x80\xa6", sym);
        k->rgb = RGB_AMBER;
    } else {
        k->sigil = k->host[0] != 0;
        if (theme_now_unix() > 100000) /* with the clock unset the first line already is the host name */
            snprintf(buf, sizeof buf, "%s %.16s", sym, k->host[0] ? k->host : t == HAL_LINK_USB ? "USB" : t == HAL_LINK_BLE ? "BLE" : "PC");
        else
            snprintf(buf, sizeof buf, "%s", sym);
    }
    snprintf(s_link_txt, sizeof s_link_txt, "%s", buf);
    int w2 = k->sigil ? 2 * HOST_R : k->age_min ? ui_tally_width(k->age_min) : 0;
    k->w = LINK_PX + (w2 ? LINK_GAP + w2 : 0);
}

/* The link, centred on (cx, cy). */
static void render_link(const link_look_t *k, int cx, int cy, bool on)
{
    int x0 = cx - k->w / 2, x2 = x0 + LINK_PX + LINK_GAP; /* where the sigil or the tally begins */
    show(s_link, on);
    show(s_host, on && k->sigil);
    show(s_age, on && k->age_min > 0);
    ui_glyph_set_pos(s_link, x0 + LINK_PX / 2, cy);
    ui_glyph_set(s_link, k->icon, k->rgb ? lv_color_hex(k->rgb) : COL_TEXT2);
    if (k->sigil) {
        ui_sigil_set_pos(s_host, x2 + HOST_R, cy);
        ui_sigil_set_name(s_host, k->host);
    }
    if (k->age_min) {
        ui_tally_set_pos(s_age, x2 + (k->w - LINK_PX - LINK_GAP) / 2, cy);
        ui_tally_set(s_age, k->age_min, lv_color_hex(k->rgb));
    }
}

/* The battery, centred on (cx, cy): a ring that is the level, round the battery glyph (which also says
 * charging). Vermillion when low, as on HOME. A board without a battery shows none. */
static void render_battery(const hal_battery_t *bp, int cx, int cy, bool on)
{
    hal_battery_t b = *bp;
    bool known = b.percent <= 100, low = known && b.percent <= BATT_LOW && !b.charging;
    if (known) snprintf(s_batt_txt, sizeof s_batt_txt, "%s %u%%", battery_icon(&b), b.percent);
    else s_batt_txt[0] = 0;
    show(s_batt_arc, on && known);
    show(s_batt, on && known);
    if (!known) return;
    ui_arc_set_geometry(s_batt_arc, cx, cy, BATT_R);
    ui_arc_set(s_batt_arc, b.percent * 10, low ? lv_color_hex(RGB_ERROR) : COL_TEXT, COL_FILL2);
    ui_glyph_set_pos(s_batt, cx, cy);
    ui_glyph_set(s_batt, battery_icon(&b), low ? lv_color_hex(RGB_ERROR) : COL_TEXT2);
}

/* The About face: sessions, waiting for you, failed, each a glyph with its count under it (none and grey at
 * zero); then the privacy eye and the version. */
static void render_about(bool on)
{
    model_counts_t c = model_counts();
    const unsigned n[A_COUNT] = {(unsigned)model_count() % 100u, (unsigned)c.waiting % 100u, (unsigned)c.error % 100u};
    static const char *const icon[A_COUNT] = {ICON_SESSIONS, ICON_PERMISSION, ICON_ERROR};
    static const uint32_t rgb[A_COUNT] = {0xffffff, RGB_PERMISSION, RGB_ERROR};
    int y = s_gauges ? ABOUT_Y_G : ABOUT_Y;
    for (int i = 0; i < A_COUNT; i++) {
        char buf[12];
        snprintf(buf, sizeof buf, "%u", n[i]);
        show(s_about[i], on);
        show(s_about_n[i], on && n[i] > 0);
        ui_glyph_set_pos(s_about[i], UI_CX + (i - 1) * ABOUT_PITCH, y);
        ui_glyph_set(s_about[i], icon[i], n[i] ? lv_color_hex(rgb[i]) : COL_TEXT3);
        theme_set_text(s_about_n[i], s_about_txt[i], sizeof s_about_txt[i], buf);
        theme_set_text_color(s_about_n[i], lv_color_hex(rgb[i]));
        put(s_about_n[i], FONT_HEAD, UI_CX + (i - 1) * ABOUT_PITCH - ABOUT_NUM_W / 2, y + ABOUT_NUM_DY, &s_about_at[i]);
    }
    if (!s_ver_txt[0]) { /* once: the version does not change while the watch runs */
        lv_point_t sz;
        snprintf(s_ver_txt, sizeof s_ver_txt, "%.14s", hal_board_info()->fw_version);
        lv_text_get_size(&sz, s_ver_txt, FONT_CAPTION, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        s_ver_w = sz.x;
        lv_obj_set_width(s_ver, s_ver_w + 2);
        lv_label_set_text(s_ver, s_ver_txt);
    }
    privacy_t p = model_settings()->privacy;
    int x0 = UI_CX - (EYE_PX + 8 + s_ver_w) / 2;
    show(s_eye, on);
    show(s_ver, on);
    ui_glyph_set_pos(s_eye, x0 + EYE_PX / 2, y + ABOUT_VER_DY);
    ui_glyph_set(s_eye, p == PRIVACY_META ? ICON_EYE_OFF : p == PRIVACY_FULL ? ICON_G_READ : ICON_EYE_HALF, COL_TEXT2);
    put(s_ver, FONT_CAPTION, x0 + EYE_PX + 8, y + ABOUT_VER_DY, &s_ver_at);
}

static void render(void)
{
    s_last_render = s_now;
    uint32_t now = theme_now_unix();
    int sel = ui_orbit_selected(s_orbit);
    bool about = sel == T_ABOUT;
    char buf[96];
    s_gauges = ui_gauges_status_render(now);

    /* the clock; until it is set its digits are dashes (the report names the computer instead, as before) */
    bool set = now > 100000;
    if (set) {
        uint32_t local = now + (uint32_t)((int32_t)model_settings()->tz_off_min * 60);
        snprintf(s_time_txt, sizeof s_time_txt, "%u:%02u", (unsigned)(local / 3600 % 24), (unsigned)(local / 60 % 60));
    } else {
        const char *host = model_host();
        snprintf(s_time_txt, sizeof s_time_txt, "%.20s", host[0] ? host : "Watch");
    }
    theme_set_text(s_clock, s_clock_txt, sizeof s_clock_txt, set ? s_time_txt : "--:--");
    theme_set_text_color(s_clock, set ? COL_TEXT : COL_TEXT3);
    show(s_clock, !about);
    /* the link above the clock and the battery under it; or, when the gauges have the slot above, both under
     * it as one centred row */
    link_look_t link;
    hal_battery_t batt = hal_battery_read();
    link_look(&link);
    int link_x = UI_CX, batt_x = UI_CX;
    if (s_gauges && batt.percent <= 100) {
        int x0 = UI_CX - (link.w + ROW_GAP + BATT_D) / 2;
        link_x = x0 + link.w / 2;
        batt_x = x0 + link.w + ROW_GAP + BATT_D / 2;
    }
    render_link(&link, link_x, s_gauges ? LOW_Y : TOP_Y, !about);
    render_battery(&batt, batt_x, LOW_Y, !about);
    render_about(about);

    /* the toggles: each glyph is its state */
    ui_orbit_item_t items[N_TOGGLES];
    for (int i = 0; i < N_TOGGLES; i++) items[i] = (ui_orbit_item_t){icon_of(i, is_on(i, now)), 0, false};
    ui_orbit_set_items(s_orbit, items, N_TOGGLES);
    /* the two quantities: white round a resting disc, dark inside the selected one */
    ui_arc_set(s_quiet_arc, quiet_permille(now), sel == T_QUIET ? COL_ON_ACCENT : COL_TEXT, lv_color_black());
    if (sel == T_VOLUME)
        ui_arc_set(s_vol_arc, prefs_volume() * 1000 / PREFS_VOL_MAX, volume_heard() ? COL_ON_ACCENT : lv_color_hex(RGB_VAL_OFF_SEL),
                   lv_color_hex(RGB_VAL_TRACK_SEL));
    else ui_arc_set(s_vol_arc, prefs_volume() * 1000 / PREFS_VOL_MAX, volume_heard() ? COL_TEXT : COL_TEXT3, COL_FILL2);

    describe(buf, sizeof buf, sel, now);
    snprintf(s_desc_txt, sizeof s_desc_txt, "%s", buf);
    update_hint(sel);
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
    ui_orbit_select(s_orbit, T_QUIET);
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
    if (i == ui_orbit_selected(s_orbit)) return;
    ui_orbit_select(s_orbit, i);
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
    } else if (sel == T_VOLUME) {
        /* Changing the level means the user wants to hear it: like a phone, it also unmutes. */
        prefs_set_volume(volume_next());
        bool unmuted = st->alert != ALERT_FULL;
        if (unmuted) {
            st->alert = ALERT_FULL;
            model_settings_changed();
        }
        dbg_event("ui", "volume_set", "\"step\":%u,\"unmuted\":%s", (unsigned)prefs_volume(), unmuted ? "true" : "false");
        g_ui_actions->volume_changed();
    } else {
        return; /* About only informs */
    }
    render();
}

bool ui_status_act(nav_action_t a, const gesture_evt_t *g)
{
    if (g->kind != G_LIFT && g->kind != G_SHAKE) s_last_input = s_now;
    switch (a) {
    case NAV_COMMIT: {
        int hit = g->kind == G_TAP ? ui_orbit_hit(s_orbit, g->x, g->y) : -1;
        if (hit >= 0) select_item(hit, "tap", NULL);
        commit(hit >= 0 ? hit : ui_orbit_selected(s_orbit));
        return true;
    }
    case NAV_HOLD_LONG:
        if (ui_orbit_hit(s_orbit, g->x, g->y) < 0) ui_go_back();
        return true;
    case NAV_SCRUB: {
        int d = nav_list_scrub(&s_scrub_last, g->kind, g->x);
        if (d) select_item(ui_orbit_selected(s_orbit) + d, "scrub", g_ui_actions->detent);
        return true;
    }
    case NAV_NEXT: select_item(ui_orbit_selected(s_orbit) + 1, "btn", g_ui_actions->tick); return true;
    case NAV_PREV: select_item(ui_orbit_selected(s_orbit) - 1, "btn", g_ui_actions->tick); return true;
    default: {
        /* The discs reach past the centre zone (nav.h: r 150), where a tap is no action of this screen: a tap
         * on a disc is still a tap on it. */
        int hit = g->kind == G_TAP ? ui_orbit_hit(s_orbit, g->x, g->y) : -1;
        if (hit < 0) return false;
        select_item(hit, "tap", NULL);
        commit(hit);
        return true;
    }
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
    /* the toggles' glyphs by their Phosphor names, so a script can check that each shows its state; last, as
       the report drops lines once it is full */
    uint32_t now = theme_now_unix();
    ui_report_add(r, "icons %s %s volume-%u info", is_on(T_QUIET, now) ? "bell-slash" : "bell", is_on(T_MUTE, now) ? "speaker-slash" : "speaker",
                  (unsigned)prefs_volume());
}

static lv_obj_t *centred(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, int cx, int cy, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    int h = lv_font_get_line_height(font);
    lv_obj_set_size(l, w, h);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_pos(l, cx - w / 2, cy - h / 2);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ui_status_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    ui_gauges_status_create(s_root); /* under everything: two arcs inside the bezel, two rows above the clock */

    /* the toggles, and the two that carry a quantity */
    s_orbit = ui_orbit_create(s_root);
    ui_orbit_set_geometry(s_orbit, ORBIT_R, ORBIT_PHASE);
    s_quiet_arc = ui_arc_create(s_root, UI_CX - ORBIT_R, UI_CX, VAL_R, VAL_W);
    s_vol_arc = ui_arc_create(s_root, UI_CX + ORBIT_R, UI_CX, VAL_R, VAL_W);
    ui_arc_set_span(s_vol_arc, VOL_START, VOL_SPAN);
    ui_arc_set_steps(s_vol_arc, PREFS_VOL_MAX);

    /* the subject: the clock, the link above it and the battery under it */
    s_clock = centred(s_root, FONT_NUM, COL_TEXT, UI_CX, CLOCK_Y, 164);
    s_link = ui_glyph_create(s_root, FONT_ICON_M, UI_CX, TOP_Y);
    s_host = ui_sigil_create(s_root, UI_CX, TOP_Y, HOST_R);
    s_age = ui_tally_create(s_root, UI_CX, TOP_Y);
    s_batt_arc = ui_arc_create(s_root, UI_CX, LOW_Y, BATT_R, BATT_W);
    s_batt = ui_glyph_create(s_root, FONT_ICON_S, UI_CX, LOW_Y);

    /* the About face, in the clock's place while About is selected */
    for (int i = 0; i < A_COUNT; i++) {
        s_about[i] = ui_glyph_create(s_root, FONT_ICON_L, UI_CX + (i - 1) * ABOUT_PITCH, ABOUT_Y);
        s_about_n[i] = centred(s_root, FONT_HEAD, COL_TEXT, UI_CX + (i - 1) * ABOUT_PITCH, ABOUT_Y + ABOUT_NUM_DY, ABOUT_NUM_W);
    }
    s_eye = ui_glyph_create(s_root, FONT_ICON_M, UI_CX, ABOUT_Y + ABOUT_VER_DY);
    /* the firmware version: the screen's one piece of text (it cannot be drawn) */
    s_ver = centred(s_root, FONT_CAPTION, COL_TEXT3, UI_CX, ABOUT_Y + ABOUT_VER_DY, 10);
    lv_obj_set_style_text_align(s_ver, LV_TEXT_ALIGN_LEFT, 0);

    /* rule 6: A does to the selected toggle what its glyph shows, B is the next one */
    s_hint[0] = ui_key_hint_create(s_root, UI_KEY_A);
    s_hint[1] = ui_key_hint_create(s_root, UI_KEY_B);
    ui_key_hint_set(s_hint[1], ICON_NEXT);
    lv_obj_t *hide[] = {s_host, s_age, s_about[0], s_about[1], s_about[2], s_about_n[0], s_about_n[1], s_about_n[2], s_eye, s_ver};
    for (size_t i = 0; i < sizeof hide / sizeof hide[0]; i++) lv_obj_add_flag(hide[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
