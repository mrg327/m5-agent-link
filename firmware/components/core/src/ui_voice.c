/* The VOICE screen (issue 0ea9): dictate a reply to one session. Opened by "Dictate..." at the end of
 * the Reply list, or on ASK for a request that takes free text (rid != 0: the transcript answers it). Listening (tap or A stops; 15 s at most), Transcribing, then the transcript with
 * "hold to send": only the 1.2 s hold sends it (cmd dictate names the recording; the daemon holds
 * the text and types it). Long swipe down or B held discards. voice.c does the recording. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "voice.h"

#define HEADER_Y 80
#define BIG_Y 170
#define TEXT_Y 130
#define TEXT_W 330
#define HINT_Y 364

static lv_obj_t *s_root, *s_header, *s_big, *s_text, *s_hint;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static char s_header_txt[64], s_big_txt[32], s_text_txt[256], s_hint_txt[64], s_word_txt[16];
static uint32_t s_sid, s_rid, s_now, s_text_since;
static voice_state_t s_shown = (voice_state_t)-1;
static bool s_holding, s_ramp_started;
static uint32_t s_hold_t0;

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void hold_cancel(void)
{
    if (!s_holding) return;
    s_holding = false;
    if (s_ramp_started) g_ui_actions->hold_stop();
    s_ramp_started = false;
    ring_set_progress(ui_home_ring(), 0, RGB_ASK);
}

static void render(void)
{
    voice_state_t st = voice_state();
    const session_t *s = model_find(s_sid);
    char buf[64];
    const char *base = s ? theme_basename(s->repo) : "";
    snprintf(buf, sizeof buf, "Dictate%s%s", theme_sep(), base[0] ? base : "(no folder)");
    theme_set_text(s_header, s_header_txt, sizeof s_header_txt, buf);
    if (st != s_shown) {
        if (st == VOICE_TEXT) s_text_since = s_now;
        if (st != VOICE_TEXT) hold_cancel();
        s_shown = st;
    }
    const char *word = "STOP", *hint = "";
    switch (st) {
    case VOICE_REC: {
        uint32_t ms = voice_elapsed_ms(s_now);
        snprintf(buf, sizeof buf, "%u:%02u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60));
        theme_set_text(s_big, s_big_txt, sizeof s_big_txt, buf);
        theme_set_text(s_text, s_text_txt, sizeof s_text_txt, "Listening...");
        hint = "tap to stop";
        if (!s_holding) ring_set_progress(ui_home_ring(), voice_level() * 10, RGB_ASK); /* loudness */
        break;
    }
    case VOICE_WAIT:
        theme_set_text(s_big, s_big_txt, sizeof s_big_txt, "");
        theme_set_text(s_text, s_text_txt, sizeof s_text_txt, "Transcribing...");
        ring_set_progress(ui_home_ring(), 0, RGB_ASK);
        word = "WAIT";
        break;
    case VOICE_TEXT:
        theme_set_text(s_big, s_big_txt, sizeof s_big_txt, "");
        theme_set_text(s_text, s_text_txt, sizeof s_text_txt, voice_text());
        hint = "hold to send";
        word = "HOLD";
        break;
    case VOICE_FAILED:
    default:
        theme_set_text(s_big, s_big_txt, sizeof s_big_txt, "");
        theme_set_text(s_text, s_text_txt, sizeof s_text_txt, voice_error()[0] ? voice_error() : "Not sent");
        ring_set_progress(ui_home_ring(), 0, RGB_ASK);
        hint = "tap to try again";
        word = "RETRY";
        break;
    }
    show(s_big, s_big_txt[0] != 0);
    theme_set_text(s_hint, s_hint_txt, sizeof s_hint_txt, hint);
    if (strcmp(word, s_word_txt)) {
        snprintf(s_word_txt, sizeof s_word_txt, "%s", word);
        theme_key_badge_set_word(s_badge[0], s_word[0], word, UI_BADGE_A_DEG, UI_BADGE_R);
    }
}

void ui_voice_open(uint32_t sid, uint32_t rid, uint32_t now)
{
    s_sid = sid;
    s_rid = rid;
    s_now = now;
    s_shown = (voice_state_t)-1;
    s_holding = s_ramp_started = false;
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
    ring_set_progress(ui_home_ring(), 0, RGB_ASK);
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
        } else if (st == VOICE_TEXT) {
            ui_toast("Hold to send", RGB_ASK);
            g_ui_actions->reject();
        }
        render();
        return true;
    case NAV_HOLD_PRESS:
        /* only the transcript arms a hold, and only once it has been readable for a moment */
        if (st == VOICE_TEXT && s_now - s_text_since >= NAV_CARD_ARM_MS) {
            s_holding = true;
            s_ramp_started = false;
            s_hold_t0 = s_now;
        }
        return true;
    case NAV_HOLD_RELEASE: hold_cancel(); return true;
    default: return false;
    }
}

/* True when the screen wants to close (the transcript was sent). */
bool ui_voice_tick(uint32_t now)
{
    s_now = now;
    if (s_holding && voice_state() == VOICE_TEXT) {
        uint32_t el = now - s_hold_t0;
        if (el >= NAV_HOLD_SHOW_MS) {
            if (!s_ramp_started) {
                s_ramp_started = true;
                g_ui_actions->hold_start();
            }
            ring_set_progress(ui_home_ring(), (int)(el * 1000 / NAV_HOLD_MS), RGB_ASK);
        }
        if (el >= NAV_HOLD_MS) {
            s_holding = s_ramp_started = false;
            ring_set_progress(ui_home_ring(), 0, RGB_ASK);
            g_ui_actions->dictate(s_sid, s_rid, voice_aid(), el);
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
    if (s_big_txt[0]) ui_report_add(r, "%s", s_big_txt);
    if (s_text_txt[0]) ui_report_add(r, "%s", voice_state() == VOICE_TEXT ? "(transcript)" : s_text_txt);
    if (s_hint_txt[0]) ui_report_add(r, "%s", s_hint_txt);
}

lv_obj_t *ui_voice_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_header = theme_label(s_root, 16, COL_MUTED);
    lv_obj_set_size(s_header, 280, lv_font_get_line_height(theme_font(16)));
    lv_obj_set_style_text_align(s_header, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_header, UI_CX - 140, HEADER_Y);
    s_big = theme_label(s_root, 48, COL_TEXT);
    lv_obj_set_size(s_big, 300, lv_font_get_line_height(theme_font(48)));
    lv_obj_set_style_text_align(s_big, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_big, UI_CX - 150, BIG_Y);
    s_text = theme_label(s_root, 20, COL_TEXT);
    lv_obj_set_size(s_text, TEXT_W, 5 * lv_font_get_line_height(theme_font(20)));
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_text, UI_CX - TEXT_W / 2, TEXT_Y);
    s_hint = theme_label(s_root, 16, COL_MUTED);
    lv_obj_set_size(s_hint, 300, lv_font_get_line_height(theme_font(16)));
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_hint, UI_CX - 150, HINT_Y);
    static const char *const words[2] = {"STOP", "BACK"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    snprintf(s_word_txt, sizeof s_word_txt, "STOP");
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
