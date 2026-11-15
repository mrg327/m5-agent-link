/* The VOICE screen in Sigil (docs/design/GLYPHS.md §5; issue 0ea9 for what it does): dictate a reply to one
 * session. Opened by Dictate at the end of the Reply list, or on ASK for a request that takes free text
 * (rid != 0: the transcript answers it). voice.c does the recording.
 *
 * What is on the glass, top to bottom:
 *   - who hears it: the reply glyph beside the project's sigil (the question glyph when the words answer a
 *     question of a request);
 *   - listening: the microphone as the hero glyph, inside a ring that drains with the time left (60 s), with
 *     the loudness as waves leaving it left and right. A tap or A stops it;
 *   - transcribing: the microphone goes grey and one wave walks inwards;
 *   - the transcript (text: rule 1), five rows at a time, page pips under it, and one disc below: grey with
 *     the caret while pages are unread (a tap or A turns the page), then the primary disc in yellow with the
 *     paper plane (the tick when the words are one answer of several) inside the ring a hold fills. Only the
 *     1.2 s hold sends, and only once every page has been on screen and the text has been readable for a
 *     moment, so a hold never sends words the user was not shown;
 *   - failed: the reason as a sign of one or two glyphs, and the retry disc (a tap or A).
 * A tap where a hold is needed, or a hold while pages are unread, is answered on the control itself: the hold
 * ring lights up, or the caret disc does. Long swipe down, swipe right or B held discards.
 * The report names all of it in the words the screen used to show (rule 9). */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "voice.h"

/* Geometry: centres. The content circle is r 205 about (233, 233). */
#define HEAD_Y 50
#define HEAD_SIGIL_R 22
#define HEAD_GLYPH_X (UI_CX - 28) /* a 28 px glyph, a 10 px gap, the sigil: centred as a pair */
#define HEAD_SIGIL_X (UI_CX + 19)
#define HERO_Y 228        /* the microphone, its time ring and its waves share this centre */
#define TIME_R 84         /* the ring that drains while recording */
#define TIME_W 4
#define WAVE_R0 104       /* the loudness: four pairs of arcs, the outermost at r 155 */
#define WAVE_PITCH 17
#define WAVE_N 4
#define WAVE_STEP_MS 170  /* transcribing: the lit wave moves in one step per this */
#define TEXT_CY 166
#define TEXT_W_MAX 330    /* as wide as the chord allows, at most this */
#define TEXT_LINES 5
#define PIPS_Y 236
#define DISC_Y 338        /* the send disc: with its hold ring it ends at r 197 */
#define HOLD_RING (UI_DISC_R_PRIMARY + 11) /* the centre line of the disc's hold ring (ui_kit.c: 8 px out, 6 wide) */
#define SIGN_Y 172        /* failed: the reason */
#define RETRY_Y 312
#define NUDGE_MS 1800     /* how long a control stays lit after the wrong gesture on it */

typedef enum { NUDGE_NONE, NUDGE_HOLD, NUDGE_READ } nudge_t;

static lv_obj_t *s_root, *s_head, *s_sigil, *s_time, *s_waves, *s_hero, *s_box, *s_text, *s_pips, *s_track, *s_disc;
static lv_obj_t *s_sign, *s_sign_big, *s_key_a;
static char s_header_txt[64], s_low_txt[32], s_reason_txt[96];
MODEL_EXT_BSS static char s_text_txt[VOICE_TEXT_MAX];
static uint32_t s_sid, s_rid, s_now, s_text_since, s_wait_since, s_nudge_until;
static bool s_queue; /* the hold sends the dictation with q (a working session: typed mid-turn, PROTOCOL §6d) */
static int s_qi = -1;  /* >= 0: a free answer to that question of request s_rid (proto 0.18); the hold hands it to ASK */
static bool s_qlast;   /* ... and it is the request's last question: the hold sends every answer ("Send", else "Use") */
static voice_state_t s_shown = (voice_state_t)-1;
static bool s_holding, s_ramp_started;
static bool s_lock_press; /* a press while pages are unread: says so once it has lasted NAV_HOLD_SHOW_MS */
static uint32_t s_hold_t0;
static int s_page_i, s_pages;
static bool s_read_all; /* the last page has been on screen: everything a hold would send has been shown */
static nudge_t s_nudge;

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* The hold fills the ring round the disc and, because the finger covers the disc, the bezel ring too. */
static void hold_progress(int permille)
{
    ui_disc_set_hold(s_disc, permille);
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

/* "Use": the words are one answer of several, handed to ASK; "Send": the hold sends. */
static bool is_use(void) { return !s_queue && s_qi >= 0 && !s_qlast; }
static const char *nudge_words(void) { return s_nudge == NUDGE_HOLD ? "Hold to send" : "Tap to read the rest"; }

/* The loudness (0..100) as lit waves, counted from the microphone. A square-root curve: speech sits at 10..30
 * of 100 and should still light two or three of the four. A quiet room lights none, so nothing repaints. */
static void set_level(int level)
{
    level = level < 0 ? 0 : level > 100 ? 100 : level;
    int root = 0;
    while ((root + 1) * (root + 1) <= level * 100) root++;
    int lit = (root + 10) / 20;
    if (lit > WAVE_N) lit = WAVE_N;
    ui_waves_set(s_waves, (1u << lit) - 1, lv_color_hex(RGB_ASK), COL_FILL2);
}

/* Transcribing: what was heard travels into the microphone, one wave at a time, then a beat of nothing. */
static void set_pulse(void)
{
    unsigned mask = 1; /* animations off: the innermost wave, still */
    if (ui_animations_allowed()) {
        int step = (int)((s_now - s_wait_since) / WAVE_STEP_MS % (WAVE_N + 1));
        mask = step < WAVE_N ? 1u << (WAVE_N - 1 - step) : 0;
    }
    ui_waves_set(s_waves, mask, lv_color_hex(RGB_ASK), COL_FILL1);
}

/* The transcript, centred on TEXT_CY: FONT_BODY in a box as tall as the text, at most five lines. A longer
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

/* The one disc under the transcript, or under the reason. While pages are unread it is the small grey caret
 * ("More 1/3", which a tap answers); once every page has been shown it grows into the primary disc, in
 * yellow, inside the ring its hold fills ("Send" / "Use"). After a failure it is the retry ("Try again"). */
static void set_disc(voice_state_t st)
{
    bool text = st == VOICE_TEXT, send = text && s_read_all;
    ui_disc_set_pos(s_disc, UI_CX, text ? DISC_Y : RETRY_Y);
    ui_disc_set_r(s_disc, send ? UI_DISC_R_PRIMARY : UI_DISC_R_SECONDARY);
    ui_disc_set_glyph(s_disc, !text ? ICON_RETRY : !s_read_all ? ICON_CARET_DOWN : is_use() ? ICON_CHECK : ICON_SENT);
    /* a hold while pages are unread lights the caret: this is what to tap */
    ui_disc_set_rgb(s_disc, send || (text && s_nudge == NUDGE_READ) ? RGB_ASK : 0);
    /* the ring to fill: dim until a tap asks for the hold */
    show(s_track, send);
    if (send) ui_arc_set(s_track, 0, COL_TEXT, s_nudge == NUDGE_HOLD ? lv_color_hex(RGB_ASK) : theme_dim(RGB_ASK, 30));
    ui_pips_set(s_pips, text ? s_pages : 1, s_page_i);
}

static void nudge(nudge_t n)
{
    s_nudge = n;
    s_nudge_until = s_now + NUDGE_MS;
    set_disc(voice_state());
    g_ui_actions->reject();
    dbg_event("ui", "voice_nudge", "\"text\":%s", dbg_q(nudge_words()));
}

/* The next page (the first again after the last); the last one on screen unlocks the hold. */
static void turn_page(void)
{
    s_page_i = (s_page_i + 1) % s_pages;
    lv_obj_set_y(s_text, -s_page_i * TEXT_LINES * lv_font_get_line_height(FONT_BODY));
    if (s_page_i == s_pages - 1) s_read_all = true;
    s_nudge = NUDGE_NONE;
    set_disc(VOICE_TEXT);
    dbg_event("ui", "voice_page", "\"page\":%d,\"pages\":%d", s_page_i + 1, s_pages);
}

/* Why it failed, as a sign (rule 8): one glyph as large as a state, or two side by side. */
static void set_reason(const char *why)
{
    if (!strcmp(why, s_reason_txt)) return;
    snprintf(s_reason_txt, sizeof s_reason_txt, "%s", why);
    const char *g = theme_glyph(why);
    if (!g) g = ICON_ERROR;
    bool one = !g[3] && ui_glyph_font(g, 128) == FONT_ICON_HERO;
    ui_glyph_set(s_sign_big, one ? g : "", lv_color_hex(RGB_ERROR));
    ui_glyph_set(s_sign, one ? "" : g, lv_color_hex(RGB_ERROR));
}

static void render(void)
{
    voice_state_t st = voice_state();
    const session_t *s = model_find(s_sid);
    const char *base = s ? theme_basename(s->repo) : "";
    bool answer = !s_queue && s_qi >= 0;
    snprintf(s_header_txt, sizeof s_header_txt, "%s %s", answer ? "Answer for" : "Reply to", base[0] ? base : "(no folder)");
    ui_sigil_set_name(s_sigil, base);
    ui_glyph_set(s_head, answer ? ICON_QUESTION : ICON_REPLY, answer ? lv_color_hex(RGB_ASK) : COL_TEXT2);
    bool rec = st == VOICE_REC, wait = st == VOICE_WAIT, text = st == VOICE_TEXT, failed = !rec && !wait && !text;
    if (st != s_shown) {
        if (text) s_text_since = s_now;
        if (wait) s_wait_since = s_now;
        if (!text) hold_cancel();
        s_shown = st;
        s_nudge = NUDGE_NONE;
        show(s_hero, rec || wait);
        show(s_waves, rec || wait);
        show(s_time, rec);
        ui_glyph_set(s_hero, ICON_MIC, rec ? lv_color_hex(RGB_ASK) : COL_TEXT3);
        if (text) { /* before the disc, which depends on the pages */
            s_text_txt[0] = 0; /* a retry that heard the same words starts on page one again */
            set_transcript(voice_text());
        }
        show(s_box, text);
        show(s_pips, text);
        show(s_sign, failed);
        show(s_sign_big, failed);
        show(s_disc, text || failed);
        if (text || failed) set_disc(st);
        else show(s_track, false);
        if (rec) set_level(0);
    }
    const char *key = ""; /* what A does: stop, the next page, try again; nothing while waiting or once all is read */
    switch (st) {
    case VOICE_REC: {
        uint32_t ms = voice_elapsed_ms(s_now);
        snprintf(s_low_txt, sizeof s_low_txt, "%u:%02u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60));
        int left = ms >= VOICE_MAX_MS ? 0 : (int)((VOICE_MAX_MS - ms) * 1000 / VOICE_MAX_MS);
        ui_arc_set(s_time, left, lv_color_hex(RGB_ASK), COL_FILL2);
        set_level(voice_level());
        key = ICON_STOP;
        break;
    }
    case VOICE_WAIT:
        snprintf(s_low_txt, sizeof s_low_txt, "Transcribing\xe2\x80\xa6");
        set_pulse();
        break;
    case VOICE_TEXT:
        key = s_read_all ? "" : ICON_CARET_DOWN;
        break;
    case VOICE_FAILED:
    default:
        set_reason(voice_error()[0] ? voice_error() : "Not sent");
        key = ICON_RETRY;
        break;
    }
    show(s_key_a, key[0] != 0);
    if (key[0]) ui_key_hint_set(s_key_a, key);
}

void ui_voice_open(uint32_t sid, uint32_t rid, bool queue, int qi, bool last, uint32_t now)
{
    s_queue = queue;
    s_qi = rid ? qi : -1;
    s_qlast = last;
    s_sid = sid;
    s_rid = rid;
    s_now = now;
    s_shown = (voice_state_t)-1;
    s_text_txt[0] = s_reason_txt[0] = s_low_txt[0] = 0;
    s_page_i = 0;
    s_pages = 1;
    s_read_all = false;
    s_holding = s_ramp_started = s_lock_press = false;
    s_nudge = NUDGE_NONE;
    ui_disc_set_hold(s_disc, 0);
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
            nudge(NUDGE_HOLD); /* a tap never sends: the ring to fill lights up */
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
    if (s_nudge != NUDGE_NONE && (int32_t)(now - s_nudge_until) >= 0) {
        s_nudge = NUDGE_NONE;
        if (voice_state() == VOICE_TEXT) set_disc(VOICE_TEXT);
    }
    if (s_lock_press && now - s_hold_t0 >= NAV_HOLD_SHOW_MS) { /* a hold, not a tap: say why it does nothing */
        s_lock_press = false;
        nudge(NUDGE_READ);
        dbg_event("ui", "voice_locked", "\"pages\":%d", s_pages);
    }
    if (s_holding && voice_state() == VOICE_TEXT && s_read_all) {
        uint32_t el = now - s_hold_t0;
        if (el >= NAV_HOLD_SHOW_MS) {
            if (!s_ramp_started) {
                s_ramp_started = true;
                g_ui_actions->hold_start();
                if (s_nudge != NUDGE_NONE) { /* the hold it asked for: the ring is the hold's now */
                    s_nudge = NUDGE_NONE;
                    set_disc(VOICE_TEXT);
                }
            }
            hold_progress((int)(el * 1000 / NAV_HOLD_MS));
        }
        if (el >= NAV_HOLD_MS) {
            s_holding = s_ramp_started = false;
            hold_progress(0);
            if (s_qi >= 0) { /* "Other": the recording is this question's answer; ASK sends them all */
                uint32_t aid = voice_aid();
                ui_ask_other_done(s_rid, s_qi, aid, el);
                voice_cancel(); /* done with it on the device; the daemon keeps the transcript for the answer */
                return true;
            }
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
    voice_state_t st = voice_state();
    if (s_nudge != NUDGE_NONE && st == VOICE_TEXT) ui_report_add(r, "%s", nudge_words()); /* the lit control */
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    if (s_low_txt[0] && (st == VOICE_REC || st == VOICE_WAIT)) ui_report_add(r, "%s", s_low_txt);
    if (st == VOICE_TEXT) ui_report_add(r, "(transcript)");
    if (st == VOICE_FAILED) ui_report_add(r, "%s", s_reason_txt);
    if (st == VOICE_TEXT && !s_read_all) ui_report_add(r, "More %d/%d", s_page_i + 1, s_pages); /* the disc */
    else if (st == VOICE_TEXT || st == VOICE_FAILED) ui_report_add(r, "%s", st == VOICE_TEXT ? (is_use() ? "Use" : "Send") : "Try again");
}

lv_obj_t *ui_voice_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* who hears it */
    s_head = ui_glyph_create(s_root, FONT_ICON_M, HEAD_GLYPH_X, HEAD_Y);
    s_sigil = ui_sigil_create(s_root, HEAD_SIGIL_X, HEAD_Y, HEAD_SIGIL_R);

    /* listening and transcribing: the time left, the loudness, the microphone */
    s_time = ui_arc_create(s_root, UI_CX, HERO_Y, TIME_R, TIME_W);
    s_waves = ui_waves_create(s_root, UI_CX, HERO_Y, WAVE_R0, WAVE_PITCH, WAVE_N);
    s_hero = ui_glyph_create(s_root, FONT_ICON_HERO, UI_CX, HERO_Y);

    /* the transcript: a box that clips the text to one page, and which page */
    s_box = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_box);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_box, TEXT_W_MAX, TEXT_LINES * lv_font_get_line_height(FONT_BODY));
    s_text = lv_label_create(s_box);
    lv_obj_set_style_text_font(s_text, FONT_BODY, 0);
    lv_obj_set_style_text_color(s_text, COL_TEXT, 0);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_text, TEXT_W_MAX);
    lv_obj_set_height(s_text, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_text, 0, 0);
    lv_label_set_text(s_text, "");
    s_pips = ui_pips_create(s_root, UI_CX, PIPS_Y);

    /* why it failed */
    s_sign = ui_glyph_create(s_root, FONT_ICON_XL, UI_CX, SIGN_Y);
    s_sign_big = ui_glyph_create(s_root, FONT_ICON_HERO, UI_CX, SIGN_Y);

    /* the disc, and under it the ring its hold fills */
    s_track = ui_arc_create(s_root, UI_CX, DISC_Y, HOLD_RING, 6);
    s_disc = ui_disc_create(s_root, UI_CX, DISC_Y, UI_DISC_R_SECONDARY);

    /* rule 6: what A does. B has no hint here: a short press does nothing (held, it leaves) */
    s_key_a = ui_key_hint_create(s_root, UI_KEY_A);

    lv_obj_t *hide[] = {s_time, s_waves, s_hero, s_box, s_pips, s_sign, s_sign_big, s_track, s_disc, s_key_a, s_root};
    for (size_t i = 0; i < sizeof hide / sizeof hide[0]; i++) lv_obj_add_flag(hide[i], LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
