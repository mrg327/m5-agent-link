/* The TRACKPAD screen in Sigil (docs/design/GLYPHS.md §5; EXPERIMENTS.md, proto 0.20): the glass drives the
 * pointer of the PC the daemon runs on, and the screen is the pad. A drag moves the pointer (with acceleration),
 * a tap is a left click and a tap soon after near the last one the second click of a double click, a hold is a
 * right click, a turn along the bezel scrolls (clockwise = down, a notch per 15 degrees). Moves are summed into
 * one cmd_op_pointer per NAV_POINTER_COALESCE_MS; a click or a notch goes out at once, carrying any move still
 * pending. A, B long and the chord leave.
 *
 * On glass: a large faint pointer in the middle (what a drag moves) and the how-to as three pictograms, each
 * where its gesture is made: the mouse with its wheel under an arc along the bezel (turn the bezel to scroll),
 * the mouse with its left button (a tap), and the mouse with its right button inside a hold ring (a hold). When
 * the pad cannot work, a sign takes the pictograms' place: the plug (offline), "computer, download" (the daemon
 * is too old), "computer, pointer, cross" (this PC has no pointer to drive), "computer, refused". The words are
 * in the report (rule 9). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "hal.h"
#include "nav.h"
#include "ui_icons.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define POINTER_Y 212
#define RIM_R 190       /* the scroll arc: just inside the session ring */
#define RIM_W 4
#define RIM_SPAN 56     /* degrees, centred on 12 o'clock */
#define WHEEL_Y 80
#define CLICK_Y 302     /* the two click pictograms: with the wheel they stand round the pointer, clear of */
#define CLICK_DX 116    /* the key hint and of a toast's sign */
#define HOLD_R 34       /* the hold ring round the right button's mouse */
#define HOLD_W 4
#define HOLD_PM 720     /* how far the pictogram's ring is filled: a hold in progress */
#define SIGN_Y UI_Y_TOAST /* why the pad cannot work: where the toast that announced it was, so one follows the other */
#define RGB_FAINT 0x38383c /* the pointer: there, and out of the way */

typedef enum { PAD_OK, PAD_OFFLINE, PAD_OLD, PAD_NO_POINTER, PAD_REFUSED } pad_t;

static int s_fast_port = -1; /* the port asked for the short interval, -1 none */
static lv_obj_t *s_root, *s_pointer, *s_rim, *s_wheel, *s_left, *s_right, *s_hold, *s_sign, *s_hint;
static uint32_t s_now, s_sent_ms;
static char s_hint_txt[128];    /* in words, for the report: the how-to, or why the pad cannot work */
static pad_t s_pad = PAD_OK, s_pad_shown = PAD_OK;
static bool s_failed;           /* the daemon refused: the sign says why until the screen reopens */
static int s_rx10, s_ry10;      /* sub-pixel remainder of the accelerated moves, tenths of a pixel */
static int s_pend_dx, s_pend_dy; /* pointer pixels not sent yet */
static int s_scrub_last;
static uint32_t s_tap_ms;       /* the last left click: a double click needs the next one soon and near */
static int s_tap_x, s_tap_y, s_tap_n;
/* for the report */
static int s_n_left, s_n_right, s_n_double, s_wheel_sum, s_moved_x, s_moved_y;

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static const char *const k_hint = "Drag to move, tap to click\nHold for a right click\nTurn the bezel to scroll";

/* The pad's state: the pictograms while it works, otherwise the sign for why it does not; `words` is the same
 * in words. */
static void set_pad(pad_t pad, const char *words)
{
    static const char *const sign[] = {"", ICON_OFFLINE, ICON_COMPUTER ICON_UPDATE, ICON_COMPUTER ICON_CURSOR ICON_X, ICON_COMPUTER ICON_REFUSED};
    snprintf(s_hint_txt, sizeof s_hint_txt, "%s", words);
    s_pad = pad;
    lv_obj_t *const howto[] = {s_rim, s_wheel, s_left, s_right, s_hold};
    for (size_t i = 0; i < sizeof howto / sizeof howto[0]; i++) show(howto[i], pad == PAD_OK);
    if (pad != PAD_OK && pad != s_pad_shown) ui_sign_set(s_sign, sign[pad], NULL, RGB_AMBER);
    s_pad_shown = pad;
    show(s_sign, pad != PAD_OK);
}

void ui_trackpad_refresh(uint32_t now)
{
    s_now = now;
    if (s_failed) return;
    if (!ui_link_connected()) set_pad(PAD_OFFLINE, "Offline");
    else if (!model_pointer()) set_pad(PAD_OLD, "agentlinkd on this PC has no trackpad");
    else set_pad(PAD_OK, k_hint);
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
    if (err == 1 && msg && msg[0]) set_pad(PAD_REFUSED, msg); /* the daemon's own words: to the report */
    else if (err == 2) set_pad(PAD_NO_POINTER, "This PC has no pointer agentlinkd can drive");
    else set_pad(PAD_REFUSED, "The PC refused");
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

lv_obj_t *ui_trackpad_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* the pad: the pointer a drag moves, faint */
    s_pointer = ui_glyph_create(s_root, FONT_ICON_HERO, UI_CX, POINTER_Y);
    ui_glyph_set(s_pointer, ICON_CURSOR, lv_color_hex(RGB_FAINT));

    /* the how-to, three pictograms, each where its gesture is made */
    s_rim = ui_arc_create(s_root, UI_CX, UI_CX, RIM_R, RIM_W); /* along the bezel: a turn scrolls */
    ui_arc_set_span(s_rim, -RIM_SPAN / 2, RIM_SPAN);
    ui_arc_set(s_rim, 1000, COL_TEXT2, lv_color_black());
    s_wheel = ui_glyph_create(s_root, FONT_ICON_L, UI_CX, WHEEL_Y);
    ui_glyph_set(s_wheel, ICON_WHEEL, COL_TEXT2);
    s_left = ui_glyph_create(s_root, FONT_ICON_L, UI_CX - CLICK_DX, CLICK_Y); /* a tap: the left button */
    ui_glyph_set(s_left, ICON_LEFT_CLICK, COL_TEXT2);
    s_right = ui_glyph_create(s_root, FONT_ICON_L, UI_CX + CLICK_DX, CLICK_Y); /* a hold: the right button, in a hold ring */
    ui_glyph_set(s_right, ICON_RIGHT_CLICK, COL_TEXT2);
    s_hold = ui_arc_create(s_root, UI_CX + CLICK_DX, CLICK_Y, HOLD_R, HOLD_W);
    ui_arc_set(s_hold, HOLD_PM, COL_TEXT2, COL_FILL2);

    s_sign = ui_sign_create(s_root, UI_CX, SIGN_Y);
    show(s_sign, false);
    /* rule 6: A goes back (B leaves only by a long press, so it has no hint) */
    s_hint = ui_key_hint_create(s_root, UI_KEY_A);
    ui_key_hint_set(s_hint, ICON_BACK);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
