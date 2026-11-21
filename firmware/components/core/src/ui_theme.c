#include "ui_theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "hal.h"
#include "ui_kit.h"

uint32_t theme_state_rgb(sess_state_t s)
{
    switch (s) {
    case SESS_INPUT: return RGB_ASK;
    case SESS_ERROR: return RGB_ERROR;
    case SESS_DONE: return RGB_DONE;
    case SESS_WORK:
    case SESS_START: return RGB_WORKING;
    default: return RGB_IDLE;
    }
}

uint32_t theme_session_rgb(const session_t *s)
{
    return s->state == SESS_INPUT && s->sub == 0 ? RGB_PERMISSION : theme_state_rgb(s->state);
}

uint32_t theme_lane_rgb(const session_t *s, int lane)
{
    /* violet, blue, lime, reddish purple, teal, tan, light grey */
    static const uint32_t lanes[7] = {0x9b7bff, 0x0072b2, 0x8fd14f, 0xcc79a7, 0x00b5ad, 0xc9a26b, 0xd9d9d9};
    if (lane <= 0) return s ? theme_session_rgb(s) : RGB_IDLE;
    return lanes[(lane - 1) % 7];
}

bool theme_is_job(const session_t *s) { return s->agent == AGENT_SHELL; }

const char *theme_state_word(const session_t *s)
{
    if (theme_is_job(s)) { /* a job runs and ends; it does not work or get done */
        if (s->state == SESS_START || s->state == SESS_WORK) return "Running";
        if (s->state == SESS_DONE) return "Finished";
        if (s->state == SESS_ERROR) return "Failed";
    }
    switch (s->state) {
    case SESS_START: return "Starting";
    case SESS_WORK: return "Working";
    case SESS_INPUT: return s->sub == 1 ? "Has a question" : "Needs approval";
    case SESS_ERROR: return "Error";
    case SESS_DONE: return "Done";
    case SESS_IDLE: return "Idle";
    default: return "Ended";
    }
}

/* Centre glyph per state: a shape beside the word, so state never depends on colour alone. */
const char *theme_state_glyph(const session_t *s)
{
    switch (s->state) {
    case SESS_INPUT: return s->sub == 0 ? ICON_PERMISSION : ICON_QUESTION;
    case SESS_ERROR: return ICON_ERROR;
    case SESS_DONE: return ICON_DONE;
    case SESS_IDLE: return ICON_IDLE;
    default: return ICON_WORKING;
    }
}

/* The agent's mark in the context line (GLYPHS.md §2): the screens no longer spell the agent's name. */
const char *theme_agent_glyph(agent_t a)
{
    switch (a) {
    case AGENT_COPILOT: return ICON_AG_COPILOT;
    case AGENT_CLAUDE: return ICON_AG_CLAUDE;
    case AGENT_CLOUD: return ICON_AG_CLOUD;
    case AGENT_SHELL: return ICON_G_SHELL;
    default: return ICON_G_ROBOT;
    }
}

/* GLYPHS.md §3.8, the vocabulary: the words the watch draws as a glyph. A screen keeps naming its controls and
 * messages in words (the UI report, the debug log and the tests read those); what it draws is theme_glyph() of
 * the word, when there is one. Two glyphs side by side read as one sign ("computer, refused"). */
static const struct { const char *word, *glyph; } kGlyphs[] = {
    /* key hints and discs */
    {"Open", ICON_MORE}, {"Next", ICON_NEXT}, {"Select", ICON_CHECK}, {"Back", ICON_BACK}, {"More", ICON_CARET_DOWN},
    {"Stop", ICON_STOP}, {"Answer", ICON_REPLY}, {"Review", ICON_G_READ}, {"Read", ICON_READ},
    {"Open on PC", ICON_COMPUTER}, {"Send", ICON_SENT}, {"Use", ICON_CHECK}, {"Try again", ICON_RETRY},
    {"Dictate", ICON_MIC}, {"Keyboard", ICON_KEYBOARD}, {"Done", ICON_CHECK}, {"Reply", ICON_REPLY},
    {"Bottom", ICON_LIVE}, {"Page", ICON_DOWN}, {"SEND", ICON_SENT}, {"DEL", ICON_BACKSPACE},
    {"Quiet 30 min", ICON_QUIET}, {"Quiet 1 h", ICON_QUIET}, {"Quiet off", ICON_BELL}, {"Mute", ICON_MUTE},
    {"Unmute", ICON_SOUND}, {"Volume 0", ICON_WAVE}, {"Volume 1", ICON_WAVE}, {"Volume 2", ICON_WAVE},
    {"Volume 3", ICON_WAVE}, {"Volume 4", ICON_WAVE}, {"Volume 5", ICON_WAVE},
    /* VOICE and KEYS: why a dictation failed, why the keyboard was refused */
    {"Nothing heard", ICON_WAVE ICON_X}, {"No microphone", ICON_MIC ICON_REFUSED}, {"No transcript", ICON_COMPUTER ICON_WAIT},
    {"Busy", ICON_KEYBOARD ICON_WAIT}, {"Not kitty", ICON_G_SHELL ICON_REFUSED},
    /* toasts */
    {"Sent", ICON_SENT}, {"Not sent", ICON_SENT ICON_X}, {"Offline", ICON_OFFLINE}, {"Opening on PC...", ICON_COMPUTER},
    {"Hold to stop", ICON_TAP ICON_STOP}, {"Hold to run again", ICON_TAP ICON_RERUN}, /* AGENT: a tap on a hold disc */
    {"Focused", ICON_COMPUTER}, {"Stopping", ICON_STOP}, {"Interrupting", ICON_STOP}, {"Running again", ICON_RERUN},
    {"Unqueued", ICON_QUEUED ICON_X}, {"Will buzz when done", ICON_NOTIFY}, {"Won't notify", ICON_QUIET},
    {"Snoozed 30 min", ICON_IDLE " 30"}, {"Unsnoozed", ICON_UNSNOOZE}, {"Session ended", ICON_IDLE},
    {"Not supported", ICON_REFUSED}, {"Not allowed on the PC", ICON_COMPUTER ICON_REFUSED}, {"Failed", ICON_ERROR},
    {"Denied", ICON_DENIED}, {"Timed out", ICON_WAIT}, {"No window", ICON_WINDOW ICON_X},
    {"Out of date", ICON_COMPUTER ICON_UPDATE}, {"Nothing else needs you", ICON_DONE},
    /* STATUS, LABS, TRACKPAD */
    {"Set the clock first", ICON_CLOCK ICON_X}, {"Update agentlinkd", ICON_COMPUTER ICON_UPDATE},
    /* results (ui_burst) */
    {"Approved", ICON_CHECK}, {"Always allowed", ICON_ALWAYS}, {"On your computer", ICON_COMPUTER},
    {"Waiting for your computer", ICON_COMPUTER}, {"Time ran out", ICON_WAIT},
    /* ASK (ui_ask.c): a tap on a hold item is told "finger, and what the hold does"; a shut grant is told why */
    {"Hold to approve", ICON_TAP ICON_CHECK}, {"Hold to deny", ICON_TAP ICON_X}, {"Hold to always allow", ICON_TAP ICON_ALWAYS},
    {"Hold to choose", ICON_TAP ICON_CHECK}, {"Hold to send", ICON_TAP ICON_SENT},
    {"Hold for the next question", ICON_TAP ICON_NEXT}, {"Read it first", ICON_READ},
    {"Too long to approve here", ICON_COMPUTER ICON_CHECK}, {"Rule too long for the watch", ICON_ALWAYS ICON_REFUSED},
    {"Tick at least one", ICON_G_PLAN},
};

const char *theme_glyph(const char *word)
{
    for (size_t i = 0; i < sizeof kGlyphs / sizeof kGlyphs[0]; i++)
        if (!strcmp(kGlyphs[i].word, word)) return kGlyphs[i].glyph;
    return NULL;
}

bool theme_is_glyph(const char *text) { return (uint8_t)text[0] == 0xEE; } /* the icons' private-use block */

bool theme_all_glyphs(const char *text)
{
    for (const uint8_t *p = (const uint8_t *)text; *p; p += 3)
        if (p[0] != 0xEE || !p[1] || !p[2]) return false;
    return text[0] != 0;
}

const char *theme_basename(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if ((*p == '/' || *p == '\\') && p[1]) base = p + 1;
    return base;
}

const char *theme_agent_name(agent_t a)
{
    switch (a) {
    case AGENT_COPILOT: return "Copilot CLI";
    case AGENT_CLAUDE: return "Claude Code";
    case AGENT_CLOUD: return "Cloud agent";
    case AGENT_SHELL: return "Terminal";
    case AGENT_PI: return "pi";
    default: return "Agent";
    }
}

const lv_font_t *theme_font(int px)
{
    if (px <= 17) return FONT_CAPTION;
    if (px <= 21) return FONT_BODY;
    if (px <= 28) return FONT_HEAD;
    if (px <= 40) return FONT_TITLE;
    return FONT_NUM;
}

lv_color_t theme_dim(uint32_t rgb, int pct)
{
    return lv_color_mix(lv_color_hex(rgb), lv_color_black(), (uint8_t)(255 * pct / 100));
}

lv_obj_t *theme_label(lv_obj_t *parent, int px, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, theme_font(px), 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

static bool local_is(lv_obj_t *o, lv_style_prop_t prop, lv_style_value_t *v)
{
    return lv_obj_get_local_style_prop(o, prop, v, LV_PART_MAIN) == LV_STYLE_RES_FOUND;
}

void theme_set_text_color(lv_obj_t *o, lv_color_t c)
{
    lv_style_value_t v;
    if (local_is(o, LV_STYLE_TEXT_COLOR, &v) && lv_color_eq(v.color, c)) return;
    lv_obj_set_style_text_color(o, c, 0);
}

void theme_set_bg_color(lv_obj_t *o, lv_color_t c)
{
    lv_style_value_t v;
    if (local_is(o, LV_STYLE_BG_COLOR, &v) && lv_color_eq(v.color, c)) return;
    lv_obj_set_style_bg_color(o, c, 0);
}

void theme_set_opa(lv_obj_t *o, lv_opa_t opa)
{
    lv_style_value_t v;
    if (local_is(o, LV_STYLE_OPA, &v) && v.num == opa) return;
    lv_obj_set_style_opa(o, opa, 0);
}

void theme_align(lv_obj_t *o, lv_align_t align, int32_t x, int32_t y)
{
    lv_style_value_t a, vx, vy;
    if (local_is(o, LV_STYLE_ALIGN, &a) && a.num == (int32_t)align && local_is(o, LV_STYLE_X, &vx) && vx.num == x &&
        local_is(o, LV_STYLE_Y, &vy) && vy.num == y)
        return;
    lv_obj_align(o, align, x, y);
}

void theme_set_text(lv_obj_t *l, char *cache, size_t cap, const char *t)
{
    size_t n = strlen(t);
    if (n < cap) {
        if (!strcmp(cache, t)) return;
        memcpy(cache, t, n + 1);
    } else {
        uint32_t h = 2166136261u; /* FNV-1a */
        for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)t[i]) * 16777619u;
        char key[16];
        snprintf(key, sizeof key, "\x01%08x%05x", (unsigned)h, (unsigned)(n & 0xfffff)); /* no text starts with 0x01 */
        if (!strcmp(cache, key)) return;
        memcpy(cache, key, sizeof key);
    }
    lv_label_set_text(l, t);
}

int theme_chord(int y, int margin)
{
    float dy = (float)(y - 233);
    float r2 = 233.0f * 233.0f - dy * dy;
    int w = r2 > 0 ? (int)(2.0f * sqrtf(r2)) - 2 * margin : 0;
    return w > 0 ? w : 0;
}

/* Figtree has U+00B7; the fallbacks keep a font without it (a test build, a new face) readable. */
const char *theme_sep(void)
{
    static const char *sep;
    if (sep) return sep;
    lv_font_glyph_dsc_t dsc;
    if (lv_font_get_glyph_dsc(theme_font(20), &dsc, 0xB7, 0)) sep = " \xC2\xB7 ";
    else if (lv_font_get_glyph_dsc(theme_font(20), &dsc, 0x2022, 0)) sep = " \xE2\x80\xA2 ";
    else sep = " - ";
    return sep;
}

/* The daemon's separator is U+00B7, which the built-in font may lack: show the theme's separator. */
void theme_display_text(char *dst, size_t cap, const char *text)
{
    const char *sep = strstr(text, " \xC2\xB7 ");
    if (sep) snprintf(dst, cap, "%.*s%s%s", (int)(sep - text), text, theme_sep(), sep + 4);
    else snprintf(dst, cap, "%s", text);
}

void theme_host_prefix(char *dst, size_t cap, const session_t *s)
{
    if (!s->host) dst[0] = 0;
    else snprintf(dst, cap, "%s%s", s->hn[0] ? s->hn : "Remote", theme_sep());
}

void theme_elapsed(char *dst, size_t cap, uint32_t secs)
{
    if (secs < 60) snprintf(dst, cap, "%us", (unsigned)secs);
    else if (secs < 3600) snprintf(dst, cap, "%um", (unsigned)(secs / 60));
    else snprintf(dst, cap, "%uh%02um", (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60));
}

uint32_t theme_now_unix(void) { return app_now_unix(); }

void theme_elapsed_long(char *dst, size_t cap, uint32_t secs)
{
    if (secs < 60) snprintf(dst, cap, "%us", (unsigned)secs);
    else if (secs < 3600) snprintf(dst, cap, "%um %02us", (unsigned)(secs / 60), (unsigned)(secs % 60));
    else snprintf(dst, cap, "%uh %02um", (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60));
}

/* The old legend calls, drawing the Sigil key hints (GLYPHS.md §3.6): see ui_theme.h. */
static const char *hint_glyph(const char *word)
{
    if (!word || !word[0]) return "";
    if (theme_is_glyph(word)) return word;
    const char *g = theme_glyph(word);
    return g ? g : ICON_G_DOT; /* a word without a glyph yet: a dot, never letters (rule 6) */
}

lv_obj_t *theme_key_badge(lv_obj_t *parent, const char *key, const char *word, bool key_left, int deg, int r,
                          lv_obj_t **key_out, lv_obj_t **word_out)
{
    (void)key;
    (void)deg;
    (void)r;
    lv_obj_t *b = ui_key_hint_create(parent, key_left ? UI_KEY_A : UI_KEY_B);
    ui_key_hint_set(b, hint_glyph(word));
    lv_obj_t *dummy = lv_label_create(b); /* what the old callers keep as "the key label" and "the word label" */
    lv_label_set_text(dummy, "");
    lv_obj_add_flag(dummy, LV_OBJ_FLAG_HIDDEN);
    if (key_out) *key_out = dummy;
    if (word_out) *word_out = dummy;
    return b;
}

void theme_key_badge_set_word(lv_obj_t *b, lv_obj_t *word_lbl, const char *word, int deg, int r)
{
    (void)word_lbl;
    (void)deg;
    (void)r;
    ui_key_hint_set(b, hint_glyph(word));
}

static bool job_running(const session_t *s) { return s->state == SESS_START || s->state == SESS_WORK; }

void theme_job_time(char *dst, size_t cap, const session_t *s, uint32_t now)
{
    char t[24];
    dst[0] = 0;
    if (!s->t0) return;
    if (job_running(s)) {
        if (now < s->t0) return;
        uint32_t el = now - s->t0;
        if (s->eta && el < s->eta) { /* rounded up, in calm steps: 10 s under a minute, whole minutes above */
            uint32_t left = s->eta - el;
            left = left < 60 ? (left + 9) / 10 * 10 : (left + 59) / 60 * 60;
            theme_elapsed(t, sizeof t, left);
            snprintf(dst, cap, "about %s left", t);
        } else {
            theme_elapsed_long(dst, cap, el);
        }
        return;
    }
    if (s->state != SESS_DONE && s->state != SESS_ERROR) return;
    theme_elapsed_long(t, sizeof t, s->tst >= s->t0 ? s->tst - s->t0 : 0);
    if (s->state == SESS_DONE) snprintf(dst, cap, "in %s", t);
    else snprintf(dst, cap, "exit %u%s%s", (unsigned)s->exit_code, theme_sep(), t);
}

int theme_job_progress(const session_t *s, uint32_t now)
{
    if (!theme_is_job(s) || !job_running(s) || !s->eta || !s->t0 || now < s->t0) return -1;
    uint32_t el = now - s->t0;
    return el >= s->eta ? 1000 : (int)(el * 1000u / s->eta);
}
