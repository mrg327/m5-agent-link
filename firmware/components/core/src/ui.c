/* Ring screen host: toast, alert glow, link state, report for the debug channel. */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_countdown.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_mote_pair.h"
#include "ui_ota.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "ui_toy.h"

#define TOAST_MS 1800
/* The badge at 12 o'clock (issue 20261126-0a74): what waits behind the open screen, and how many;
 * a target 72 x 56 around it. */
#define BADGE_D 28
#define BADGE_CY 20
#define BADGE_HIT_W 72
#define BADGE_HIT_H 56
#define BADGE_POP 358 /* it enters at 1.4 x (256 = 1.0) */

const ui_actions_t *g_ui_actions;
static lv_obj_t *s_home, *s_agent, *s_ask, *s_scroll, *s_status, *s_watch, *s_voice, *s_keys,
    *s_read, *s_labs, *s_trackpad, *s_graph, *s_toy;
static nav_mode_t s_mode = MODE_HOME;
static uint32_t s_mode_seq; /* counts mode changes (ui_mode_seq) */
static uint32_t s_mode_sid; /* the session selected when the mode opened */
static int s_rel_y;
static bool s_link_up, s_animate = true;
static uint32_t s_seen_rev;
static uint32_t s_now_ms;

/* Black under the sign, so what a screen has there rests while it shows: the bottom slot (x
 * 125..341, y 340..406), and above it a wider band for a row of text (x 113..353, y 326..346). Only
 * on the screens that keep content there (toast_blanks_slot): elsewhere the sign's own black margin
 * is all. */
#define TOAST_MASK_W 216
#define TOAST_MASK_Y 340
#define TOAST_MASK_H 66
#define TOAST_BAND_W 240
#define TOAST_BAND_Y 326
#define TOAST_BAND_H 20
/* a sign (GLYPHS.md rule 8, §3.7) on the key-hint row */
static lv_obj_t *s_toast, *s_toast_mask[2];
static uint32_t s_toast_until_ms;
/* the toast in words: the report and the log; the screen shows its glyphs */
static char s_toast_txt[64];
/* The alert glow (issue 20261126-0ca0): the kind's ring, the session's sigil and the kind's hero
 * glyph, so a glance says who and what without a word. */
#define GLOW_SIGIL_Y 112
#define GLOW_SIGIL_R 40
#define GLOW_HERO_Y 262
static lv_obj_t *s_glow, *s_glow_ring, *s_glow_sigil, *s_glow_hero;
static char s_glow_name[48]; /* the words, for the report */
static const char *s_glow_kind = "";
static lv_obj_t *s_badge, *s_badge_num, *s_badge_glyph; /* what waits (ui_offer_attention) */
static lv_obj_t *s_pair, *s_pair_num;                   /* BLE pairing passkey (ui_pair_show) */
static uint32_t s_pair_until_ms;
#define PAIR_SHOW_MS 60000u /* the SM gives up after 30 s; this only guards a lost end event */

lv_obj_t *ui_new_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, UI_SIZE, UI_SIZE);
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

/* BLE pairing (ui_pair_show): the Bluetooth glyph over six digits, on a black disc. */
static void pair_create(lv_obj_t *top)
{
    s_pair = lv_obj_create(top);
    lv_obj_remove_style_all(s_pair);
    lv_obj_set_size(s_pair, UI_SIZE, UI_SIZE);
    lv_obj_set_style_bg_color(s_pair, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_pair, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_pair, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *glyph = lv_label_create(s_pair);
    lv_label_set_text(glyph, ICON_BLUETOOTH);
    lv_obj_set_style_text_font(glyph, FONT_ICON_XL, 0);
    lv_obj_set_style_text_color(glyph, lv_color_hex(RGB_ACCENT), 0);
    lv_obj_align(glyph, LV_ALIGN_CENTER, 0, -70);
    s_pair_num = lv_label_create(s_pair);
    lv_obj_set_style_text_font(s_pair_num, FONT_NUM, 0);
    lv_obj_set_style_text_color(s_pair_num, COL_TEXT, 0);
    lv_obj_align(s_pair_num, LV_ALIGN_CENTER, 0, 30);
    ui_mote_pair_create(s_pair); /* the Mote, still at 12 while the digits wait (FUN_MOTE) */
    lv_obj_add_flag(s_pair, LV_OBJ_FLAG_HIDDEN);
}

/* A disc of the waiting request's colour at 12 o'clock, on the rim, with how many wait in black,
 * or with one waiting its kind's glyph (glyphs over words; GLYPHS.md rule 4 allows a count in a
 * badge). */
static void badge_create(lv_obj_t *top)
{
    s_badge = lv_obj_create(top);
    lv_obj_remove_style_all(s_badge);
    lv_obj_set_size(s_badge, BADGE_D, BADGE_D);
    lv_obj_set_pos(s_badge, UI_CX - BADGE_D / 2, BADGE_CY - BADGE_D / 2);
    lv_obj_set_style_radius(s_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_badge, COL_BG, 0);
    lv_obj_set_style_border_width(s_badge, 2, 0);
    lv_obj_set_style_transform_pivot_x(s_badge, BADGE_D / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_badge, BADGE_D / 2, 0);
    lv_obj_remove_flag(s_badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_badge_num = lv_label_create(s_badge);
    lv_obj_set_style_text_font(s_badge_num, FONT_CAPTION, 0);
    lv_obj_set_style_text_color(s_badge_num, COL_ON_ACCENT, 0);
    lv_label_set_text(s_badge_num, "");
    lv_obj_center(s_badge_num);
    /* children sit inside the 2 px border */
    s_badge_glyph = ui_glyph_create(s_badge, FONT_ICON_S, BADGE_D / 2 - 2, BADGE_D / 2 - 2);
    lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
}

static void create_overlays(void)
{
    lv_obj_t *top = lv_layer_top();
    ui_ota_init(top); /* under the toast, so a refusal shows over it */
    /* One slot on every screen, at the bottom (the key hints are on the upper rim and stay in
     * sight); a sign that carries content is wider, with a black margin. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *m = s_toast_mask[i] = lv_obj_create(top);
        lv_obj_remove_style_all(m);
        if (i == 0)
            lv_obj_set_size(m, TOAST_MASK_W, TOAST_MASK_H),
                lv_obj_set_pos(m, UI_CX - TOAST_MASK_W / 2, TOAST_MASK_Y);
        else
            lv_obj_set_size(m, TOAST_BAND_W, TOAST_BAND_H),
                lv_obj_set_pos(m, UI_CX - TOAST_BAND_W / 2, TOAST_BAND_Y);
        lv_obj_set_style_radius(m, i == 0 ? 14 : 0, 0);
        lv_obj_set_style_bg_color(m, COL_BG, 0);
        lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
        lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
    }
    s_toast = ui_sign_create(top, UI_CX, UI_Y_TOAST);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    ui_burst_init(top); /* over the toast, under the glow */

    badge_create(top);

    s_glow = lv_obj_create(top);
    lv_obj_remove_style_all(s_glow);
    lv_obj_set_size(s_glow, UI_SIZE, UI_SIZE);
    lv_obj_set_style_bg_color(s_glow, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_glow, LV_OPA_COVER, 0);
    s_glow_ring = ring_create(s_glow);
    s_glow_sigil = ui_sigil_create(s_glow, UI_CX, GLOW_SIGIL_Y, GLOW_SIGIL_R);
    ui_sigil_set_color(s_glow_sigil, COL_TEXT2); /* grey: the only colour is the kind's */
    s_glow_hero = ui_glyph_create(s_glow, FONT_ICON_HERO, UI_CX, GLOW_HERO_Y);
    lv_obj_add_flag(s_glow, LV_OBJ_FLAG_HIDDEN);

    pair_create(top); /* over the glow too: the PC waits for these digits */
}

void ui_pair_show(uint32_t passkey)
{
    char digits[8];
    snprintf(digits, sizeof digits, "%06u", (unsigned)(passkey % 1000000u));
    lv_label_set_text(s_pair_num, digits);
    lv_obj_align(s_pair_num, LV_ALIGN_CENTER, 0, 30);
    lv_obj_remove_flag(s_pair, LV_OBJ_FLAG_HIDDEN);
    s_pair_until_ms = s_now_ms + PAIR_SHOW_MS;
    ui_mote_pair_wait();
    /* the passkey itself stays off the debug log */
    dbg_event("ui", "pair", "\"shown\":true");
}

/* A lost end event: the passkey goes after PAIR_SHOW_MS. */
static void pair_tick(uint32_t now_ms)
{
    bool shown = !lv_obj_has_flag(s_pair, LV_OBJ_FLAG_HIDDEN);
    if (ui_mote_pair_tick(now_ms, shown) || (shown && (int32_t)(now_ms - s_pair_until_ms) >= 0)) {
        ui_pair_hide();
    }
}

static bool s_paired_pending; /* "Paired" waits for the Mote's flare to end (owner, 2026-11-27) */

static void paired_sign(void)
{ ui_toast_sign("Paired", ICON_BLUETOOTH ICON_CHECK, NULL, RGB_DONE); }

void ui_pair_done(void)
{
    /* the Mote's one flare keeps the digits up for its 600 ms, and the "Paired" sign is drawn only
     * once they go, so the flare does not pay for a sign hidden under it; otherwise both at once */
    if (!lv_obj_has_flag(s_pair, LV_OBJ_FLAG_HIDDEN) && ui_mote_pair_done(s_now_ms)) {
        s_paired_pending = true;
        return;
    }
    ui_pair_hide();
    paired_sign();
}

void ui_pair_hide(void)
{
    if (!lv_obj_has_flag(s_pair, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_pair, LV_OBJ_FLAG_HIDDEN);
        dbg_event("ui", "pair", "\"shown\":false");
    }
    if (s_paired_pending) { /* the flare ended, or something closed the screen first */
        s_paired_pending = false;
        paired_sign();
    }
}

void ui_init(const ui_actions_t *actions)
{
    g_ui_actions = actions;
    s_home = ui_home_init();
    s_agent = ui_agent_init(s_home);
    s_ask = ui_ask_init(s_home);
    s_scroll = ui_scroll_init(s_home);
    s_status = ui_status_init(s_home);
    s_watch = ui_watch_init(s_home);
    s_voice = ui_voice_init(s_home);
    s_keys = ui_keys_init(s_home);
    s_read = ui_read_init(s_home);
    s_graph = ui_graph_init(s_home);
    s_labs = ui_labs_init(s_home);
    s_trackpad = ui_trackpad_init(s_home);
    s_toy = ui_toy_init(s_home);
    ui_countdown_init(s_home); /* over every mode's content, under the toast and the glow */
    create_overlays();
    lv_screen_load(s_home);
    ui_home_refresh(0);
    dbg_event("ui", "screen", "\"screen\":\"ring\"");
}

void ui_set_animations(bool enabled)
{
    s_animate = enabled;
    ui_home_animations_changed();
}
bool ui_animations_allowed(void) { return s_animate; }

bool ui_link_connected(void) { return s_link_up; }

static bool s_was_up;
static uint32_t s_link_down_at;
uint32_t ui_link_down_ms(void) { return s_link_up || !s_was_up ? 0 : s_now_ms - s_link_down_at; }

void ui_set_link_state(bool connected)
{
    if (s_link_up == connected) return;
    if (connected) s_was_up = true;
    else s_link_down_at = s_now_ms;
    s_link_up = connected;
    s_seen_rev = 0; /* force a refresh */
}

static int s_link_kind = -1;
int ui_link_transport(void) { return s_link_kind; }

void ui_set_link_transport(int kind)
{
    if (s_link_kind == kind) return;
    s_link_kind = kind;
    s_seen_rev = 0;
}

nav_mode_t ui_mode(void) { return s_mode; }
bool ui_dimmed(void) { return s_mode == MODE_HOME && ui_home_ambient(); }
int ui_last_release_y(void) { return s_rel_y; }

/* The session WATCH was on when ASK or SCROLL took over; they return there instead of HOME. */
static uint32_t s_resume_watch_sid;
/* A request took over from a toy: ASK returns to it, paused (EXPERIMENTS.md "Toys"). */
static bool s_resume_toy;
static uint32_t s_voice_rid; /* the request VOICE answers (ui_go_voice) */
static bool s_voice_queue;   /* VOICE queues the dictation (ui_go_voice) */
static int s_voice_qi = -1;  /* VOICE answers that question of s_voice_rid (ui_go_voice_answer) */
static bool s_voice_qlast;

/* ASK closed because its request went from the model (a new link drops every card, issue
 * 20261126-d95a): the rid, and the mode change that closed it. */
static uint32_t s_ask_lost_rid, s_ask_lost_seq;

static void enter_mode(nav_mode_t m)
{
    if (m != MODE_ASK && m != MODE_SCROLL) s_resume_watch_sid = 0;
    if (m != MODE_ASK) { s_resume_toy = false; }
    if (m == s_mode) return;
    if (s_mode == MODE_AGENT) ui_agent_close();
    if (s_mode == MODE_ASK) {
        uint32_t rid = ui_ask_rid();
        const attention_t *a = rid ? model_attention_find(rid) : NULL;
        /* back to the deck or WATCH without an answer: the queue passes over it (issue
         * 20261126-aa28) */
        if (a && !a->answer_sent && (m == MODE_HOME || m == MODE_WATCH || m == MODE_TOY)) {
            model_attention_skip(rid);
        }
        s_ask_lost_rid = rid && !a ? rid : 0;
        s_ask_lost_seq = s_mode_seq + 1;
        ui_ask_close();
    }
    if (s_mode == MODE_SCROLL) ui_scroll_close();
    if (s_mode == MODE_STATUS) ui_status_close();
    if (s_mode == MODE_WATCH) ui_watch_close();
    if (s_mode == MODE_VOICE) ui_voice_close();
    if (s_mode == MODE_KEYS) ui_keys_close();
    if (s_mode == MODE_READ) ui_read_close();
    if (s_mode == MODE_LABS) ui_labs_close();
    if (s_mode == MODE_TRACKPAD) ui_trackpad_close();
    if (s_mode == MODE_GRAPH) ui_graph_close();
    if (s_mode == MODE_TOY) { ui_toy_close(); }
    s_mode = m;
    s_mode_seq++;
    s_mode_sid = ui_home_sel_sid();
    ui_home_set_card_shown(m == MODE_HOME);
    if (m == MODE_AGENT) ui_agent_open(ui_home_sel_sid(), s_now_ms);
    else if (m == MODE_ASK) ui_ask_open(ui_home_sel_sid(), s_now_ms);
    else if (m == MODE_SCROLL) ui_scroll_open(ui_home_sel_sid(), s_now_ms);
    else if (m == MODE_STATUS) {
        ui_home_refresh(s_now_ms); /* the ring shows sessions again, e.g. after AGENT -> Status */
        ui_status_open(s_now_ms);
    } else if (m == MODE_WATCH) ui_watch_open(ui_home_sel_sid(), s_now_ms);
    else if (m == MODE_VOICE)
        ui_voice_open(ui_home_sel_sid(), s_voice_rid, s_voice_queue, s_voice_qi, s_voice_qlast,
                      s_now_ms);
    else if (m == MODE_KEYS) ui_keys_open(ui_home_sel_sid(), s_now_ms);
    else if (m == MODE_READ) ui_read_open(ui_home_sel_sid(), s_now_ms);
    else if (m == MODE_LABS) {
        ui_home_refresh(s_now_ms);
        ui_labs_open(s_now_ms);
    } else if (m == MODE_TRACKPAD) ui_trackpad_open(s_now_ms);
    else if (m == MODE_GRAPH) ui_graph_open(ui_home_sel_sid(), s_now_ms);
    else ui_home_refresh(s_now_ms); /* the ring shows sessions again */
    if (m == MODE_TOY) { ui_toy_open(s_now_ms); }
    if (m != MODE_HOME && m != MODE_KEYS) {
        /* deeper modes rise into place; KEYS records strokes at once */
        lv_obj_t *const roots[MODE_COUNT] = {NULL,       s_agent, s_ask,  s_scroll, s_status,
                                             s_watch,    s_voice, s_keys, s_read,   s_labs,
                                             s_trackpad, s_graph, s_toy};
        ui_motion_enter(roots[m]);
    }
    dbg_event("ui", "mode", "\"mode\":\"%s\"", nav_mode_name(m));
}

/* ASK or SCROLL opened over WATCH goes back to WATCH when it closes, if the watched session is
 * still there; ASK that took over from a toy goes back to the toy, paused (what waits next takes
 * over from there at once). TRACKPAD and a toy go back to the experiments they were opened from. */
static void leave_mode(void)
{
    if (s_mode == MODE_TRACKPAD || s_mode == MODE_TOY) {
        enter_mode(MODE_LABS);
        return;
    }
    if (s_mode == MODE_ASK && s_resume_toy) {
        enter_mode(MODE_TOY);
        return;
    }
    uint32_t sid = s_mode == MODE_ASK || s_mode == MODE_SCROLL ? s_resume_watch_sid : 0;
    const session_t *s = sid ? model_find(sid) : NULL;
    enter_mode(MODE_HOME);
    if (!s || s->state == SESS_ENDED) return;
    ui_home_select_sid(sid, "watch");
    enter_mode(MODE_WATCH);
}

static void open_ask(void)
{
    uint32_t resume = s_mode == MODE_WATCH ? ui_watch_sid() : 0;
    enter_mode(MODE_ASK);
    if (s_mode == MODE_ASK) s_resume_watch_sid = resume;
}

void ui_go_home(void) { leave_mode(); }
/* Back, as a screen asks for it (hold still off the rows): the ASK reader closes first. */
void ui_go_back(void)
{
    if (!(s_mode == MODE_ASK && ui_ask_back()) && !(s_mode == MODE_READ && ui_read_back()))
        leave_mode();
    g_ui_actions->tick();
}
void ui_go_watch(void) { enter_mode(MODE_WATCH); }
void ui_go_voice(uint32_t rid, bool queue)
{
    s_voice_rid = rid;
    s_voice_queue = queue;
    s_voice_qi = -1;
    enter_mode(MODE_VOICE);
}
void ui_go_voice_answer(uint32_t rid, int qi, bool last)
{
    s_voice_rid = rid;
    s_voice_queue = false;
    s_voice_qi = qi;
    s_voice_qlast = last;
    enter_mode(MODE_VOICE);
}
void ui_go_read(void) { enter_mode(MODE_READ); }
void ui_go_graph(void) { enter_mode(MODE_GRAPH); }
void ui_go_replies(void)
{
    enter_mode(MODE_AGENT);
    if (s_mode == MODE_AGENT) ui_agent_show_replies();
}
void ui_go_keys(void) { enter_mode(MODE_KEYS); }
void ui_go_trackpad(void) { enter_mode(MODE_TRACKPAD); }
void ui_go_toy(int i)
{
    ui_toy_select(i);
    enter_mode(MODE_TOY);
}
uint32_t ui_watched_sid(void) { return s_mode == MODE_WATCH ? ui_watch_sid() : 0; }

void ui_scroll_failed(void)
{
    if (s_mode == MODE_SCROLL) leave_mode();
}

void ui_pointer_failed(int err, const char *msg)
{
    if (s_mode == MODE_TRACKPAD) ui_trackpad_failed(err, msg);
}

void ui_scroll_pos(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total,
                   uint32_t total, const char *preview)
{
    if (s_mode == MODE_SCROLL)
        ui_scroll_on_pos(sid, lines_above, at_bottom, has_total, total, preview);
}

/* An attention always lands on the deck, whatever was open. */
void ui_show_session(uint32_t sid)
{
    enter_mode(MODE_HOME);
    ui_home_select_sid(sid, "attention");
}

/* A permission or question opens ASK on its session (UI-V2 §2); done/error only update the card. */
void ui_show_attention(uint32_t sid)
{
    uint32_t resume = s_mode == MODE_WATCH                          ? ui_watch_sid()
                      : s_mode == MODE_ASK || s_mode == MODE_SCROLL ? s_resume_watch_sid
                                                                    : 0;
    if (ui_asking(sid)) { return; /* ASK already shows its session's request */ }
    ui_show_session(sid);
    if (ui_home_sel_sid() == sid && ui_home_askable()) {
        enter_mode(MODE_ASK);
        s_resume_watch_sid = resume;
    }
}

uint32_t ui_pending_attention_sid(void)
{
    const attention_t *a = model_attention_first();
    return a && !a->answer_sent && (a->kind == ATT_APPROVAL || a->kind == ATT_QUESTION) ? a->sid
                                                                                        : 0;
}

/* ───────── what waits: the queue (issues 20261122-883f, 20261126-aa28) ───────── */

/* Requests or an error that did not take over: the screen the user works in owns them, ASK shows
 * another request, or the display was dark. The requests form a queue (model_attention_next: least
 * time left first, passing over those the user went back from); its head opens when the user comes
 * back to the deck or WATCH, after an answer closes ASK, or when the display wakes
 * (ui_reveal_attention), never under a finger or a held button, and on the deck or WATCH only once
 * the user has left them alone (issue 20261126-0a74). Meanwhile a badge of its colour, with how
 * many wait, sits on the rim. */
static bool s_wait_req;      /* requests wait in the queue */
static uint32_t s_wait_err;  /* the session of an error card that waits, 0 = none */
static uint32_t s_wait_seq;  /* the mode it found: offered again once the mode has changed */
static bool s_wait_quiet;    /* it waits only for the user to stop touching HOME or WATCH */
static uint32_t s_badge_rgb; /* the badge as drawn: colour, count, glyph */
static int s_badge_n = -1;
static const char *s_badge_icon = "";
static uint32_t s_report_sid; /* the session the badge stands for, in words for the report */
static int s_report_n;        /* how many it counts, for the report */

uint32_t ui_mode_seq(void) { return s_mode_seq; }
bool ui_asking(uint32_t sid) { return s_mode == MODE_ASK && ui_home_sel_sid() == sid; }

/* Screens where the user composes, reads or works: an automatic takeover would lose the typed
 * text, the recording, the page or the place (UI-V3 §1.6 "Nothing is lost"). AGENT (a hold on Stop
 * or Run again) and SCROLL (the place in the terminal) too (issue 20261126-0a74); both close
 * themselves after 8 s idle, and the request opens then. A toy is not one: it pauses and yields
 * at once, and ASK comes back to it (s_resume_toy). */
static bool mode_keeps_work(nav_mode_t m)
{
    return m == MODE_KEYS || m == MODE_VOICE || m == MODE_READ || m == MODE_GRAPH ||
           m == MODE_STATUS || m == MODE_LABS || m == MODE_TRACKPAD || m == MODE_AGENT ||
           m == MODE_SCROLL;
}

/* The last touch or button on any screen (not a raise): a request takes over HOME or WATCH only
 * once the user has left them alone for TAKEOVER_QUIET_MS (issue 20261126-0a74), so a tap aimed at
 * the deck never lands on ASK. */
static bool s_touched;
static uint32_t s_touch_ms;

static bool quiet_for_takeover(void)
{ return !s_touched || (int32_t)(lv_tick_get() - s_touch_ms) >= TAKEOVER_QUIET_MS; }

static const attention_t *askable_for(uint32_t sid)
{
    const attention_t *a = model_attention_for_sid(sid);
    return a && (a->kind == ATT_APPROVAL || a->kind == ATT_QUESTION) && !a->answer_sent ? a : NULL;
}

/* Taking over now would lose work, or hide a request the user is answering. */
static bool takeover_blocked(uint32_t sid)
{ return mode_keeps_work(s_mode) || (s_mode == MODE_ASK && !ui_asking(sid)); }

/* The queue's head, other than the request ASK shows. */
static const attention_t *queue_next(void)
{
    const attention_t *shown = s_mode == MODE_ASK ? ui_ask_request() : NULL;
    return model_attention_next(s_now_ms, shown ? shown->rid : 0);
}

/* A finger on the glass or a button held: no card opens under it (issue 20261126-aa28). */
static bool input_down(void)
{ return g_ui_actions->touching() || g_ui_actions->button_down(0) || g_ui_actions->button_down(1); }

/* The screen a takeover opened settles before input acts on it (issue 20261126-86cd): a touch or a
 * press that began before the takeover, or within TAKEOVER_SETTLE_MS after it, was aimed at the
 * screen before. */
static bool s_settle_on;
static uint32_t s_settle_ms, s_settle_seq;

bool ui_input_settling(uint32_t began_ms)
{
    if (!s_settle_on || s_mode_seq != s_settle_seq) { return false; }
    return (int32_t)(began_ms - s_settle_ms) < TAKEOVER_SETTLE_MS;
}

/* Long settled (and clear of the clock's wrap), or the user has moved on. */
static void settle_step(void)
{
    if (s_settle_on && (s_mode_seq != s_settle_seq || s_now_ms - s_settle_ms > 60000u)) {
        s_settle_on = false;
    }
}

/* ASK closed itself (an answer went out and the daemon cleared it, or its window ran out): the
 * queue's next request slides in from the right, with a tick, at once (no quiet time: the user is
 * answering in turn), as long as the user has not moved since (issue 20261126-74c0). */
static bool s_slide;
static uint32_t s_slide_seq;

static bool slide_pending(void) { return s_slide && s_mode_seq == s_slide_seq; }

static void anim_translate_x(void *o, int32_t v) { lv_obj_set_style_translate_x(o, v, 0); }

/* The next one, not the same one: it enters from the right instead of rising. */
static void enter_from_right(lv_obj_t *o)
{
    lv_anim_delete(o, NULL); /* the rise enter_mode started */
    lv_obj_set_style_translate_y(o, 0, 0);
    lv_obj_set_style_translate_x(o, 0, 0);
    if (!ui_animations_allowed()) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, anim_translate_x);
    lv_anim_set_values(&a, UI_MOTION_SLIDE, 0);
    lv_anim_set_duration(&a, UI_MOTION_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void take_over(uint32_t sid, bool error)
{
    bool slide = slide_pending() && !error;
    s_slide = false;
    if (error) { s_wait_err = 0; }
    s_wait_quiet = false;
    lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    bool from_toy = s_mode == MODE_TOY || (s_mode == MODE_ASK && s_resume_toy);
    if (error && !askable_for(sid)) {
        ui_show_session(sid);
    } else {
        ui_show_attention(sid);
    }
    if (from_toy && s_mode == MODE_ASK) { s_resume_toy = true; }
    if (slide && s_mode == MODE_ASK) {
        enter_from_right(s_ask);
        g_ui_actions->tick(); /* the wrist knows there is another */
        dbg_event("ui", "slide_in", "\"rid\":%u", (unsigned)ui_ask_rid());
    }
    s_settle_on = true;
    s_settle_ms = lv_tick_get(); /* now: this runs while the link drains, before ui_tick */
    s_settle_seq = s_mode_seq;
}

void ui_wait_attention(uint32_t sid, bool error, uint32_t rgb)
{
    (void)rgb; /* the badge takes the colour of what the queue opens next */
    if (error) {
        s_wait_err = sid;
    } else {
        s_wait_req = true;
    }
    s_wait_seq = s_mode_seq;
    dbg_event("ui", "wait", "\"sid\":%u,\"mode\":\"%s\"", (unsigned)sid, nav_mode_name(s_mode));
}

void ui_offer_attention(uint32_t sid, bool error, uint32_t rgb)
{
    if (takeover_blocked(sid)) {
        ui_wait_attention(sid, error, rgb);
    } else if ((s_mode == MODE_HOME || s_mode == MODE_WATCH) &&
               (!quiet_for_takeover() || input_down())) {
        /* the user is at the deck or WATCH: it opens once they let go of it */
        ui_wait_attention(sid, error, rgb);
        s_wait_quiet = true;
    } else {
        take_over(sid, error);
    }
}

void ui_restore_attention(uint32_t sid, uint32_t rgb)
{
    if (takeover_blocked(sid)) {
        ui_wait_attention(sid, false, rgb);
    } else {
        take_over(sid, false);
    }
}

void ui_resent_attention(uint32_t sid, uint32_t rid, uint32_t rgb)
{
    /* ASK was open on it when the link dropped the cards, and the user has not moved since: it
     * comes back as it was, without an alert */
    if (!rid || rid != s_ask_lost_rid || s_mode_seq != s_ask_lost_seq || s_mode != MODE_HOME) {
        return;
    }
    s_ask_lost_rid = 0;
    dbg_event("ui", "ask_restored", "\"rid\":%u", (unsigned)rid);
    ui_offer_attention(sid, false, rgb);
}

void ui_reveal_attention(void)
{
    const attention_t *next = s_wait_req ? queue_next() : NULL;
    if (next && !takeover_blocked(next->sid)) {
        take_over(next->sid, false);
    } else if (!next && s_wait_err && !takeover_blocked(s_wait_err)) {
        take_over(s_wait_err, true);
    }
}

/* Unanswered requests other than the one ASK shows: how many wait. */
static int waiting_count(void)
{
    const attention_t *shown = s_mode == MODE_ASK ? ui_ask_request() : NULL;
    return model_attention_pending(s_now_ms, shown ? shown->rid : 0);
}

static void anim_scale(void *o, int32_t v) { lv_obj_set_style_transform_scale(o, v, 0); }

/* It enters with one pulse, 1.4 x to 1 x, then rests. */
static void badge_pop(void)
{
    lv_anim_delete(s_badge, anim_scale);
    lv_obj_set_style_transform_scale(s_badge, 256, 0);
    if (!ui_animations_allowed()) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_badge);
    lv_anim_set_exec_cb(&a, anim_scale);
    lv_anim_set_values(&a, BADGE_POP, 256);
    lv_anim_set_duration(&a, UI_MOTION_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* Change-only: the colour of what opens next, and how many wait (one: its kind's glyph). */
static void badge_show(bool on, uint32_t rgb, int n, const char *icon)
{
    bool shown = !lv_obj_has_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    if (!on) {
        if (shown) lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (rgb != s_badge_rgb) {
        s_badge_rgb = rgb;
        lv_obj_set_style_bg_color(s_badge, lv_color_hex(rgb), 0);
    }
    if (n >= 2) icon = "";
    if (n != s_badge_n) {
        s_badge_n = n;
        char digits[4] = "";
        if (n >= 2) snprintf(digits, sizeof digits, "%d", n > 9 ? 9 : n);
        lv_label_set_text(s_badge_num, digits);
    }
    if (icon != s_badge_icon) {
        s_badge_icon = icon;
        ui_glyph_set(s_badge_glyph, icon, COL_ON_ACCENT);
    }
    if (!shown) {
        lv_obj_remove_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
        badge_pop();
    }
}

/* Per step: drop what no longer waits, open the queue's head on the way back to the deck or WATCH
 * (once the user has left them alone), show the badge elsewhere. */
static void wait_step(void)
{
    settle_step();
    if (s_wait_err && (!model_find(s_wait_err) || ui_asking(s_wait_err))) { s_wait_err = 0; }
    const attention_t *next = s_wait_req ? queue_next() : NULL;
    if (!next) { s_wait_req = false; /* answered, gone, gone back from, or open in ASK */ }
    if (!next && !s_wait_err) {
        s_wait_quiet = false;
        s_slide = false;
        badge_show(false, 0, 0, "");
        return;
    }
    uint32_t sid = next ? next->sid : s_wait_err;
    if ((s_mode_seq != s_wait_seq || s_wait_quiet) && !input_down() &&
        (s_mode == MODE_TOY || ((s_mode == MODE_HOME || s_mode == MODE_WATCH) &&
                                (quiet_for_takeover() || slide_pending())))) {
        if (next) { dbg_event("ui", "queue_next", "\"rid\":%u", (unsigned)next->rid); }
        take_over(sid, !next);
        return;
    }
    s_report_sid = sid;
    s_report_n = waiting_count();
    uint32_t rgb = !next ? RGB_ERROR : next->kind == ATT_QUESTION ? RGB_ASK : RGB_PERMISSION;
    const char *icon = !next                        ? ICON_ERROR
                       : next->kind == ATT_QUESTION ? ICON_QUESTION
                                                    : ICON_PERMISSION;
    badge_show(s_mode != MODE_HOME, rgb, s_report_n, icon);
}

/* The badge's touch target; not in KEYS or TRACKPAD, where the glass is the stroke recorder or the
 * pointer. */
static bool badge_hit(int x, int y)
{
    if (lv_obj_has_flag(s_badge, LV_OBJ_FLAG_HIDDEN)) return false;
    if (s_mode == MODE_KEYS || s_mode == MODE_TRACKPAD) return false;
    return x >= UI_CX - BADGE_HIT_W / 2 && x < UI_CX + BADGE_HIT_W / 2 && y >= 0 && y < BADGE_HIT_H;
}

/* A tap on the badge goes to what it stands for: the queue's head on its session in ASK, or the
 * error's card. Only navigation: nothing is answered. */
static void badge_go(void)
{
    const attention_t *next = queue_next();
    uint32_t err = s_wait_err;
    dbg_event("ui", "badge_tap", "\"rid\":%u", next ? (unsigned)next->rid : 0u);
    if (next) {
        ui_show_attention(next->sid);
    } else if (err) {
        s_wait_err = 0;
        ui_show_session(err);
    }
    g_ui_actions->tick();
}

void ui_report_add(ui_report_t *r, const char *fmt, ...)
{
    if (r->n_visible >= 8) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->visible[r->n_visible], sizeof r->visible[0], fmt, ap);
    va_end(ap);
    model_utf8_drop_partial(r->visible[r->n_visible++]); /* a cut label: no stray lead byte */
}

void ui_report(ui_report_t *r)
{
    memset(r, 0, sizeof *r);
    r->power = "active";
    if (ui_ota_visible()) { /* the Updating overlay covers everything */
        ui_ota_report(r);
        return;
    }
    ui_burst_report(r);
    if (ui_glow_active()) {
        ui_report_add(r, "Glow");
        if (s_glow_name[0]) ui_report_add(r, "%s", s_glow_name);
        if (s_glow_kind[0]) ui_report_add(r, "%s", s_glow_kind);
    }
    if (!lv_obj_has_flag(s_toast, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_toast_txt);
    if (!lv_obj_has_flag(s_badge, LV_OBJ_FLAG_HIDDEN)) {
        ui_report_add(r, "waiting %u", (unsigned)s_report_sid);
        if (s_report_n > 0) {
            ui_report_add(r, "%d waiting", s_report_n);
        } else {
            ui_report_add(r, "error waiting");
        }
    }
    if (s_mode == MODE_TOY) {
        ui_toy_report(r);
        return;
    }
    if (s_mode == MODE_AGENT) ui_agent_report(r);
    else if (s_mode == MODE_ASK) ui_ask_report(r);
    else if (s_mode == MODE_SCROLL) ui_scroll_report(r);
    else if (s_mode == MODE_STATUS) ui_status_report(r);
    else if (s_mode == MODE_WATCH) ui_watch_report(r);
    else if (s_mode == MODE_VOICE) ui_voice_report(r);
    else if (s_mode == MODE_KEYS) ui_keys_report(r);
    else if (s_mode == MODE_READ) ui_read_report(r);
    else if (s_mode == MODE_GRAPH) ui_graph_report(r);
    else if (s_mode == MODE_LABS) ui_labs_report(r);
    else if (s_mode == MODE_TRACKPAD) ui_trackpad_report(r);
    else ui_home_report(r);
}

/* ───────── toast & glow ───────── */

static void anim_translate_y(void *o, int32_t v) { lv_obj_set_style_translate_y(o, v, 0); }

static void rise(lv_obj_t *o, int px)
{
    lv_anim_delete(o, anim_translate_y);
    if (!ui_animations_allowed()) {
        lv_obj_set_style_translate_y(o, 0, 0);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, anim_translate_y);
    lv_anim_set_values(&a, px, 0);
    lv_anim_set_duration(&a, UI_MOTION_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void ui_motion_enter(lv_obj_t *content) { rise(content, UI_MOTION_RISE); }

/* The screens whose own rows and discs lie in the bottom slot, under the sign: HOME (its
 * summary's second row, the graph strip, the book disc), AGENT (the queued message; Dictate and
 * Keyboard), READ (the page dots), STATUS (the About disc), LABS and TRACKPAD (their own sign for
 * why the trackpad cannot start or work). A rebuilt screen that puts content there adds its
 * mode. */
static bool toast_blanks_slot(void)
{
    return s_mode == MODE_HOME || s_mode == MODE_AGENT || s_mode == MODE_READ ||
           s_mode == MODE_STATUS || s_mode == MODE_LABS || s_mode == MODE_TRACKPAD;
}

/* A toast is a sign (GLYPHS.md rule 8): the glyphs the vocabulary has for its words, in its colour.
 * The words stay what the callers pass and what the report and the log say. Words without a glyph
 * yet draw the info glyph: a toast never spells them out. Only content is text: what was sent
 * (`Sent "..."`), a short number the vocabulary puts after the glyph ("Snoozed 30 min": the moon
 * and 30). */
static void toast_show(const char *words, const char *icons, const char *content, uint32_t rgb)
{
    snprintf(s_toast_txt, sizeof s_toast_txt, "%s", words);
    ui_sign_set(s_toast, icons, content, rgb ? rgb : RGB_IDLE);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 2; i++) {
        if (toast_blanks_slot()) lv_obj_remove_flag(s_toast_mask[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_toast_mask[i], LV_OBJ_FLAG_HIDDEN);
    }
    rise(s_toast, UI_TOAST_RISE);
    s_toast_until_ms = s_now_ms + TOAST_MS;
    dbg_event("ui", "toast", "\"text\":%s", dbg_q(words));
}

void ui_toast_sign(const char *words, const char *icons, const char *content, uint32_t rgb)
{ toast_show(words, icons, content ? content : "", rgb); }

void ui_toast(const char *text, uint32_t rgb)
{
    char icons[16], content[64] = "";
    const char *g = theme_glyph(text);
    if (!g && !strncmp(text, "Sent \"", 6)) {
        g = ICON_SENT;
        snprintf(content, sizeof content, "%s", text + 5);
    }
    if (!g) g = ICON_INFO;
    size_t n = 0;
    /* the icons: 3 bytes each */
    while ((uint8_t)g[n] == 0xEE && g[n + 1] && g[n + 2] && n + 3 < sizeof icons) n += 3;
    memcpy(icons, g, n);
    icons[n] = 0;
    if (!content[0]) snprintf(content, sizeof content, "%s", g[n] == ' ' ? g + n + 1 : g + n);
    toast_show(text, icons, content, rgb);
}

/* The kind in words, for the report (GLYPHS.md rule 9): the glass shows its glyph. */
static const char *glow_kind_words(const char *icon)
{
    if (!strcmp(icon, ICON_PERMISSION)) return "Needs approval";
    if (!strcmp(icon, ICON_QUESTION)) return "Question";
    if (!strcmp(icon, ICON_ERROR)) return "Error";
    if (!strcmp(icon, ICON_DONE)) return "Done";
    return "";
}

void ui_glow(uint32_t rgb, uint32_t sid, const char *icon)
{
    lv_color_t dim = lv_color_mix(lv_color_hex(rgb), COL_BG, 140); /* dim: ~55 % */
    ring_set_solid(s_glow_ring, dim);
    const session_t *s = model_find(sid);
    const char *base = s ? theme_basename(s->repo) : "";
    ui_sigil_set_session(s_glow_sigil, base, sid);
    if (s) {
        lv_obj_remove_flag(s_glow_sigil, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_glow_sigil, LV_OBJ_FLAG_HIDDEN);
    }
    ui_glyph_set(s_glow_hero, icon ? icon : "", dim);
    snprintf(s_glow_name, sizeof s_glow_name, "%s", s ? (base[0] ? base : "(no folder)") : "");
    s_glow_kind = glow_kind_words(icon ? icon : "");
    lv_obj_remove_flag(s_glow, LV_OBJ_FLAG_HIDDEN);
    dbg_event("ui", "glow", "\"rgb\":%u,\"sid\":%u", (unsigned)rgb, (unsigned)sid);
}

void ui_glow_hide(void) { lv_obj_add_flag(s_glow, LV_OBJ_FLAG_HIDDEN); }
bool ui_glow_active(void) { return !lv_obj_has_flag(s_glow, LV_OBJ_FLAG_HIDDEN); }

/* ───────── per-step ───────── */

/* The session a screen was opened on ended under it and the screen closed to the deck: say so, as
 * WATCH does, instead of dropping there silently (issue 20261122-9df4, UX-PROFILE E7). */
static void say_session_ended(nav_mode_t was, uint32_t sid)
{
    bool on_session = was == MODE_AGENT || was == MODE_ASK || was == MODE_SCROLL ||
                      was == MODE_VOICE || was == MODE_KEYS || was == MODE_READ ||
                      was == MODE_GRAPH;
    if (!on_session || s_mode != MODE_HOME || !sid) { return; }
    const session_t *s = model_find(sid);
    if (!s || s->state == SESS_ENDED) { ui_toast("Session ended", RGB_IDLE); }
}

/* ASK closed itself: back to the deck or WATCH, and the queue's next slides in from there. */
static void ask_done(void)
{
    leave_mode();
    s_slide = true;
    s_slide_seq = s_mode_seq;
    s_wait_req = true; /* every pending request is in the queue, also one that never waited */
}

/* The model changed: every open screen refreshes, and one whose subject is gone closes. */
static void refresh_screens(uint32_t now_ms)
{
    nav_mode_t was = s_mode;
    uint32_t was_sid = s_mode_sid; /* the deck may move off a session that left */
    ui_home_refresh(now_ms);
    if (s_mode == MODE_AGENT && !ui_agent_refresh(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_ASK && !ui_ask_refresh(now_ms)) ask_done();
    /* the session is gone */
    if (s_mode == MODE_SCROLL && !ui_scroll_refresh(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_STATUS) ui_status_refresh(now_ms);
    if (s_mode == MODE_VOICE && !ui_voice_refresh(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_KEYS && !ui_keys_refresh(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_READ && !ui_read_refresh(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_GRAPH && !ui_graph_refresh(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_LABS) ui_labs_refresh(now_ms);
    if (s_mode == MODE_TRACKPAD) ui_trackpad_refresh(now_ms);
    if (s_mode == MODE_WATCH && !ui_watch_refresh(now_ms)) {
        enter_mode(MODE_HOME);
        ui_toast("Session ended", RGB_IDLE);
    }
    say_session_ended(was, was_sid);
}

void ui_tick(uint32_t now_ms)
{
    s_now_ms = now_ms;
    if (!lv_obj_has_flag(s_toast, LV_OBJ_FLAG_HIDDEN) &&
        (int32_t)(now_ms - s_toast_until_ms) >= 0) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 2; i++) lv_obj_add_flag(s_toast_mask[i], LV_OBJ_FLAG_HIDDEN);
    }
    ui_burst_tick(now_ms);
    ui_key_hint_tick(now_ms);
    pair_tick(now_ms);
    if (model_rev() != s_seen_rev) {
        s_seen_rev = model_rev();
        refresh_screens(now_ms);
    }
    ui_home_tick(now_ms);
    if (s_mode == MODE_AGENT && ui_agent_tick(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_ASK && ui_ask_tick(now_ms)) ask_done();
    if (s_mode == MODE_SCROLL && ui_scroll_tick(now_ms)) leave_mode(); /* idle: back to WATCH */
    if (s_mode == MODE_STATUS && ui_status_tick(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_WATCH) ui_watch_tick(now_ms);
    if (s_mode == MODE_VOICE && ui_voice_tick(now_ms)) { /* sent */
        /* an answer to a request goes back to ASK, which shows it pending until the daemon's
         * clear */
        const attention_t *a = ui_voice_rid() ? model_attention_find(ui_voice_rid()) : NULL;
        uint32_t sid = a ? a->sid : 0;
        enter_mode(MODE_HOME);
        if (sid) {
            ui_home_select_sid(sid, "voice");
            enter_mode(MODE_ASK);
        }
    }
    if (s_mode == MODE_KEYS) ui_keys_tick(now_ms);
    if (s_mode == MODE_READ) ui_read_tick(now_ms);
    if (s_mode == MODE_GRAPH) ui_graph_tick(now_ms);
    if (s_mode == MODE_LABS && ui_labs_tick(now_ms)) enter_mode(MODE_HOME);
    if (s_mode == MODE_TRACKPAD) ui_trackpad_tick(now_ms);
    if (s_mode == MODE_TOY) { ui_toy_tick(now_ms); }
    wait_step(); /* after the screens that closed themselves this step */
    /* the answer window runs on the request it belongs to: on ASK, and on its card on the deck */
    ui_countdown_show(s_mode == MODE_ASK    ? ui_ask_request()
                      : s_mode == MODE_HOME ? ui_home_sel_request()
                                            : NULL,
                      now_ms);
}

/* The interaction layer maps the gesture to an action; mode changes are handled here, everything
 * else goes to the screen that owns the mode. */
/* The key hint of key 0 (A) or 1 (B) on HOME or WATCH; NULL elsewhere. */
static lv_obj_t *key_hint(int key)
{
    if (s_mode == MODE_HOME) return ui_home_key_hint(key);
    if (s_mode == MODE_WATCH) return ui_watch_key_hint(key);
    return NULL;
}

/* The key whose shown hint is under the point (0 A, 1 B), or -1. */
static int key_hint_at(int x, int y)
{
    for (int k = 0; k < 2; k++) {
        if (ui_key_hint_hit(key_hint(k), x, y)) return k;
    }
    return -1;
}

/* A press on HOME or WATCH, by the key or a tap on its hint, lights that hint's ring: it teaches
 * which hint belongs to which key. */
static void key_hint_flash(gesture_kind_t kind, int key)
{
    if (kind == G_BTN_A || kind == G_BTN_A_DOUBLE) {
        key = 0;
    } else if (kind == G_BTN_B) {
        key = 1;
    }
    lv_obj_t *hint = key >= 0 ? key_hint(key) : NULL;
    if (hint && lv_obj_is_visible(hint)) ui_key_hint_flash(hint, s_now_ms);
}

bool ui_on_gesture(const gesture_evt_t *g)
{
    if (g->kind == G_RELEASE) s_rel_y = g->y;
    if (g->kind != G_SHAKE && g->kind != G_LIFT) {
        s_touched = true;
        s_touch_ms = lv_tick_get();
    }
    /* the badge: what waits behind this screen (issue 20261126-0a74) */
    if (g->kind == G_TAP && badge_hit(g->x, g->y)) {
        badge_go();
        return true;
    }
    nav_zone_t zone = nav_centre(g->x, g->y) ? ZONE_CENTRE : ZONE_RIM;
    /* The deck's book disc and graph strip sit in the bottom slot and reach past the centre
     * zone: a tap on one is a tap on it, not on the rim (a drag there still scrubs the
     * sessions). */
    if (zone == ZONE_RIM && s_mode == MODE_HOME && (g->kind == G_TAP || g->kind == G_DOUBLE_TAP) &&
        (ui_home_read_hit(g->x, g->y) || ui_home_strip_hit(g->x, g->y)))
        zone = ZONE_CENTRE;
    /* ASK's Open on PC is button A's hint, and a tap item sits between the hints: both beyond the
     * centre zone */
    if (zone == ZONE_RIM && s_mode == MODE_ASK && (g->kind == G_TAP || g->kind == G_DOUBLE_TAP) &&
        ui_ask_target_hit(g->x, g->y))
        zone = ZONE_CENTRE;
    /* So do the discs of AGENT's orbit and of READ's: a touch on one (a tap, or the press of a
     * hold) is a touch on it; a slide along the bezel still dials. */
    if (zone == ZONE_RIM &&
        (g->kind == G_TAP || g->kind == G_DOUBLE_TAP || g->kind == G_PRESS ||
         g->kind == G_LONG_PRESS) &&
        ((s_mode == MODE_AGENT && ui_agent_hit(g->x, g->y)) ||
         (s_mode == MODE_READ && ui_read_hit(g->x, g->y))))
        zone = ZONE_CENTRE;
    /* HOME and WATCH: a tap on a shown key hint presses its key (issue 20261126-34e5) */
    int key = zone == ZONE_RIM && g->kind == G_TAP ? key_hint_at(g->x, g->y) : -1;
    if (key >= 0) zone = key ? ZONE_KEY_B : ZONE_KEY_A;
    nav_action_t a =
        nav_lookup(s_mode, g->kind, zone, ui_home_request_pending(), ui_home_askable());
    if (a != NAV_NONE) key_hint_flash(g->kind, key);
    /* moves: a stream */
    if (a != NAV_NONE && g->kind != G_PRESS && g->kind != G_RELEASE && g->kind != G_MOVE)
        dbg_event("ui", "nav", "\"mode\":\"%s\",\"gesture\":\"%s\",\"action\":\"%s\"",
                  nav_mode_name(s_mode), gesture_name(g->kind), nav_action_name(a));
    switch (a) {
    case NAV_OPEN_AGENT:
        ui_home_note_input();
        if (!ui_home_sel_sid()) {
            g_ui_actions->reject();
            return true;
        }
        if (g->kind == G_TAP && ui_home_read_hit(g->x, g->y)) {
            /* the card's book disc (LAST-MESSAGE.md) */
            enter_mode(MODE_READ);
            g_ui_actions->tick();
            return true;
        }
        if (g->kind == G_TAP && ui_home_strip_hit(g->x, g->y)) {
            /* the card's graph strip (GRAPH.md §5) */
            enter_mode(MODE_GRAPH);
            g_ui_actions->tick();
            return true;
        }
        enter_mode(MODE_AGENT);
        g_ui_actions->tick();
        return true;
    case NAV_OPEN_ASK: /* shows the options; answering takes a further tap or hold */
        ui_home_note_input();
        if (!ui_home_askable()) {
            g_ui_actions->reject();
            return true;
        }
        open_ask();
        g_ui_actions->tick();
        return true;
    case NAV_OPEN_STATUS:
        ui_home_note_input();
        enter_mode(MODE_STATUS);
        g_ui_actions->tick();
        return true;
    case NAV_OPEN_LABS:
        ui_home_note_input();
        enter_mode(MODE_LABS);
        g_ui_actions->tick();
        return true;
    case NAV_BACK:
        /* too short: ignored */
        if (g->kind == G_SWIPE_DOWN && !nav_long_swipe_down(g->y, s_rel_y)) return true;
        if (s_mode == MODE_ASK && ui_ask_back()) {
            /* the reader closes first; the request stays on screen */
            g_ui_actions->tick();
            return true;
        }
        if (s_mode == MODE_READ && ui_read_back()) { /* the end picker goes back to the last page */
            g_ui_actions->tick();
            return true;
        }
        if (s_mode == MODE_LABS && ui_labs_back()) { /* Personality goes back to the list */
            g_ui_actions->tick();
            return true;
        }
        /* it stopped a glide */
        if (s_mode == MODE_SCROLL && g->kind == G_TAP && ui_scroll_caught()) return true;
        /* a flick that scrolled */
        if (s_mode == MODE_SCROLL && g->kind == G_SWIPE_RIGHT && ui_scroll_dragged()) return true;
        leave_mode();
        g_ui_actions->tick();
        return true;
    case NAV_HOME: enter_mode(MODE_HOME); return true;
    case NAV_SCROLL_DRAG:
        if (s_mode != MODE_WATCH) break; /* SCROLL's own drags go to the screen below */
        {
            /* A drag on WATCH scrolls the watched session's terminal (issue 5cbb): bring it to the
             * front quietly, open SCROLL over WATCH, then this first drag step moves it. */
            uint32_t sid = ui_watch_sid();
            const session_t *s = model_find(sid);
            ui_home_note_input();
            if (!s || s->no_scroll) {
                ui_toast("Not supported", RGB_AMBER);
                g_ui_actions->reject();
                return true;
            }
            g_ui_actions->scroll_open(sid);
            enter_mode(MODE_SCROLL);
            if (s_mode != MODE_SCROLL) return true;
            s_resume_watch_sid = sid;
            return ui_scroll_act(a, g);
        }
    default: break;
    }
    if (s_mode == MODE_AGENT) {
        ui_home_note_input();
        return ui_agent_act(a, g);
    }
    if (s_mode == MODE_ASK) {
        ui_home_note_input();
        return ui_ask_act(a, g);
    }
    if (s_mode == MODE_SCROLL) {
        ui_home_note_input();
        return ui_scroll_act(a, g);
    }
    if (s_mode == MODE_STATUS) {
        ui_home_note_input();
        return ui_status_act(a, g);
    }
    if (s_mode == MODE_VOICE) {
        ui_home_note_input();
        return ui_voice_act(a, g);
    }
    if (s_mode == MODE_KEYS) {
        ui_home_note_input();
        return ui_keys_act(a, g);
    }
    if (s_mode == MODE_READ) {
        ui_home_note_input();
        return ui_read_act(a, g);
    }
    if (s_mode == MODE_WATCH) {
        ui_home_note_input();
        return ui_watch_act(a, g);
    }
    if (s_mode == MODE_GRAPH) {
        ui_home_note_input();
        return ui_graph_act(a, g);
    }
    if (s_mode == MODE_LABS) {
        ui_home_note_input();
        return ui_labs_act(a, g);
    }
    if (s_mode == MODE_TRACKPAD) {
        ui_home_note_input();
        return ui_trackpad_act(a, g);
    }
    if (s_mode == MODE_TOY) {
        ui_home_note_input();
        return ui_toy_act(a, g);
    }
    return ui_home_act(a, g);
}

void ui_note_motion(void) { ui_home_note_input(); }
