/* The VOICE screen (issue 0ea9; looks per UI-V3 §4.7): dictate a reply to one session. Opened by "Dictate"
 * at the end of the Reply list, or on ASK for a request that takes free text (rid != 0: the transcript
 * answers it). Listening (tap or A stops; 15 s at most), Transcribing, then the transcript with a hold
 * button: only the 1.2 s hold sends it (cmd dictate names the recording; the daemon holds the text and
 * types it). A transcript longer than its five-line box is paged (issue c6cf): a tap or A turns the page,
 * and the button reads "More" until the last page has been on screen, so a hold never sends words the user
 * was not shown. Long swipe down or B held discards. voice.c does the recording. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "ui_wheel.h"
#include "voice.h"

#define DISC_Y 186
#define DISC_D 96
#define HALO_R_MIN 48
#define HALO_R_MAX 80 /* the halo's lowest edge (y 266) stays above the timer (top at y 271) */
#define HALO_STEP 4 /* radius quantum: a repaint only when the loudness moves this far */
#define LOW_Y 292   /* timer / "Transcribing…" */
#define TEXT_CY 170
#define TEXT_W_MAX 330 /* as wide as the chord allows, at most this */
#define TEXT_LINES 5
#define ERR_ICON_Y 176
#define REASON_Y 236
#define BUTTON_Y 296

static lv_obj_t *s_root, *s_header, *s_halo, *s_disc, *s_mic, *s_low, *s_box, *s_text, *s_err, *s_reason, *s_btn;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static char s_header_txt[64], s_low_txt[32], s_reason_txt[96], s_word_txt[16];
MODEL_EXT_BSS static char s_text_txt[VOICE_TEXT_MAX];
static uint32_t s_sid, s_rid, s_now, s_text_since;
static bool s_queue; /* the hold queues the dictation ("Queue" instead of "Send") */
static voice_state_t s_shown = (voice_state_t)-1;
static bool s_holding, s_ramp_started;
static bool s_lock_press; /* a press while pages are unread: says so once it has lasted NAV_HOLD_SHOW_MS */
static uint32_t s_hold_t0;
static int s_halo_r;
static int s_page_i, s_pages;
static bool s_read_all; /* the last page has been on screen: everything a hold would send has been shown */

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void hold_progress(int permille)
{
    wheel_set_progress(s_btn, permille);
    ring_set_progress(ui_home_ring(), permille, RGB_ASK);
}

static void hold_cancel(void)
{
    if (!s_holding) return;
    s_holding = false;
    if (s_ramp_started) g_ui_actions->hold_stop();
    s_ramp_started = false;
    hold_progress(0);
}

/* LVGL invalidates on every style write, so the font is written only when it changes. */
static void set_low_font(const lv_font_t *f)
{
    if (lv_obj_get_style_text_font(s_low, 0) == f) return;
    int lh = lv_font_get_line_height(f);
    lv_obj_set_style_text_font(s_low, f, 0);
    lv_obj_set_height(s_low, lh);
    lv_obj_set_y(s_low, LOW_Y - lh / 2);
}

/* Halo radius follows the loudness (0..100), quantised so a quiet room does not repaint. */
static void set_halo(int level)
{
    level = level < 0 ? 0 : level > 100 ? 100 : level;
    int root = 0; /* square-root curve: speech sits at 10..30 of 100 and should still move the halo */
    while ((root + 1) * (root + 1) <= level * 100) root++;
    int r = HALO_R_MIN + (HALO_R_MAX - HALO_R_MIN) * root / 100;
    r = r / HALO_STEP * HALO_STEP;
    if (r == s_halo_r) return;
    s_halo_r = r;
    lv_obj_set_size(s_halo, 2 * r, 2 * r);
    lv_obj_set_pos(s_halo, UI_CX - r, DISC_Y - r);
}

/* The transcript, centred on y 170: FONT_BODY in a box as tall as the text, at most five lines. A longer
 * text is shown five lines at a time (the label slides up inside the clipping box). */
static void set_transcript(const char *t)
{
    if (!strcmp(t, s_text_txt)) return;
    const lv_font_t *f = FONT_BODY;
    int lh = lv_font_get_line_height(f);
    int w = theme_chord(TEXT_CY - TEXT_LINES * lh / 2, 12); /* the narrowest row is the top one */
    if (w > TEXT_W_MAX) w = TEXT_W_MAX;
    lv_point_t sz;
    lv_text_get_size(&sz, t, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    int all = sz.y / lh < 1 ? 1 : sz.y / lh;
    int lines = all > TEXT_LINES ? TEXT_LINES : all;
    s_pages = (all + TEXT_LINES - 1) / TEXT_LINES;
    s_page_i = 0;
    s_read_all = s_pages == 1;
    lv_obj_set_size(s_box, w, lines * lh);
    lv_obj_set_pos(s_box, UI_CX - w / 2, TEXT_CY - lines * lh / 2);
    lv_obj_set_width(s_text, w);
    lv_obj_set_y(s_text, 0);
    theme_set_text(s_text, s_text_txt, sizeof s_text_txt, t);
}

/* "Send" (the hold) once every page has been shown; until then "More 1/3", which a tap answers. */
static void set_button(voice_state_t st)
{
    const wheel_item_t send = {s_queue ? "Queue" : "Send", RGB_ASK, true, 1, NULL};
    static const wheel_item_t retry = {"Try again", 0, false, 2, NULL};
    char more[24];
    wheel_item_t next = {more, 0, false, 3, NULL};
    snprintf(more, sizeof more, "More %d/%d", s_page_i + 1, s_pages);
    wheel_set(s_btn, st != VOICE_TEXT ? &retry : s_read_all ? &send : &next, 1, 0);
    wheel_set_shown(s_btn, true);
}

/* The next page (the first again after the last); the last one on screen unlocks the hold. */
static void turn_page(void)
{
    s_page_i = (s_page_i + 1) % s_pages;
    lv_obj_set_y(s_text, -s_page_i * TEXT_LINES * lv_font_get_line_height(FONT_BODY));
    if (s_page_i == s_pages - 1) s_read_all = true;
    set_button(VOICE_TEXT);
    dbg_event("ui", "voice_page", "\"page\":%d,\"pages\":%d", s_page_i + 1, s_pages);
}

static void render(void)
{
    voice_state_t st = voice_state();
    const session_t *s = model_find(s_sid);
    char buf[64];
    const char *base = s ? theme_basename(s->repo) : "";
    snprintf(buf, sizeof buf, "%s %s", s_queue ? "Queue for" : "Reply to", base[0] ? base : "(no folder)");
    theme_set_text(s_header, s_header_txt, sizeof s_header_txt, buf);
    if (st != s_shown) {
        if (st == VOICE_TEXT) s_text_since = s_now;
        if (st != VOICE_TEXT) hold_cancel();
        s_shown = st;
        bool rec = st == VOICE_REC, wait = st == VOICE_WAIT, text = st == VOICE_TEXT;
        show(s_halo, rec);
        show(s_disc, rec || wait);
        theme_set_bg_color(s_disc, rec ? lv_color_hex(RGB_ASK) : COL_FILL2);
        theme_set_text_color(s_mic, rec ? COL_ON_ACCENT : COL_TEXT);
        show(s_low, rec || wait);
        if (text) { /* before the button, which names the pages */
            s_text_txt[0] = 0; /* a retry that heard the same words starts on page one again */
            set_transcript(voice_text());
        }
        show(s_box, text);
        show(s_err, !rec && !wait && !text);
        show(s_reason, !rec && !wait && !text);
        if (text || st == VOICE_FAILED) set_button(st);
        else wheel_set_shown(s_btn, false);
        if (rec) set_halo(0);
    }
    const char *word = "Stop";
    switch (st) {
    case VOICE_REC: {
        uint32_t ms = voice_elapsed_ms(s_now);
        set_low_font(FONT_TITLE);
        snprintf(buf, sizeof buf, "%u:%02u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60));
        theme_set_text(s_low, s_low_txt, sizeof s_low_txt, buf);
        set_halo(voice_level());
        break;
    }
    case VOICE_WAIT:
        set_low_font(FONT_HEAD);
        theme_set_text(s_low, s_low_txt, sizeof s_low_txt, "Transcribing\xe2\x80\xa6");
        word = ""; /* A does nothing while waiting */
        break;
    case VOICE_TEXT:
        word = s_read_all ? "" : "More";
        break;
    case VOICE_FAILED:
    default:
        theme_set_text(s_reason, s_reason_txt, sizeof s_reason_txt, voice_error()[0] ? voice_error() : "Not sent");
        word = "";
        break;
    }
    /* Stop names A only while listening (the hold button and Try again are touch / hold). */
    show(s_badge[0], word[0] != 0);
    if (word[0] && strcmp(word, s_word_txt)) {
        snprintf(s_word_txt, sizeof s_word_txt, "%s", word);
        theme_key_badge_set_word(s_badge[0], s_word[0], word, UI_BADGE_A_DEG, UI_BADGE_R);
    }
}

void ui_voice_open(uint32_t sid, uint32_t rid, bool queue, uint32_t now)
{
    s_queue = queue;
    s_sid = sid;
    s_rid = rid;
    s_now = now;
    s_shown = (voice_state_t)-1;
    s_halo_r = 0;
    s_text_txt[0] = 0;
    s_page_i = 0;
    s_pages = 1;
    s_read_all = false;
    s_holding = s_ramp_started = s_lock_press = false;
    ring_set_still(ui_home_ring(), true);
    voice_start(sid, now); /* a refusal shows as the failed state with its reason */
    render();
    show(s_root, true);
    dbg_event("ui", "voice", "\"sid\":%u,\"rid\":%u,\"state\":%d", (unsigned)sid, (unsigned)rid, (int)voice_state());
}

uint32_t ui_voice_rid(void) { return s_rid; }

void ui_voice_close(void)
{
    hold_cancel();
    voice_cancel(); /* nothing is ever sent for a recording left here */
    hold_progress(0);
    ring_set_still(ui_home_ring(), !ui_animations_allowed());
    show(s_root, false);
}

bool ui_voice_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = model_find(s_sid);
    if (!s || s->state == SESS_ENDED) return false;
    if (s_rid && !model_attention_find(s_rid)) return false; /* the request was answered elsewhere */
    render();
    return true;
}

bool ui_voice_act(nav_action_t a, const gesture_evt_t *g)
{
    (void)g;
    voice_state_t st = voice_state();
    switch (a) {
    case NAV_COMMIT:
        if (st == VOICE_REC) {
            voice_stop(s_now);
            g_ui_actions->tick();
        } else if (st == VOICE_FAILED) {
            voice_start(s_sid, s_now);
            g_ui_actions->tick();
        } else if (st == VOICE_TEXT && s_pages > 1) {
            hold_cancel();
            turn_page();
            g_ui_actions->tick();
        } else if (st == VOICE_TEXT) {
            ui_toast("Hold to send", RGB_ASK);
            g_ui_actions->reject();
        }
        render();
        return true;
    case NAV_HOLD_PRESS:
        /* only the transcript arms a hold, only once all of it has been shown, and only once it has been
         * readable for a moment */
        if (st == VOICE_TEXT && !s_read_all) {
            s_lock_press = true;
            s_hold_t0 = s_now;
        } else if (st == VOICE_TEXT && s_now - s_text_since >= NAV_CARD_ARM_MS) {
            s_holding = true;
            s_ramp_started = false;
            s_hold_t0 = s_now;
        }
        return true;
    case NAV_HOLD_RELEASE:
        s_lock_press = false;
        hold_cancel();
        return true;
    default: return false;
    }
}

/* True when the screen wants to close (the transcript was sent). */
bool ui_voice_tick(uint32_t now)
{
    s_now = now;
    if (s_lock_press && now - s_hold_t0 >= NAV_HOLD_SHOW_MS) { /* a hold, not a tap: say why it does nothing */
        s_lock_press = false;
        ui_toast("Tap to read the rest", RGB_AMBER);
        g_ui_actions->reject();
        dbg_event("ui", "voice_locked", "\"pages\":%d", s_pages);
    }
    if (s_holding && voice_state() == VOICE_TEXT && s_read_all) {
        uint32_t el = now - s_hold_t0;
        if (el >= NAV_HOLD_SHOW_MS) {
            if (!s_ramp_started) {
                s_ramp_started = true;
                g_ui_actions->hold_start();
            }
            hold_progress((int)(el * 1000 / NAV_HOLD_MS));
        }
        if (el >= NAV_HOLD_MS) {
            s_holding = s_ramp_started = false;
            hold_progress(0);
            g_ui_actions->dictate(s_sid, s_rid, voice_aid(), el, s_queue);
            return true;
        }
    }
    render();
    return false;
}

void ui_voice_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "voice");
    r->has_sel_sid = true;
    r->sel_sid = s_sid;
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    voice_state_t st = voice_state();
    if (s_low_txt[0] && (st == VOICE_REC || st == VOICE_WAIT)) ui_report_add(r, "%s", s_low_txt);
    if (st == VOICE_TEXT) ui_report_add(r, "(transcript)");
    if (st == VOICE_FAILED) ui_report_add(r, "%s", s_reason_txt);
    if (st == VOICE_TEXT && !s_read_all) ui_report_add(r, "More %d/%d", s_page_i + 1, s_pages); /* the button */
    else if (st == VOICE_TEXT || st == VOICE_FAILED) ui_report_add(r, "%s", st == VOICE_TEXT ? (s_queue ? "Queue" : "Send") : "Try again");
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, int cy, int w, int lines)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(f));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, UI_CX - w / 2, cy - lv_font_get_line_height(f) / 2);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *disc(lv_obj_t *parent, int d, lv_color_t col)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, UI_CX - d / 2, DISC_Y - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, col, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *ui_voice_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_header = label(s_root, FONT_CAPTION, COL_TEXT2, UI_Y_CAPTION, 280, 1);
    s_halo = disc(s_root, 2 * HALO_R_MIN, lv_color_hex(RGB_ASK));
    lv_obj_set_style_bg_opa(s_halo, (LV_OPA_COVER / 4), 0);
    s_disc = disc(s_root, DISC_D, lv_color_hex(RGB_ASK));
    s_mic = lv_label_create(s_disc);
    lv_obj_set_style_text_font(s_mic, FONT_ICON_L, 0);
    lv_label_set_text(s_mic, ICON_MIC);
    lv_obj_center(s_mic);
    s_low = label(s_root, FONT_TITLE, COL_TEXT, LOW_Y, 300, 1);
    s_box = lv_obj_create(s_root); /* clips the transcript to one page */
    lv_obj_remove_style_all(s_box);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_box, TEXT_W_MAX, TEXT_LINES * lv_font_get_line_height(FONT_BODY));
    s_text = label(s_box, FONT_BODY, COL_TEXT, 0, TEXT_W_MAX, 1);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(s_text, 0, 0);
    lv_obj_set_height(s_text, LV_SIZE_CONTENT);
    s_err = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_err, FONT_ICON_L, 0);
    lv_obj_set_style_text_color(s_err, lv_color_hex(RGB_ERROR), 0);
    lv_label_set_text(s_err, ICON_ERROR);
    lv_obj_align(s_err, LV_ALIGN_TOP_MID, 0, ERR_ICON_Y - 22);
    s_reason = label(s_root, FONT_HEAD, COL_TEXT, REASON_Y, 320, 2);
    s_btn = wheel_create_at(s_root, WHEEL_LAYOUT_BUTTON_TALL(BUTTON_Y));
    static const char *const words[2] = {"Stop", "Back"}; /* B has no legend here: a short press does nothing */
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    snprintf(s_word_txt, sizeof s_word_txt, "Stop");
    show(s_badge[1], false);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
