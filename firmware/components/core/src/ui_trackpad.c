/* The TRACKPAD screen (EXPERIMENTS.md, proto 0.20): the glass drives the pointer of the PC the daemon runs on.
 * A drag moves it (with acceleration), a tap is a left click and a tap soon after near the last one the second
 * click of a double click, a hold is a right click, a turn along the bezel scrolls (clockwise = down, a notch per
 * 15 degrees). Moves are summed into one cmd_op_pointer per NAV_POINTER_COALESCE_MS; a click or a notch goes out
 * at once, carrying any move still pending. A, B long and the chord leave. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "hal.h"
#include "nav.h"
#include "ui_icons.h"
#include "ui_internal.h"
#include "ui_theme.h"

#define ICON_Y 150
#define TITLE_Y 206
#define HINT_Y 262
#define HINT_LINES 3

static int s_fast_port = -1; /* the port asked for the short interval, -1 none */
static lv_obj_t *s_root, *s_icon, *s_title, *s_hint, *s_badge[2], *s_key[2], *s_word[2];
static uint32_t s_now, s_sent_ms;
static char s_hint_txt[128];
static bool s_failed;           /* the daemon refused: the hint says why until the screen reopens */
static int s_rx10, s_ry10;      /* sub-pixel remainder of the accelerated moves, tenths of a pixel */
static int s_pend_dx, s_pend_dy; /* pointer pixels not sent yet */
static int s_scrub_last;
static uint32_t s_tap_ms;       /* the last left click: a double click needs the next one soon and near */
static int s_tap_x, s_tap_y, s_tap_n;
/* for the report */
static int s_n_left, s_n_right, s_n_double, s_wheel_sum, s_moved_x, s_moved_y;

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static const char *const k_hint = "Drag to move, tap to click\nHold for a right click\nTurn the bezel to scroll";

static void set_hint(const char *t, bool warn)
{
    theme_set_text(s_hint, s_hint_txt, sizeof s_hint_txt, t);
    theme_set_text_color(s_hint, warn ? lv_color_hex(RGB_AMBER) : COL_TEXT2);
}

void ui_trackpad_refresh(uint32_t now)
{
    s_now = now;
    if (s_failed) return;
    if (!ui_link_connected()) set_hint("Offline", true);
    else if (!model_pointer()) set_hint("agentlinkd on this PC has no trackpad", true);
    else set_hint(k_hint, false);
}

/* One command: what is pending, then the click and the notches. False when it could not go out. */
static bool send(int btn, int n, int wheel)
{
    int dx = s_pend_dx, dy = s_pend_dy;
    s_pend_dx = s_pend_dy = 0;
    if (!dx && !dy && btn < 0 && !wheel) return true;
    s_sent_ms = s_now;
    s_moved_x += dx;
    s_moved_y += dy;
    return g_ui_actions->pointer && g_ui_actions->pointer(dx, dy, btn, n, wheel);
}

static void refused(void)
{
    ui_toast(ui_link_connected() ? "Not sent" : "Offline", RGB_AMBER);
    g_ui_actions->reject();
}

void ui_trackpad_open(uint32_t now)
{
    s_now = now;
    s_sent_ms = now - NAV_POINTER_COALESCE_MS; /* the first move goes out at once */
    s_failed = false;
    s_rx10 = s_ry10 = s_pend_dx = s_pend_dy = 0;
    s_tap_ms = 0;
    s_tap_n = 0;
    s_n_left = s_n_right = s_n_double = s_wheel_sum = s_moved_x = s_moved_y = 0;
    ui_home_rim_mode(true);
    if (g_ui_actions->sectors) g_ui_actions->sectors(NAV_POINTER_SECTORS); /* a notch per 15 degrees */
    if (g_ui_actions->moves) g_ui_actions->moves(true);
    s_fast_port = ui_link_transport();
    if (s_fast_port >= 0) hal_link_interactive((hal_link_kind_t)s_fast_port, true); /* the short BLE interval, as in SCROLL */
    ui_trackpad_refresh(now);
    show(s_root, true);
    dbg_event("ui", "trackpad", "\"pointer\":%s", model_pointer() ? "true" : "false");
}

void ui_trackpad_close(void)
{
    send(-1, 0, 0); /* a move still pending */
    if (g_ui_actions->moves) g_ui_actions->moves(false);
    if (s_fast_port >= 0) hal_link_interactive((hal_link_kind_t)s_fast_port, false);
    s_fast_port = -1;
    show(s_root, false);
    dbg_event("ui", "trackpad_close", "\"left\":%d,\"right\":%d,\"double\":%d,\"wheel\":%d", s_n_left, s_n_right, s_n_double,
              s_wheel_sum);
}

void ui_trackpad_failed(int err, const char *msg)
{
    s_failed = true;
    set_hint(err == 1 && msg && msg[0] ? msg : err == 2 ? "This PC has no pointer agentlinkd can drive" : "The PC refused", true);
}

static void move(const gesture_evt_t *g)
{
    int gain = nav_pointer_gain10(g->dx, g->dy);
    s_rx10 += g->dx * gain;
    s_ry10 += g->dy * gain;
    s_pend_dx += s_rx10 / 10;
    s_pend_dy += s_ry10 / 10;
    s_rx10 %= 10;
    s_ry10 %= 10;
    if (s_now - s_sent_ms >= NAV_POINTER_COALESCE_MS) send(-1, 0, 0); /* an idle link: at once */
}

static void left_click(int x, int y)
{
    int dx = x - s_tap_x, dy = y - s_tap_y;
    bool again = s_tap_n > 0 && s_now - s_tap_ms <= NAV_DOUBLE_CLICK_MS && dx * dx + dy * dy <= NAV_DOUBLE_CLICK_PX * NAV_DOUBLE_CLICK_PX;
    int n = again && s_tap_n < 3 ? s_tap_n + 1 : 1;
    s_tap_ms = s_now;
    s_tap_x = x;
    s_tap_y = y;
    s_tap_n = n;
    if (!send(0, n, 0)) {
        refused();
        return;
    }
    s_n_left++;
    if (n == 2) s_n_double++;
    dbg_event("ui", "pointer_click", "\"btn\":\"left\",\"n\":%d", n);
    g_ui_actions->tick();
}

bool ui_trackpad_act(nav_action_t a, const gesture_evt_t *g)
{
    switch (a) {
    case NAV_POINTER:
        if (g->kind == G_MOVE) {
            move(g);
        } else if (g->kind == G_TAP) {
            left_click(g->x, g->y);
        } else if (g->kind == G_DOUBLE_TAP) { /* injected input only: both clicks */
            left_click(g->x, g->y);
            left_click(g->x, g->y);
        } else if (g->kind == G_LONG_PRESS) {
            s_tap_n = 0;
            if (!send(1, 1, 0)) {
                refused();
                return true;
            }
            s_n_right++;
            dbg_event("ui", "pointer_click", "\"btn\":\"right\",\"n\":1");
            g_ui_actions->tick();
        }
        return true;
    case NAV_SCRUB: {
        int d = nav_rim_scrub(&s_scrub_last, g->kind, g->x, NAV_POINTER_SECTORS);
        if (!d) return true;
        if (!send(-1, 0, d)) {
            refused();
            return true;
        }
        s_wheel_sum += d;
        g_ui_actions->detent();
        return true;
    }
    default: return false;
    }
}

void ui_trackpad_tick(uint32_t now)
{
    s_now = now;
    if ((s_pend_dx || s_pend_dy) && now - s_sent_ms >= NAV_POINTER_COALESCE_MS) send(-1, 0, 0);
}

void ui_trackpad_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "trackpad");
    ui_report_add(r, "Trackpad");
    ui_report_add(r, "clicks left %d right %d double %d", s_n_left, s_n_right, s_n_double);
    ui_report_add(r, "moved %d,%d wheel %d", s_moved_x, s_moved_y, s_wheel_sum);
    char tmp[sizeof s_hint_txt];
    snprintf(tmp, sizeof tmp, "%s", s_hint_txt);
    char *nl = strchr(tmp, '\n');
    if (nl) *nl = 0;
    ui_report_add(r, "%s", tmp);
}

static lv_obj_t *centred(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, int cy, int w, int lines)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(font));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, UI_CX - w / 2, cy - lv_font_get_line_height(font) / 2);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ui_trackpad_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_icon = centred(s_root, FONT_ICON_L, COL_TEXT2, ICON_Y, 80, 1);
    lv_label_set_text(s_icon, ICON_COMPUTER);
    s_title = centred(s_root, FONT_HEAD, COL_TEXT, TITLE_Y, theme_chord(TITLE_Y, 30), 1);
    lv_label_set_text(s_title, "Trackpad");
    s_hint = centred(s_root, FONT_CAPTION, COL_TEXT2, HINT_Y, theme_chord(HINT_Y + 60, 24), HINT_LINES);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_MODE_WRAP);
    static const char *const words[2] = {"Back", "Back"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    show(s_badge[1], false); /* B leaves only by a long press; A is the one to show */
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
