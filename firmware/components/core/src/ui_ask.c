/* The ASK screen in Sigil (docs/design/GLYPHS.md §5, docs/design/glyphs/ASK.md; the functions and the safety
 * rules are those of UI-V3 §4.3 and §1).
 *
 * Top to bottom: the project's sigil (with the agent's mark beside it), what is asked (the request's title and
 * text, a question, a standing rule: content, so text, rule 1), and under it what can be done, as shapes:
 *   - a request without options of its own (a bare permission) is discs in fixed places: the first item is the
 *     primary disc, the other hold items (Deny, Always allow) stand left of it, a tap item (Dictate) sits between
 *     the key hints, and Open on PC is button A's key hint itself;
 *   - a request with options (the agent's own words: content) is a short list down a spine of discs: the
 *     selected row in the middle with its disc, its neighbours above and below as thin rings.
 * One item is selected (button B, the dial and a drag step through them; a tap on another selects it). Only
 * the selected item is lit in its colour, and only it can be held: its ring's path is drawn round it, and the
 * hold fills that ring. A grant whose text has not been read is dim. Results are one hero glyph; nothing here is
 * a word that is not the agent's or the user's.
 *
 * The item list and the hold rules come from nav.c; this file draws the items and runs the one the user commits.
 * A hold is bound to the request id it started on and cancels when that request changes, clears or is
 * answered. What every glyph means is said in words in the report (rule 9), as before. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "detail.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"

/* ───────── geometry: centres. Content stays inside r 205; a touch is a centre touch inside r 150 ───────── */
#define SIGIL_Y 50 /* as on HOME: the same mark in the same place */
#define SIGIL_R 22
#define MARK_DX 40      /* the marks beside the sigil: remote (left), the agent (right) */
#define BLOCK_Y0 72     /* what is asked: title and text, centred as a block in y 72..176 */
#define BLOCK_H 104
#define TITLE_W 256
#define TEXT_W 292      /* a permission's text: the width it has always been measured at (UI-V3 §4.3a) */
#define TEXT_W_Q 312    /* a question's */
#define TEXT_LINES 3
#define MORE_LINES 2    /* a text that goes on: two lines and the book glyph */
#define MORE_H 30
#define PIPS_Y 404      /* which question of several; which page of the reader */
/* discs (a request without options of its own) */
#define P_X 304 /* the primary disc with hold items beside it: its ring ends where a toast's sign begins */
#define P_Y 262
#define P_R 74
#define SOLO_X UI_CX /* the primary disc alone */
#define LEFT_X 140 /* the other hold items: one, or two stacked */
#define LEFT_Y 262
#define LEFT_R UI_DISC_R_SECONDARY
#define LEFT2_X 146 /* two: smaller, and clear of button A's hint */
#define LEFT2_DY 44
#define LEFT2_R 36
#define DOCK_Y 378 /* tap items, between the key hints */
#define DOCK_R 26
#define DOCK_PITCH 62
/* rows (a request with options): the selected row, and one neighbour above and below */
#define SPINE_X 118 /* the spine's leftmost place: the selected disc stays inside the centre zone */
#define ROW_GAP 26  /* between a disc and its text: clear of the hold ring */
#define ROW_SEL_Y 262
#define ROW_SEL_H 72 /* 7 mm: the selected row is the one that is held */
#define ROW_NB_DY 60
#define ROW_NB_H 42
#define ROW_SEL_R UI_DISC_R_SMALL
#define ROW_NB_R 14
#define ROW_SEL_W 252
#define ROW_NB_W 236
#define ROW_X0 66 /* a row's touch band */
#define ROW_X1 410
/* results */
#define HERO_Y 206 /* as on HOME */
#define RES_TEXT_Y 294
#define RES_MARK_Y 334
#define RES_MARK_Y_ALONE 306
#define RGB_RING 0x6c6c73 /* a neighbour's ring: COL_TEXT3 */

static lv_obj_t *s_root, *s_sigil, *s_mark[2], *s_pips, *s_title, *s_text, *s_more;
static lv_obj_t *s_disc[ASK_MAX_ITEMS], *s_seldisc, *s_nb[2], *s_row[3]; /* rows: [0] above, [1] selected, [2] below */
static lv_obj_t *s_hero, *s_res, *s_resmarks, *s_key[2];
static uint32_t s_sid, s_rid, s_shown_ms, s_now;
static ask_item_t s_items[ASK_MAX_ITEMS];
static int s_n, s_sel, s_listed; /* s_listed: the item count the selection belongs to */
static bool s_rows, s_items_on;
static struct {
    int16_t x, y, r; /* r 0: not a disc of its own (Open on PC is button A's key hint) */
} s_slot[ASK_MAX_ITEMS];
static bool s_holding, s_hold_fired, s_ramp_started;
static uint32_t s_hold_t0, s_hold_rid, s_nudge_ms;
static int s_hold_idx;
/* The words of the screen: the report says them (rule 9); only the request's own are drawn. */
static char s_header_txt[80], s_title_txt[48], s_text_txt[168], s_word_txt[24], s_opt_txt[MODEL_RULE_CAP + 8];
static char s_req_title[48];  /* the request's own title; the rule view's is its scope, in words */
static char s_title_draw[48]; /* the title as drawn: what the agent sent, never a stand-in */
static char s_title_lbl[48], s_res_txt[MODEL_RULE_CAP + 12], s_row_txt[3][64];
static const char *s_after_words = "";
static int s_blk_key, s_title_y, s_title_y_shown, s_text_w, s_row_key, s_resmark_y;
static int s_spine = SPINE_X, s_spine_shown = SPINE_X; /* the rows' discs: the list is centred on what it holds */
static bool s_more_on;
/* Read before you approve (UI-V3 §4.3a). A text that does not fit shows MORE_LINES lines and the book glyph; a
 * tap on it opens the reader, which pages through the whole text. Until its last page has been on screen,
 * nothing that grants permission can be held. A text the daemon cut (proto 0.11: it says so with `len`; older
 * daemons: it ends in an ellipsis or fills the buffer) is not the whole request: the reader fetches the full
 * text by request id (detail.c, ANSWER-WINDOW.md §4) and pages through it as it arrives, and the lock releases
 * only once all of it has arrived and its last page has been shown. A cut text the watch cannot fetch (an older
 * daemon, privacy meta, over DETAIL_CAP, a refused or failed transfer) can be read and denied here but only
 * approved on the PC. */
#define READ_TITLE_Y 74
#define READ_Y 104
#define READ_W 320
#define READ_LINES 7
#define LOAD_Y 290
#define ASK_BLOCK_CUT_TEXT 1 /* 0: a daemon-cut text the watch cannot fetch may be approved once its preview was read */
#define GATE_TELL_MS 400     /* a finger resting this long on a locked Approve gets told why nothing happens */
#define NUDGE_MS 600         /* a tap on a hold item: its ring starts and falls back */
#define NUDGE_PM 110
static lv_obj_t *s_read, *s_read_lbl, *s_load;
static bool s_long, s_cut, s_fetch, s_reading, s_read_ok, s_gate_press;
static int s_page_i, s_pages, s_read_pad;
static uint32_t s_read_ms, s_gate_t0;
static uint32_t s_reader_len; /* bytes of text in the reader now; s_read_len: when its last page was shown */
static uint32_t s_read_len;
static uint32_t s_reader_got; /* detail bytes the reader was last loaded with, and the transfer's state then */
static detail_state_t s_reader_st;
/* The request went back to the computer (window over, or handed back): ASK says so for BACK_MIN_MS. */
#define BACK_MIN_MS 3000
static uint32_t s_back_ms; /* when that state first showed; 0 = not showing */
#define ANSWERED_MIN_MS 900 /* the answered state stays this long, so a quick clear cannot make it flash */
#define ANSWERED_RULE_MS 2500 /* a saved standing rule: long enough to read what was saved */
/* "Always allow" (STANDING-RULES.md). While that item is selected the rule stands in place of the request, exactly
 * as Claude Code will save it (`Bash(npm test:*)`, one rule per line), with two glyphs over it: where it is saved
 * (the folder: this project; the hourglass: until the session ends) and how wide it is (the asterisk, in the
 * request's colour: a wildcard; the equals sign: exactly as written). Nothing is cut: a rule that needs more than
 * RULE_LINES lines locks the item (the report: "Rule too long for the watch"). The hold is bound to the hash of
 * the rule on screen: the daemon saves it only if that is the hash of its own rule, and a rule that changes under
 * a running hold cancels it. */
#define RULE_LINES 2
#define RULE_MARK_DX 22
static lv_obj_t *s_scope, *s_breadth, *s_grule, *s_gsign;
static bool s_gview, s_rule_fits, s_rule_wide_shown, s_answered_rule;
static uint8_t s_rule_scope_shown;
static char s_rule_txt[MODEL_RULE_CAP + 1];
static uint32_t s_rule_hash_shown, s_hold_hash;
static uint32_t s_answered_ms; /* when the answered state first showed; 0 = not showing */
static bool s_keeping;         /* the request is gone but the answered state is still held on screen */
/* Multiple-choice questions (proto 0.18, LAST-MESSAGE.md §3.3): one question at a time, its options as rows. A
 * single-choice option is picked by a hold on its own row; a multi-select one is ticked by a tap and the ticks
 * sent by holding Send; "Other" dictates a free answer. Each answer moves on to the next question and the last
 * one sends them all (cmd answer). The answers so far belong to the request: kept while VOICE takes an "Other"
 * answer, dropped when another request takes the screen. Nothing reaches the daemon before the last answer. */
static struct {
    uint32_t rid;
    int qi; /* the question on screen */
    uint32_t picks[MODEL_MAX_QS], aids[MODEL_MAX_QS];
} s_q;
static bool s_qmode; /* the request on screen has questions */
static char s_qlabel[MODEL_MAX_QOPTS][64];
MODEL_EXT_BSS static char s_qread[MODEL_MAX_QOPTS * 190 + 200]; /* the question and its options' descriptions */
static bool s_qdesc; /* some option has a description: the reader shows them */

static void paint(void);

/* The request this screen is about: the selected session's unanswered permission or question. */
static const attention_t *req(void)
{
    const attention_t *a = model_attention_for_sid(s_sid);
    return a && (a->kind == ATT_APPROVAL || a->kind == ATT_QUESTION) ? a : NULL;
}

static const ask_q_t *cur_q(const attention_t *a) { return s_qmode && a ? model_attention_question(a->rid, s_q.qi) : NULL; }
static bool last_q(const attention_t *a) { return a && s_q.qi >= a->n_qs - 1; }

/* What an item is called: the report's word for it (rule 9). */
static const char *label_of(const ask_item_t *it, const attention_t *a)
{
    switch (it->id) {
    case ASK_QOPT: return s_qlabel[it->opt % MODEL_MAX_QOPTS];
    case ASK_OTHER: return "Other";
    case ASK_SEND: return last_q(a) ? "Send" : "Next";
    case ASK_OPT: return a->opts[it->opt];
    case ASK_APPROVE: return "Approve";
    case ASK_DENY: return "Deny";
    case ASK_DICTATE: return "Dictate";
    case ASK_GRANT: return "Always allow";
    default: return "Open on PC";
    }
}

/* The option's own words, drawn beside its disc; "" for an item that is only a glyph. */
static const char *text_of(const ask_item_t *it, const attention_t *a)
{
    const ask_q_t *q = cur_q(a);
    if (it->id == ASK_QOPT) return q ? q->label[it->opt % MODEL_MAX_QOPTS] : "";
    return it->id == ASK_OPT ? a->opts[it->opt] : "";
}

static bool ticked(const ask_item_t *it) { return it->id == ASK_QOPT && ((s_q.picks[s_q.qi] >> it->opt) & 1u); }

/* The glyph on an item's disc: what it does. A multi-select option's is its tick, when it has one. */
static const char *glyph_of(const ask_item_t *it, const attention_t *a)
{
    const ask_q_t *q = cur_q(a);
    switch (it->id) {
    case ASK_QOPT: return q && q->multi && !ticked(it) ? "" : ICON_CHECK;
    case ASK_OPT: return it->hold_ms && !it->grants ? ICON_X : ICON_CHECK;
    case ASK_OTHER:
    case ASK_DICTATE: return ICON_MIC;
    case ASK_SEND: return last_q(a) ? ICON_SENT : ICON_NEXT;
    case ASK_APPROVE: return ICON_CHECK;
    case ASK_DENY: return ICON_X;
    case ASK_GRANT: return ICON_ALWAYS;
    default: return ICON_COMPUTER;
    }
}

/* Colour says what the item does: orange grants, vermillion denies, yellow answers a question, grey goes to the
 * PC. Never the only cue: the glyph says it too, and a hold item shows its ring. */
static uint32_t rgb_of(const ask_item_t *it)
{
    if (it->id == ASK_QOPT || it->id == ASK_OTHER || it->id == ASK_SEND) return RGB_ASK; /* answers, never grants */
    if (it->grants) return RGB_PERMISSION;
    if (it->hold_ms) return RGB_ERROR;
    return it->id == ASK_OPEN_PC ? RGB_IDLE : RGB_ASK;
}

static void show(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return; /* LVGL invalidates even on a no-op */
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* The disc the selected item is drawn on, or NULL (Open on PC among discs is the key hint). */
static lv_obj_t *sel_disc(void)
{
    if (s_rows) return s_seldisc;
    return s_sel < s_n && s_slot[s_sel].r ? s_disc[s_sel] : NULL;
}

static void set_progress(int permille)
{
    lv_obj_t *d = sel_disc();
    if (d) ui_disc_set_hold(d, permille);
}

static void hold_cancel(void)
{
    if (s_nudge_ms) {
        s_nudge_ms = 0;
        set_progress(0);
    }
    if (!s_holding) return;
    s_holding = false;
    if (s_ramp_started) g_ui_actions->hold_stop();
    s_ramp_started = false;
    ring_set_progress(ui_home_ring(), 0, 0xffffff);
    set_progress(0);
}

/* What the reader shows: the full text as far as it has arrived, else the request's own text. */
static const char *reader_text(void)
{
    if (s_qmode) return s_qread; /* a question and its options' descriptions */
    detail_state_t st = s_fetch ? detail_state(s_rid) : DETAIL_NONE;
    const char *d = st == DETAIL_LOADING || st == DETAIL_DONE ? detail_text(s_rid) : NULL;
    return d ? d : s_text_txt; /* a failed transfer shows the preview, which ends in its ellipsis */
}

/* The reader holds the whole request: a text that was never cut, or a cut one whose full text has arrived. */
static bool text_complete(void) { return !s_cut || (s_fetch && detail_state(s_rid) == DETAIL_DONE); }

/* A cut text the watch will never have in full: approve only on the PC. */
static bool text_unreachable(void) { return s_cut && (!s_fetch || detail_state(s_rid) == DETAIL_FAILED); }

/* The request's text keeps a grant shut: the whole of it has not been, or cannot be, read (its last page shown
 * while the reader held all of it). */
static bool text_locked(const ask_item_t *it)
{
    if (!it->grants) return false;
    if (s_cut) {
        if (text_unreachable()) return ASK_BLOCK_CUT_TEXT || !s_read_ok;
        return !(text_complete() && s_read_ok && s_read_len == detail_total(s_rid));
    }
    return s_long && !s_read_ok;
}

/* True while item `it` may not be held. */
static bool grant_locked(const ask_item_t *it)
{
    if (!it->grants) return false;
    if (it->id == ASK_GRANT && (!s_gview || !s_rule_fits)) return true; /* the rule is not on screen in full */
    return text_locked(it);
}

static void reader_page(void)
{
    int lh = lv_font_get_line_height(FONT_BODY);
    lv_obj_set_y(s_read_lbl, s_read_pad - s_page_i * READ_LINES * lh);
    bool last = s_page_i == s_pages - 1, done = text_complete() || text_unreachable();
    s_read_ms = s_now;
    if (last && done) { /* the whole text has now been on screen */
        s_read_ok = true;
        s_read_len = s_reader_len;
    }
    paint();
}

/* Measures the reader's text again (it grows as the full text arrives); the page on screen stays. */
static void reader_load(void)
{
    const char *t = reader_text();
    int lh = lv_font_get_line_height(FONT_BODY);
    lv_point_t sz;
    lv_text_get_size(&sz, t, FONT_BODY, 0, 0, READ_W, LV_TEXT_FLAG_NONE);
    int lines = sz.y / lh;
    for (size_t n = strlen(t); n > 0 && t[n - 1] == '\n' && lines > 1; n--) lines--; /* no page of blank lines at the end */
    s_pages = lines <= READ_LINES ? 1 : (lines + READ_LINES - 1) / READ_LINES;
    s_read_pad = lines < READ_LINES ? (READ_LINES - lines) * lh / 2 : 0; /* a short text stands in the middle of the window */
    if (s_page_i >= s_pages) s_page_i = s_pages - 1;
    s_reader_len = (uint32_t)strlen(t);
    s_reader_got = detail_got(s_rid);
    s_reader_st = detail_state(s_rid);
    lv_label_set_text_static(s_read_lbl, t);
}

static void reader_open(void)
{
    s_page_i = 0;
    hold_cancel();
    s_reading = true;
    s_gview = false;
    reader_load();
    reader_page();
    dbg_event("ui", "read", "\"rid\":%u,\"pages\":%d", (unsigned)s_rid, s_pages);
}

static void reader_close(void)
{
    if (!s_reading) return;
    s_reading = false;
    paint();
}

/* Where the rule is saved, in words (the report) and as a glyph (the screen). */
static const char *scope_title(uint8_t scope) { return scope == MODEL_RULE_PROJECT ? "Saved for this project" : "Until this session ends"; }
static const char *scope_glyph(uint8_t scope) { return scope == MODEL_RULE_PROJECT ? ICON_FOLDER : ICON_WAIT; }

/* How wide the rule is: a wildcard is said, in the request's colour, never hidden. */
static const char *breadth_note(bool wide) { return wide ? "Wide: * matches anything" : "Exactly as written"; }

static bool grant_selected(void) { return s_sel >= 0 && s_sel < s_n && s_items[s_sel].id == ASK_GRANT; }

/* Shows the rule in place of the request text while "Always allow" is selected; puts everything back when it
 * is not. Called on every refresh and on every change of selection. */
static void rule_view(const attention_t *a)
{
    if (!a) {
        s_gview = false;
        return;
    }
    bool on = !a->answer_sent && a->has_rule && !s_reading && !s_back_ms && grant_selected();
    if (on) {
        bool changed = strcmp(s_rule_txt, a->rule) || s_rule_scope_shown != a->rule_scope || s_rule_wide_shown != a->rule_wide;
        if (changed || !s_gview) {
            int lh = lv_font_get_line_height(FONT_BODY), mh = lv_font_get_line_height(FONT_ICON_M) + 6;
            const lv_font_t *f = FONT_BODY;
            lv_point_t sz;
            lv_text_get_size(&sz, a->rule, FONT_BODY, 0, 0, TEXT_W, LV_TEXT_FLAG_NONE);
            int lines = sz.y / lh;
            s_rule_fits = lines >= 1 && lines <= RULE_LINES && sz.x <= TEXT_W;
            if (s_rule_fits) { /* set large when all of it still fits: the same whole rule, never a part */
                lv_text_get_size(&sz, a->rule, FONT_HEAD, 0, 0, TEXT_W, LV_TEXT_FLAG_NONE);
                int big = sz.y / lv_font_get_line_height(FONT_HEAD);
                if (big >= 1 && big <= RULE_LINES && sz.x <= TEXT_W) f = FONT_HEAD, lines = big, lh = lv_font_get_line_height(FONT_HEAD);
            }
            snprintf(s_rule_txt, sizeof s_rule_txt, "%s", a->rule);
            s_rule_scope_shown = a->rule_scope;
            s_rule_wide_shown = a->rule_wide;
            s_rule_hash_shown = model_rule_hash(s_rule_txt, s_rule_scope_shown, s_rule_wide_shown);
            if (s_rule_fits) { /* the two glyphs and the rule, centred as a block where the request was */
                int y0 = BLOCK_Y0 + (BLOCK_H - mh - lines * lh) / 2;
                ui_glyph_set_pos(s_scope, UI_CX - RULE_MARK_DX, y0 + mh / 2 - 3);
                ui_glyph_set_pos(s_breadth, UI_CX + RULE_MARK_DX, y0 + mh / 2 - 3);
                ui_glyph_set(s_scope, scope_glyph(s_rule_scope_shown), COL_TEXT);
                ui_glyph_set(s_breadth, s_rule_wide_shown ? ICON_WILD : ICON_EXACT,
                             s_rule_wide_shown ? lv_color_hex(RGB_PERMISSION) : COL_TEXT2);
                lv_obj_set_style_text_font(s_grule, f, 0);
                lv_label_set_text(s_grule, s_rule_txt);
                lv_obj_set_size(s_grule, TEXT_W, lines * lh);
                lv_obj_set_pos(s_grule, UI_CX - TEXT_W / 2, y0 + mh);
            }
            if (changed) hold_cancel(); /* a hold belongs to the rule it started on */
            dbg_event("ui", "rule_view", "\"rid\":%u,\"fits\":%s,\"rh\":%u", (unsigned)a->rid, s_rule_fits ? "true" : "false",
                      (unsigned)s_rule_hash_shown);
        }
    }
    s_gview = on;
    snprintf(s_title_txt, sizeof s_title_txt, "%s", on ? scope_title(s_rule_scope_shown) : s_req_title);
}

/* What is asked: the title over the text, centred as a block. A text of one or two short lines is set large. */
static void layout_text(bool perm, const char *text)
{
    const attention_t *a = req();
    int lh = lv_font_get_line_height(FONT_BODY), w = perm ? TEXT_W : TEXT_W_Q;
    lv_point_t sz;
    lv_text_get_size(&sz, text, FONT_BODY, 0, 0, w, LV_TEXT_FLAG_NONE);
    int lines = sz.y / lh;
    size_t tl = strlen(text);
    /* the daemon clips with an ellipsis; a text that fills the model's buffer was clipped on arrival */
    s_cut = (tl >= 3 && !memcmp(text + tl - 3, "\xE2\x80\xA6", 3)) || tl >= sizeof(((attention_t *)0)->text) - 1;
    if (a && a->len_known && a->full_len > 0) s_cut = true; /* proto 0.11: the daemon says it is not the whole text */
    if (s_qmode) s_cut = false; /* a question is all there is to it; its options' descriptions are what the reader adds */
    /* long: not all of it is on screen. A text that goes on anyway (cut, or a question with descriptions) shows
     * MORE_LINES lines, so a third line is already more than the screen holds */
    bool goes_on = s_cut || (s_qmode && s_qdesc);
    s_long = lines > (goes_on ? MORE_LINES : TEXT_LINES) || (s_qmode && s_qdesc);
    s_fetch = s_cut && detail_fetchable(a);
    bool more = s_long || s_cut; /* the book glyph: a text that does not fit, or is not all of it */
    lines = lines < 1 ? 1 : more && lines > MORE_LINES ? MORE_LINES : lines;
    const lv_font_t *f = FONT_BODY;
    if (!more) { /* short enough to be set large */
        lv_text_get_size(&sz, text, FONT_HEAD, 0, 0, w, LV_TEXT_FLAG_NONE);
        int big = sz.y / lv_font_get_line_height(FONT_HEAD);
        if (big >= 1 && big <= 2) f = FONT_HEAD, lines = big, lh = lv_font_get_line_height(FONT_HEAD);
    }
    if (strcmp(s_text_txt, text)) { /* a different text has not been read */
        s_read_ok = false;
        s_reading = false;
        snprintf(s_text_txt, sizeof s_text_txt, "%s", text);
        lv_label_set_text(s_text, text);
    }
    int th = s_title_draw[0] ? lv_font_get_line_height(FONT_BODY_STRONG) + 2 : 0;
    int total = th + lines * lh + (more ? MORE_H : 0), y0 = BLOCK_Y0 + (BLOCK_H - total) / 2;
    s_more_on = more;
    s_title_y = y0;
    /* every style or size write invalidates, and refresh runs on each session delta: change-only */
    int key = (perm << 30) | ((f == FONT_HEAD) << 29) | (more << 28) | (lines << 24) | ((th != 0) << 23) | (y0 << 12) | w;
    if (key != s_blk_key) {
        s_blk_key = key;
        s_text_w = w;
        lv_obj_set_style_text_font(s_text, f, 0);
        lv_obj_set_size(s_text, w, lines * lh);
        lv_obj_set_pos(s_text, UI_CX - w / 2, y0 + th);
        ui_glyph_set_pos(s_more, UI_CX, y0 + th + lines * lh + MORE_H / 2 + 2);
        ui_glyph_set(s_more, ICON_READ, lv_color_hex(perm ? RGB_PERMISSION : RGB_ASK));
    }
    theme_set_text(s_title, s_title_lbl, sizeof s_title_lbl, s_title_draw);
}

/* ───────── the result states: one hero glyph, and under it only what was said or saved ───────── */

static void set_result(const char *hero, lv_color_t col, const char *text, const char *marks, lv_color_t mark_col)
{
    ui_glyph_set(s_hero, hero, col);
    theme_set_text(s_res, s_res_txt, sizeof s_res_txt, text);
    int y = text[0] ? RES_MARK_Y : RES_MARK_Y_ALONE;
    if (y != s_resmark_y) {
        s_resmark_y = y;
        ui_glyph_set_pos(s_resmarks, UI_CX, y);
    }
    ui_glyph_set(s_resmarks, marks, mark_col);
}

/* The request went back to the computer: the terminal shows its own prompt now (ANSWER-WINDOW.md §5). The
 * desktop, and the hourglass under it when it was the time that ran out. */
static void show_back(const session_t *s, uint32_t now)
{
    s_back_ms = now ? now : 1;
    hold_cancel();
    s_reading = false;
    s_gview = false;
    s_n = 0;
    s_opt_txt[0] = 0;
    snprintf(s_word_txt, sizeof s_word_txt, "On your computer");
    snprintf(s_title_txt, sizeof s_title_txt, "%s", s_req_title);
    set_result(ICON_COMPUTER, COL_TEXT2, "", s->back_why == BACK_HANDED ? "" : ICON_WAIT, COL_TEXT2);
    paint();
    dbg_event("ui", "ask_back", "\"rid\":%u,\"why\":%d", (unsigned)s_rid, (int)s->back_why);
}

/* What was sent, in words: one question's answer, or how many. */
static void answers_caption(const attention_t *a)
{
    const ask_q_t *q0 = model_attention_question(a->rid, 0);
    if (a->n_qs != 1 || !q0 || s_q.rid != a->rid) {
        snprintf(s_opt_txt, sizeof s_opt_txt, "%d answers", a->n_qs);
        return;
    }
    if (s_q.aids[0]) {
        snprintf(s_opt_txt, sizeof s_opt_txt, "Your own words");
        return;
    }
    size_t n = 0;
    s_opt_txt[0] = 0;
    for (int i = 0; i < q0->n_opts && n < sizeof s_opt_txt; i++)
        if ((s_q.picks[0] >> i) & 1u) n += (size_t)snprintf(s_opt_txt + n, sizeof s_opt_txt - n, "%s\"%s\"", n ? ", " : "", q0->label[i]);
}

/* The answer is out. The hero says which (the tick, the double tick, the cross, the paper plane); under it what
 * was said or saved (content), and the desktop: the computer has it now. */
static void show_answered(const attention_t *a)
{
    static const char *const ticks[MODEL_MAX_QS + 1] = {"", ICON_CHECK, ICON_CHECK ICON_CHECK, ICON_CHECK ICON_CHECK ICON_CHECK,
                                                        ICON_CHECK ICON_CHECK ICON_CHECK ICON_CHECK};
    uint32_t rgb = RGB_DONE;
    const char *hero = ICON_CHECK, *word = "Approved", *marks = ICON_COMPUTER;
    char text[sizeof s_res_txt] = "";
    lv_color_t mark_col = COL_TEXT2;
    s_after_words = "Waiting for your computer";
    s_opt_txt[0] = 0;
    s_answered_rule = a->answer == ATT_ANSWER_GRANT;
    if (s_answered_rule) { /* what was granted: the rule's first line, and where it is saved */
        word = "Always allowed";
        hero = ICON_ALWAYS;
        bool more = strchr(s_rule_txt, '\n') != NULL;
        int len = (int)strcspn(s_rule_txt, "\n");
        snprintf(s_opt_txt, sizeof s_opt_txt, "%.*s%s", len, s_rule_txt, more ? " +" : "");
        snprintf(text, sizeof text, "%.*s%s", len, s_rule_txt, more ? "\xE2\x80\xA6" : "");
        s_after_words = scope_title(s_rule_scope_shown);
        marks = scope_glyph(s_rule_scope_shown);
    }
    if (a->answer == ATT_ANSWER_DENY) {
        word = "Denied";
        hero = ICON_X;
        rgb = RGB_ERROR;
    } else if (a->answer == ATT_ANSWER_REPLY || a->answer == ATT_ANSWER_DICTATE || a->answer == ATT_ANSWER_QUESTIONS) {
        word = "Sent";
        hero = ICON_SENT;
        rgb = RGB_ASK;
        if (a->answer == ATT_ANSWER_REPLY) snprintf(s_opt_txt, sizeof s_opt_txt, "\"%s\"", a->opts[a->answer_opt % 4]);
        if (a->answer == ATT_ANSWER_QUESTIONS) answers_caption(a);
        if (a->answer == ATT_ANSWER_QUESTIONS && (a->n_qs != 1 || s_q.rid != a->rid)) {
            marks = ticks[a->n_qs <= MODEL_MAX_QS ? a->n_qs : MODEL_MAX_QS]; /* how many answers: a tick each */
            mark_col = lv_color_hex(RGB_ASK);
        } else if (a->answer == ATT_ANSWER_QUESTIONS && s_q.aids[0]) {
            marks = ICON_MIC ICON_COMPUTER; /* the user's own words */
        } else if (s_opt_txt[0]) {
            snprintf(text, sizeof text, "%s", s_opt_txt); /* the option, in the agent's words */
        }
    }
    snprintf(s_word_txt, sizeof s_word_txt, "%s", word);
    snprintf(s_title_txt, sizeof s_title_txt, "%s", s_req_title);
    set_result(hero, lv_color_hex(rgb), text, marks, mark_col);
    s_reading = false;
    s_gview = false;
    hold_cancel();
    s_n = 0;
}

/* The question on screen: its options' names for the report (a tick before a ticked one) and the reader's text. */
static void question_prep(const ask_q_t *q)
{
    uint32_t picks = s_q.picks[s_q.qi];
    for (int i = 0; i < q->n_opts && i < MODEL_MAX_QOPTS; i++)
        snprintf(s_qlabel[i], sizeof s_qlabel[i], "%s%s", q->multi && ((picks >> i) & 1u) ? ICON_DONE " " : "", q->label[i]);
    size_t n = (size_t)snprintf(s_qread, sizeof s_qread, "%s", q->q);
    s_qdesc = false;
    for (int i = 0; i < q->n_opts && i < MODEL_MAX_QOPTS && n < sizeof s_qread; i++) {
        if (!q->desc[i][0]) continue;
        n += (size_t)snprintf(s_qread + n, sizeof s_qread - n, "%s%s: %s", s_qdesc ? "\n" : "\n\n", q->label[i], q->desc[i]);
        s_qdesc = true;
    }
}

/* ───────── the items: discs in fixed places, or rows down a spine ───────── */

/* Where each item's disc stands (a request without options): the first item is the primary disc; the other hold
 * items stand left of it; tap items sit between the key hints; Open on PC is button A's key hint. */
static void place_discs(void)
{
    int left[ASK_MAX_ITEMS], dock[ASK_MAX_ITEMS], nl = 0, nd = 0;
    for (int i = 0; i < ASK_MAX_ITEMS; i++) s_slot[i].r = 0;
    for (int i = 1; i < s_n; i++) {
        if (s_items[i].id == ASK_OPEN_PC) continue;
        if (s_items[i].hold_ms && nl < 2) left[nl++] = i;
        else dock[nd++] = i;
    }
    if (s_n > 0) {
        s_slot[0].x = nl ? P_X : SOLO_X;
        s_slot[0].y = P_Y;
        s_slot[0].r = P_R;
    }
    for (int k = 0; k < nl; k++) {
        s_slot[left[k]].x = nl == 1 ? LEFT_X : LEFT2_X;
        s_slot[left[k]].y = (int16_t)(nl == 1 ? LEFT_Y : LEFT_Y + (k ? LEFT2_DY : -LEFT2_DY));
        s_slot[left[k]].r = nl == 1 ? LEFT_R : LEFT2_R;
    }
    for (int k = 0; k < nd; k++) {
        s_slot[dock[k]].x = (int16_t)(UI_CX + (2 * k - (nd - 1)) * DOCK_PITCH / 2);
        s_slot[dock[k]].y = DOCK_Y;
        s_slot[dock[k]].r = DOCK_R;
    }
}

static int pc_index(void)
{
    for (int i = 0; i < s_n; i++)
        if (s_items[i].id == ASK_OPEN_PC) return i;
    return -1;
}

/* The text of row `slot` (0 above, 1 selected, 2 below), left of nothing, right of its disc. The selected row's
 * is set large when it fits one line, else on up to two lines; a neighbour's is one line. */
static void row_text(int slot, const char *t, bool on)
{
    show(s_row[slot], on && t[0]);
    if (!on || !t[0]) return;
    bool changed = strcmp(s_row_txt[slot], t) != 0;
    if (changed) {
        snprintf(s_row_txt[slot], sizeof s_row_txt[slot], "%s", t);
        lv_label_set_text(s_row[slot], t);
    }
    if (slot != 1 || !changed) return;
    lv_point_t sz;
    lv_text_get_size(&sz, t, FONT_HEAD, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    bool big = sz.x <= ROW_SEL_W;
    int lh = lv_font_get_line_height(big ? FONT_HEAD : FONT_BODY), lines = 1;
    if (!big) {
        lv_text_get_size(&sz, t, FONT_BODY, 0, 0, ROW_SEL_W, LV_TEXT_FLAG_NONE);
        lines = sz.y / lh > 1 ? 2 : 1;
    }
    int key = (big << 8) | lines;
    if (key == s_row_key) return;
    s_row_key = key;
    lv_obj_set_style_text_font(s_row[1], big ? FONT_HEAD : FONT_BODY, 0);
    lv_obj_set_size(s_row[1], ROW_SEL_W, lines * lh);
    lv_obj_set_pos(s_row[1], s_spine_shown + ROW_SEL_R + ROW_GAP, ROW_SEL_Y - lines * lh / 2);
}

/* The rows are centred as a list: the spine stands where the widest option, set large, would be centred with
 * its disc. One place for the whole list, so nothing moves sideways as the selection does. */
static void place_rows(const attention_t *a)
{
    int maxw = 0;
    for (int i = 0; i < s_n; i++) {
        const char *t = text_of(&s_items[i], a);
        lv_point_t sz;
        if (!t[0]) continue;
        lv_text_get_size(&sz, t, FONT_HEAD, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x > maxw) maxw = sz.x;
    }
    if (maxw > ROW_SEL_W) maxw = ROW_SEL_W;
    s_spine = UI_CX - (2 * ROW_SEL_R + ROW_GAP + maxw) / 2 + ROW_SEL_R;
    if (s_spine < SPINE_X) s_spine = SPINE_X;
    if (s_spine == s_spine_shown) return;
    s_spine_shown = s_spine;
    ui_disc_set_pos(s_seldisc, s_spine, ROW_SEL_Y);
    for (int k = 0; k < 3; k++) lv_obj_set_x(s_row[k], s_spine + ROW_SEL_R + ROW_GAP);
    for (int k = 0; k < 2; k++) ui_disc_set_pos(s_nb[k], s_spine, ROW_SEL_Y + (k ? ROW_NB_DY : -ROW_NB_DY));
}

/* Draws the items. Only the selected one is lit; a grant that is still shut is dim; the selected hold item
 * shows where its ring will run. */
static void paint_items(const attention_t *a, bool on)
{
    bool rows = on && s_rows, discs = on && !s_rows;
    int pc = pc_index();
    s_items_on = on;
    for (int i = 0; i < ASK_MAX_ITEMS; i++) {
        bool used = discs && i < s_n && s_slot[i].r > 0;
        show(s_disc[i], used);
        if (!used) continue;
        const ask_item_t *it = &s_items[i];
        bool sel = i == s_sel, shut = text_locked(it) || (it->id == ASK_GRANT && s_gview && !s_rule_fits);
        ui_disc_set_r(s_disc[i], s_slot[i].r);
        ui_disc_set_pos(s_disc[i], s_slot[i].x, s_slot[i].y);
        ui_disc_set_glyph(s_disc[i], glyph_of(it, a));
        ui_disc_set_rgb(s_disc[i], sel && !shut ? rgb_of(it) : 0);
        ui_disc_set_state(s_disc[i], shut ? UI_DISC_DISABLED : UI_DISC_NORMAL);
        ui_disc_set_track(s_disc[i], sel && it->hold_ms > 0);
        if (!sel) ui_disc_set_hold(s_disc[i], 0);
    }
    /* button A's hint is Open on PC itself: ringed when it is the selection */
    ui_disc_set_hold(s_key[0], on && pc >= 0 && pc == s_sel ? 1000 : 0);
    show(s_seldisc, rows);
    for (int k = 0; k < 2; k++) show(s_nb[k], rows && (k == 0 ? s_sel > 0 : s_sel < s_n - 1));
    if (!rows) {
        for (int k = 0; k < 3; k++) show(s_row[k], false);
        return;
    }
    const ask_item_t *it = &s_items[s_sel];
    const char *g = glyph_of(it, a);
    bool shut = grant_locked(it);
    ui_disc_set_glyph(s_seldisc, g);
    ui_disc_set_rgb(s_seldisc, shut || !g[0] ? 0 : rgb_of(it)); /* an unticked option: an empty disc */
    ui_disc_set_state(s_seldisc, shut ? UI_DISC_DISABLED : UI_DISC_NORMAL);
    ui_disc_set_track(s_seldisc, it->hold_ms > 0);
    row_text(1, text_of(it, a), true);
    for (int k = 0; k < 2; k++) {
        bool shown = k == 0 ? s_sel > 0 : s_sel < s_n - 1; /* the list has a top and a bottom: no wrap on the glass */
        row_text(k ? 2 : 0, shown ? text_of(&s_items[s_sel + (k ? 1 : -1)], a) : "", shown);
        if (!shown) continue;
        const ask_item_t *nb = &s_items[s_sel + (k ? 1 : -1)];
        /* a neighbour's ring: empty for a plain option, its tick in yellow once ticked, else what the item does */
        const char *ng = nb->id == ASK_QOPT ? (ticked(nb) && glyph_of(nb, a)[0] ? ICON_CHECK : "")
                         : nb->id == ASK_OPT && !nb->hold_ms ? "" : glyph_of(nb, a);
        ui_disc_set_glyph(s_nb[k], ng);
        ui_disc_set_rgb(s_nb[k], nb->id == ASK_QOPT && ng[0] ? RGB_ASK : RGB_RING);
    }
}

/* One painter for the whole screen: which parts show, from the state the rest of the file keeps. Every setter
 * is change-only, so it is called freely. */
static void paint(void)
{
    const attention_t *a = req();
    bool result = s_back_ms || (a && a->answer_sent) || s_keeping;
    bool pick = a && !result && !s_reading, read = a && !result && s_reading, rule = pick && s_gview;
    bool last = s_page_i == s_pages - 1, done = text_complete() || text_unreachable();
    show(s_title, read || (pick && !rule && s_title_draw[0]));
    int ty = read ? READ_TITLE_Y : s_title_y;
    if ((read || pick) && ty != s_title_y_shown) {
        s_title_y_shown = ty;
        lv_obj_set_y(s_title, ty);
    }
    show(s_text, pick && !rule);
    show(s_more, pick && !rule && s_more_on);
    show(s_scope, rule && s_rule_fits);
    show(s_breadth, rule && s_rule_fits);
    show(s_grule, rule && s_rule_fits);
    show(s_gsign, rule && !s_rule_fits);
    show(s_read, read);
    show(s_load, read && !done); /* more of the text is still arriving */
    paint_items(a, pick);
    show(s_hero, result);
    show(s_res, result && s_res_txt[0]);
    show(s_resmarks, result);
    /* position among a few, as pips: the page of the reader, or which question of several */
    if (read) ui_pips_set(s_pips, s_pages, s_page_i);
    else if (pick && s_qmode && a->n_qs > 1) ui_pips_set(s_pips, a->n_qs, s_q.qi);
    else ui_pips_set(s_pips, 0, 0);
    /* B steps through the items (nothing to step through with one); in the reader it turns the page, then closes */
    show(s_key[1], read || (pick && s_n > 1));
    ui_key_hint_set(s_key[1], !read ? ICON_NEXT : last && done ? ICON_CHECK : ICON_CARET_DOWN);
}

const attention_t *ui_ask_request(void)
{
    const attention_t *a = req();
    return a && !a->answer_sent && !s_back_ms ? a : NULL;
}

/* Returns false when there is nothing left to ask (request cleared or session gone). */
bool ui_ask_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = model_find(s_sid);
    const attention_t *a = req();
    if (!s || s->state == SESS_ENDED) return false;
    if (!a) {
        /* cleared: hold the answered state on screen for a moment (a new request or input still takes over) */
        if (s_answered_ms && now - s_answered_ms < (s_answered_rule ? ANSWERED_RULE_MS : ANSWERED_MIN_MS)) {
            s_keeping = true;
            paint();
            return true;
        }
        /* the window ran out (or Open on PC handed it back): say where the request is now, then go */
        if (!s_answered_ms && s_rid && s->back) {
            if (!s_back_ms) show_back(s, now);
            return now - s_back_ms < BACK_MIN_MS;
        }
        return false;
    }
    s_keeping = false;
    if (s_back_ms) { /* a new request took over from the "On your computer" state */
        s_back_ms = 0;
        s_rid = 0;
    }
    if (a->rid != s_rid) { /* a different request: re-arm, start at the default, drop any hold */
        hold_cancel();
        s_reading = false;
        s_read_ok = false;
        s_text_txt[0] = 0;
        lv_label_set_text(s_text, "");
        s_rid = a->rid;
        s_shown_ms = now;
        s_read_len = 0;
        s_listed = 0;
        s_sel = 0;
    }
    if (s_q.rid != a->rid) { /* another request's answers are not this one's */
        memset(&s_q, 0, sizeof s_q);
        s_q.rid = a->rid;
    }
    s_qmode = a->n_qs > 0;
    if (s_qmode && s_q.qi >= a->n_qs) s_q.qi = a->n_qs - 1;
    const ask_q_t *q = cur_q(a);
    if (q) question_prep(q);
    /* who asks, in words for the report; on the glass it is the sigil and the agent's mark */
    char buf[80];
    const char *base = theme_basename(s->repo);
    char host[24];
    theme_host_prefix(host, sizeof host, s);
    snprintf(buf, sizeof buf, "%s%s%s%s", host, theme_agent_name(s->agent), theme_sep(), base[0] ? base : "(no folder)");
    if (q && a->n_qs > 1) { /* which question of how many */
        size_t n = strlen(buf);
        snprintf(buf + n, sizeof buf - n, "%s%d of %d", theme_sep(), s_q.qi + 1, a->n_qs);
    }
    snprintf(s_header_txt, sizeof s_header_txt, "%s", buf);
    ui_sigil_set_name(s_sigil, base);
    ui_glyph_set(s_mark[0], s->host ? ICON_G_WEB : "", COL_TEXT3);
    ui_glyph_set(s_mark[1], theme_agent_glyph(s->agent), COL_TEXT3);
    bool perm = a->kind == ATT_APPROVAL;
    /* the title: the question's header, the request's own, else the tool. With none of them the report names
     * the kind; the glass leaves the row out (the request's colour already says which) */
    const char *own = q ? q->h : a->title[0] ? a->title : s->tool;
    snprintf(s_title_draw, sizeof s_title_draw, "%s", own);
    snprintf(s_req_title, sizeof s_req_title, "%s", own[0] ? own : q || !perm ? "Question" : "Permission request");
    snprintf(s_title_txt, sizeof s_title_txt, "%s", s_req_title);
    layout_text(perm, q ? q->q : a->text);
    if (s_fetch && !a->answer_sent) detail_want(a, now); /* the full text, while the user looks at the request */

    if (!a->answer_sent) s_answered_ms = 0, s_answered_rule = false;
    else if (!s_answered_ms) s_answered_ms = now ? now : 1;
    if (a->answer_sent) {
        show_answered(a);
    } else {
        s_n = q ? nav_question_items(q, model_voice(), s_q.picks[s_q.qi], s_items, ASK_MAX_ITEMS)
                : nav_ask_items(a, model_voice(), s_items, ASK_MAX_ITEMS);
        if (s_listed != s_n) s_sel = 0; /* another list: back to the default */
        s_listed = s_n;
        s_rows = false;
        for (int i = 0; i < s_n; i++)
            if (s_items[i].id == ASK_OPT || s_items[i].id == ASK_QOPT) s_rows = true; /* options in words: rows */
        if (s_rows) place_rows(a);
        else place_discs();
        rule_view(a);
    }
    paint();
    return true;
}

void ui_ask_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_rid = 0;
    s_now = now;
    s_holding = s_hold_fired = s_ramp_started = false;
    s_nudge_ms = 0;
    s_answered_ms = 0;
    s_answered_rule = false;
    s_back_ms = 0;
    s_keeping = false;
    s_gview = false;
    s_rule_txt[0] = 0;
    s_read_ok = s_gate_press = false;
    s_read_len = 0;
    s_text_txt[0] = 0;
    lv_label_set_text(s_text, "");
    s_reading = false;
    s_listed = 0;
    s_sel = 0;
    s_n = 0;
    ui_ask_refresh(now);
    ui_home_rim_mode(true); /* a rim drag dials through the items */
    ring_set_still(ui_home_ring(), true); /* the sessions stay on the ring, settled: no comet behind a decision */
    show(s_root, true);
    dbg_event("ui", "ask", "\"sid\":%u,\"rid\":%u,\"items\":%d", (unsigned)sid, (unsigned)s_rid, s_n);
}

void ui_ask_close(void)
{
    hold_cancel();
    s_reading = false;
    show(s_root, false);
    ring_set_still(ui_home_ring(), !ui_animations_allowed());
}

/* The item whose disc or row the screen point is on, or -1. Among discs the nearest centre wins. */
static int item_hit(int x, int y)
{
    if (!s_items_on || lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) return -1;
    if (ui_disc_hit(s_key[0], x, y)) return pc_index(); /* button A's hint is Open on PC */
    if (s_rows) {
        int top = ROW_SEL_Y - ROW_SEL_H / 2, bottom = ROW_SEL_Y + ROW_SEL_H / 2;
        if (x < ROW_X0 || x > ROW_X1) return -1;
        if (y >= top && y < bottom) return s_sel;
        if (s_sel > 0 && y >= top - ROW_NB_H && y < top) return s_sel - 1;
        if (s_sel < s_n - 1 && y >= bottom && y < bottom + ROW_NB_H) return s_sel + 1;
        return -1;
    }
    int best = -1, best_d = 0;
    for (int i = 0; i < s_n; i++) {
        int dx = x - s_slot[i].x, dy = y - s_slot[i].y, r = s_slot[i].r + UI_DISC_SLACK, d = dx * dx + dy * dy;
        if (!s_slot[i].r || d > r * r || (best >= 0 && d >= best_d)) continue;
        best = i;
        best_d = d;
    }
    return best;
}

/* A tap target of ASK lies at the point: ui.c lets a tap on one through although it is beyond the centre zone
 * (button A's hint, the disc between the hints). */
bool ui_ask_target_hit(int x, int y) { return item_hit(x, y) >= 0; }

/* A hold acts on the item under the finger: it arms only when the press is on the selected item's own disc or
 * row. A press on another item is the start of a tap on that item. */
static bool press_on_selected(const gesture_evt_t *g) { return s_n > 0 && item_hit(g->x, g->y) == s_sel; }

static int s_scrub_last, s_drag_acc;
static bool s_drag_paged; /* the reader turns one page per touch */

static void select_item(int i, const char *via, void (*haptic)(void))
{
    int n = s_n;
    if (n == 0) return;
    i = ((i % n) + n) % n;
    if (i == s_sel) return;
    hold_cancel(); /* the hold belongs to the item it started on */
    s_sel = i;
    rule_view(req());
    paint();
    dbg_event("ui", "item", "\"idx\":%d,\"via\":\"%s\"", i, via);
    if (haptic) haptic();
}

/* The question on screen is answered: on to the next one, re-armed like a new card (the hold that answered it
 * cannot run on into the next one), or, after the last, every answer goes out at once. */
static void question_answered(const attention_t *a, uint32_t hold_ms)
{
    if (!last_q(a)) {
        hold_cancel();
        s_q.qi++;
        s_reading = false;
        s_read_ok = false;
        s_shown_ms = s_now;
        s_listed = 0;
        s_sel = 0;
        g_ui_actions->tick();
        dbg_event("ui", "question", "\"rid\":%u,\"qi\":%d", (unsigned)a->rid, s_q.qi);
        ui_ask_refresh(s_now);
        return;
    }
    g_ui_actions->answer(s_sid, a->rid, s_q.picks, s_q.aids, a->n_qs, hold_ms);
}

/* VOICE took an "Other" answer for question qi of request rid: it stands for that question, and the screen
 * moves on (or, for the last question, everything goes out with this hold). */
void ui_ask_other_done(uint32_t rid, int qi, uint32_t aid, uint32_t hold_ms)
{
    const attention_t *a = model_attention_find(rid);
    if (!a || a->answer_sent || s_q.rid != rid || qi != s_q.qi || qi >= a->n_qs) {
        g_ui_actions->reject();
        return;
    }
    s_q.aids[qi] = aid;
    s_q.picks[qi] = 0;
    s_sid = a->sid;
    question_answered(a, hold_ms);
}

/* Sends the answer for item `idx` of request `a`. Approve-class and deny items only arrive here from
 * a completed hold (or an injected long press that covered it); never from a tap. */
static void commit(const attention_t *a, int idx, uint32_t hold_ms)
{
    const ask_item_t *it = &s_items[idx];
    dbg_event("ui", "commit", "\"sid\":%u,\"rid\":%u,\"item\":%d,\"opt\":%d,\"hold_ms\":%u", (unsigned)s_sid,
              (unsigned)a->rid, (int)it->id, it->opt, (unsigned)hold_ms);
    const ask_q_t *q = cur_q(a);
    switch (it->id) {
    case ASK_QOPT:
        if (!q) break;
        if (q->multi) { /* a tap ticks or unticks it; nothing is sent */
            s_q.picks[s_q.qi] ^= 1u << it->opt;
            s_q.aids[s_q.qi] = 0;
            g_ui_actions->tick();
            dbg_event("ui", "tick", "\"rid\":%u,\"qi\":%d,\"picks\":%u", (unsigned)a->rid, s_q.qi, (unsigned)s_q.picks[s_q.qi]);
            ui_ask_refresh(s_now);
            break;
        }
        s_q.picks[s_q.qi] = 1u << it->opt; /* only ever after the hold on this row */
        s_q.aids[s_q.qi] = 0;
        question_answered(a, hold_ms);
        break;
    case ASK_SEND:
        if (!q || !s_q.picks[s_q.qi]) {
            ui_toast("Tick at least one", RGB_AMBER);
            g_ui_actions->reject();
            break;
        }
        s_q.aids[s_q.qi] = 0;
        question_answered(a, hold_ms);
        break;
    case ASK_OTHER: ui_go_voice_answer(a->rid, s_q.qi, last_q(a)); break;
    case ASK_OPT: g_ui_actions->reply(s_sid, a->rid, (uint8_t)it->opt, hold_ms); break;
    case ASK_APPROVE: g_ui_actions->approve(s_sid, a->rid, hold_ms); break;
    case ASK_GRANT: /* the rule on screen, and only if it is still the request's own */
        if (s_gview && s_rule_fits && a->has_rule && s_rule_hash_shown == a->rule_hash)
            g_ui_actions->grant(s_sid, a->rid, hold_ms, s_rule_hash_shown);
        else
            g_ui_actions->reject();
        break;
    case ASK_DENY: g_ui_actions->deny(s_sid, a->rid); break;
    case ASK_DICTATE: ui_go_voice(a->rid, false); break;
    case ASK_OPEN_PC: /* the terminal shows its prompt at once (proto 0.11): the request is handed back */
        g_ui_actions->open_request(s_sid, a->rid);
        ui_go_home();
        break;
    }
}

/* Back closes the reader first; the request and what has been read stay as they are. */
bool ui_ask_back(void)
{
    const attention_t *a = req();
    if (!s_reading && s_qmode && a && !a->answer_sent && !s_back_ms && s_q.qi > 0) { /* the question before */
        hold_cancel();
        s_q.qi--;
        s_read_ok = false;
        s_shown_ms = s_now;
        s_listed = 0;
        s_sel = 0;
        dbg_event("ui", "question", "\"rid\":%u,\"qi\":%d", (unsigned)a->rid, s_q.qi);
        ui_ask_refresh(s_now);
        return true;
    }
    if (!s_reading) return false;
    reader_close();
    ui_ask_refresh(s_now);
    return true;
}

/* A tap on a hold item never commits it: the item's ring starts and falls back, and the toast names the hold. */
static void nudge(void)
{
    if (s_holding || !sel_disc()) return;
    s_nudge_ms = s_now ? s_now : 1;
    set_progress(NUDGE_PM);
}

/* A locked grant item answers a tap by showing why: the reader if the text is unread, a toast if the watch
 * can never show all of it. */
static void grant_tell(const ask_item_t *it)
{
    if (it->id == ASK_GRANT && s_gview && !s_rule_fits) {
        ui_toast("Rule too long for the watch", rgb_of(it));
        g_ui_actions->reject();
        return;
    }
    if (text_unreachable() && ASK_BLOCK_CUT_TEXT && (s_read_ok || !s_long)) {
        ui_toast("Too long to approve here", rgb_of(it));
        g_ui_actions->reject();
    } else {
        reader_open();
        g_ui_actions->tick();
    }
}

/* What a hold on a locked grant is told. */
static const char *lock_word(const ask_item_t *it)
{
    if (it->id == ASK_GRANT && !s_rule_fits) return "Rule too long for the watch";
    return text_unreachable() && ASK_BLOCK_CUT_TEXT ? "Too long to approve here" : "Read it first";
}

/* What a tap on a hold item says: a tap never commits it. */
static const char *hold_word(const ask_item_t *it)
{
    if (it->id == ASK_QOPT) return "Hold to choose";
    if (it->id == ASK_SEND) return last_q(req()) ? "Hold to send" : "Hold for the next question";
    return it->id == ASK_GRANT ? "Hold to always allow" : it->grants ? "Hold to approve" : "Hold to deny";
}

/* The request's text: a tap on it opens the reader when there is more to read. */
static bool on_text(const gesture_evt_t *g)
{
    return !s_gview && g->y >= BLOCK_Y0 && g->y < BLOCK_Y0 + BLOCK_H && g->x >= UI_CX - s_text_w / 2 - 10 &&
           g->x <= UI_CX + s_text_w / 2 + 10;
}

/* Input while the reader is up. Nothing here can answer the request: a swipe down turns a page back and is
 * never the deny swipe, and holds do nothing. Button A is still Open on PC (handled by the caller). */
static bool read_act(nav_action_t act, const gesture_evt_t *g)
{
    switch (act) {
    case NAV_COMMIT:
        if (g->kind == G_TAP && s_now - s_read_ms < NAV_CARD_ARM_MS) return true; /* the tap that opened it, doubled */
        /* fallthrough */
    case NAV_NEXT:
        if (s_page_i < s_pages - 1) {
            s_page_i++;
            reader_page();
        } else if (!text_complete() && !text_unreachable()) {
            return true; /* the rest is still arriving: the next page appears when it has */
        } else {
            ui_ask_back();
        }
        g_ui_actions->tick();
        return true;
    case NAV_PREV:
        if (s_page_i > 0) {
            s_page_i--;
            reader_page();
            g_ui_actions->tick();
        }
        return true;
    case NAV_SCROLL_DRAG: { /* the page follows the finger: up is the next page, down the one before */
        int d = nav_list_drag(&s_drag_acc, g->dy);
        int to = s_page_i + (d > 0) - (d < 0);
        if (d && !s_drag_paged && to >= 0 && to < s_pages) {
            s_drag_paged = true;
            s_page_i = to;
            reader_page();
            g_ui_actions->tick();
        }
        return true;
    }
    case NAV_HOLD_PRESS:
        s_drag_acc = 0;
        s_drag_paged = false;
        return true;
    case NAV_HOLD_LONG: ui_go_back(); return true; /* held still: closes the reader */
    case NAV_HOLD_RELEASE:
    case NAV_SCRUB: return true;
    default: return false;
    }
}

bool ui_ask_act(nav_action_t act, const gesture_evt_t *g)
{
    const attention_t *a = req();
    if (s_back_ms) { /* "On your computer": nothing to answer; any input goes on to the deck */
        if (act == NAV_FOCUS) g_ui_actions->focus(s_sid);
        if (act != NAV_HOLD_PRESS && act != NAV_HOLD_RELEASE && act != NAV_SCRUB) ui_go_home();
        return true;
    }
    /* a result's burst covers the request (GLYPHS.md §3.7): nothing is armed, selected or sent under it */
    if (ui_burst_active() && (act == NAV_HOLD_PRESS || act == NAV_COMMIT || act == NAV_HOLD_LONG)) return true;
    if (s_reading && act != NAV_FOCUS) return read_act(act, g);
    if (act == NAV_SCROLL_DRAG) { /* the items follow the finger; a moving finger holds nothing */
        s_gate_press = false;
        hold_cancel();
        int d = nav_list_drag(&s_drag_acc, g->dy);
        if (d && a && !a->answer_sent && s_n) select_item(s_sel + d, "drag", g_ui_actions->detent);
        return true;
    }
    if (act == NAV_HOLD_LONG && item_hit(g->x, g->y) < 0) { /* held still off the items: back */
        ui_go_back();
        return true;
    }
    int sel = s_sel;
    const ask_item_t *it = a && !a->answer_sent && sel < s_n ? &s_items[sel] : NULL;
    switch (act) {
    case NAV_HOLD_PRESS:
        s_hold_fired = false;
        s_drag_acc = 0;
        if (it && it->hold_ms && grant_locked(it) && press_on_selected(g)) { /* locked: never arms */
            s_gate_press = true;
            s_gate_t0 = s_now;
            return true;
        }
        /* only a hold item arms a hold, only for the request on screen, only once it is armed, and only while
         * its answer window runs */
        if (it && it->hold_ms && press_on_selected(g) && nav_hold_may_start(s_rid, s_shown_ms, a->rid, s_now) &&
            model_attention_left_ms(a, s_now) > 0) {
            if (s_nudge_ms) { /* a tap's nudge is not the start of this ring */
                s_nudge_ms = 0;
                set_progress(0);
            }
            s_holding = true;
            s_ramp_started = false;
            s_hold_t0 = s_now;
            s_hold_rid = a->rid;
            s_hold_idx = sel;
            s_hold_hash = it->id == ASK_GRANT ? s_rule_hash_shown : 0;
        }
        return true;
    case NAV_HOLD_RELEASE:
        s_gate_press = false;
        hold_cancel();
        return true;
    case NAV_COMMIT: {
        /* the card must have been on screen a moment before a tap means anything: the tap that opened it, or
         * a second one right behind it, would otherwise land on another item */
        if (g->kind == G_TAP && s_now - s_shown_ms < NAV_CARD_ARM_MS) return true;
        /* A tap acts on the item it lands on (UI-V3 §3.3), but never approves or denies: another item that
         * needs a hold is only selected, and the toast says which hold it needs. */
        if (it && (s_long || s_cut) && on_text(g)) { /* the text itself opens the reader */
            reader_open();
            g_ui_actions->tick();
            return true;
        }
        int hit = it ? item_hit(g->x, g->y) : -1;
        if (hit >= 0 && hit < s_n && hit != sel) {
            const ask_item_t *h = &s_items[hit];
            select_item(hit, "tap", g_ui_actions->tick);
            if (h->hold_ms && grant_locked(h)) {
                grant_tell(h);
            } else if (h->hold_ms) {
                ui_toast(hold_word(h), rgb_of(h));
                nudge();
            } else {
                commit(a, hit, 0);
            }
            return true;
        }
        if (!it) {
            g_ui_actions->reject();
            return true;
        }
        if (it->hold_ms && grant_locked(it)) {
            grant_tell(it);
            return true;
        }
        if (it->hold_ms) { /* a tap never commits an approve-class or deny item */
            ui_toast(hold_word(it), rgb_of(it));
            nudge();
            g_ui_actions->reject();
            return true;
        }
        commit(a, sel, 0);
        return true;
    }
    case NAV_HOLD_LONG: /* injected input whose duration already covers the hold */
        if (it && it->hold_ms && !grant_locked(it) && g->dur_ms >= it->hold_ms && !s_hold_fired && press_on_selected(g) &&
            nav_hold_may_start(s_rid, s_shown_ms, a->rid, s_now) && model_attention_left_ms(a, s_now) > 0 &&
            (it->id != ASK_GRANT || s_rule_hash_shown == a->rule_hash)) {
            hold_cancel();
            s_hold_fired = true;
            commit(a, sel, g->dur_ms);
        }
        return true;
    case NAV_FOCUS: /* button A: Open on PC, whatever is selected; the request goes back to the terminal */
        if (a && !a->answer_sent) g_ui_actions->open_request(s_sid, a->rid);
        else g_ui_actions->focus(s_sid);
        ui_go_home();
        return true;
    case NAV_SCRUB: {
        int d = nav_list_scrub(&s_scrub_last, g->kind, g->x);
        if (d && it) select_item(sel + d, "scrub", g_ui_actions->detent);
        return true;
    }
    case NAV_NEXT: select_item(sel + 1, "btn", g_ui_actions->tick); return true;
    case NAV_PREV: select_item(sel - 1, "btn", g_ui_actions->tick); return true;
    default: return false;
    }
}

/* Returns true when the screen wants to close: the request was cleared or its session went away.
 * ASK has no idle timeout; it waits for the answer or the daemon's clear. */
bool ui_ask_tick(uint32_t now)
{
    s_now = now;
    const attention_t *a = req();
    if (!model_find(s_sid)) return true;
    if (!a)
        return !((s_answered_ms && now - s_answered_ms < (s_answered_rule ? ANSWERED_RULE_MS : ANSWERED_MIN_MS)) ||
                 (s_back_ms && now - s_back_ms < BACK_MIN_MS));
    if (s_reading && s_fetch && (detail_got(s_rid) != s_reader_got || detail_state(s_rid) != s_reader_st)) {
        reader_load(); /* more of the full text arrived */
        reader_page();
    }
    if (s_nudge_ms && !s_holding && now - s_nudge_ms >= NUDGE_MS) { /* the nudged ring falls back */
        s_nudge_ms = 0;
        set_progress(0);
    }
    if (s_gate_press && now - s_gate_t0 >= GATE_TELL_MS) { /* a hold on a locked Approve: say why, once */
        s_gate_press = false;
        if (!a->answer_sent && s_sel < s_n && s_items[s_sel].hold_ms && grant_locked(&s_items[s_sel])) {
            ui_toast(lock_word(&s_items[s_sel]), rgb_of(&s_items[s_sel]));
            g_ui_actions->reject();
        }
    }
    if (s_holding && !s_hold_fired) {
        uint32_t el = now - s_hold_t0;
        const ask_item_t *it = s_hold_idx < s_n ? &s_items[s_hold_idx] : NULL;
        if (a->rid != s_hold_rid || a->answer_sent || !it || !it->hold_ms || s_hold_idx != s_sel || grant_locked(it) ||
            ui_burst_active() || model_attention_left_ms(a, now) == 0 || /* the window is over: nothing to answer */
            (it->id == ASK_GRANT && (!a->has_rule || s_hold_hash != a->rule_hash || s_hold_hash != s_rule_hash_shown))) {
            hold_cancel();
            return false;
        }
        uint32_t rgb = rgb_of(it);
        if (el >= NAV_HOLD_SHOW_MS) {
            if (!s_ramp_started) {
                s_ramp_started = true;
                if (it->grants || it->id == ASK_QOPT || it->id == ASK_SEND) g_ui_actions->hold_start();
                else g_ui_actions->deny_hold_start();
            }
            ring_set_progress(ui_home_ring(), (int)(el * 1000 / it->hold_ms), rgb);
            set_progress((int)(el * 1000 / it->hold_ms));
        }
        if (el >= it->hold_ms) {
            s_hold_fired = true;
            s_holding = false;
            s_ramp_started = false;
            ring_set_progress(ui_home_ring(), 0, rgb);
            set_progress(0);
            commit(a, s_hold_idx, el);
        }
    }
    return false;
}

/* The legend of the screen (rule 9): every glyph, disc and ring above, in the words the screen used to show. */
void ui_ask_report(ui_report_t *r)
{
    const attention_t *a = req();
    snprintf(r->screen, sizeof r->screen, "ask");
    r->has_sel_sid = model_find(s_sid) != NULL;
    r->sel_sid = s_sid;
    if (a) {
        r->has_rid = true;
        r->rid = a->rid;
    }
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    if (s_title_txt[0]) ui_report_add(r, "%s", s_title_txt);
    if (s_gview && !(a && a->answer_sent) && !s_keeping) { /* the rule in place of the request text */
        ui_report_add(r, "%s", s_rule_fits ? s_rule_txt : "This rule is too long to show on the watch");
        ui_report_add(r, "%s", s_rule_fits ? breadth_note(s_rule_wide_shown) : "Answer it on your computer");
    } else if (s_text_txt[0]) {
        ui_report_add(r, "%s", s_text_txt);
    }
    if (s_back_ms) {
        ui_report_add(r, "On your computer");
        return;
    }
    if ((a && a->answer_sent) || s_keeping) {
        ui_report_add(r, "%s", s_word_txt);
        if (s_opt_txt[0]) ui_report_add(r, "%s", s_opt_txt);
        ui_report_add(r, "%s", s_after_words);
        return;
    }
    if (s_reading) {
        ui_report_add(r, "Reading %d of %d", s_page_i + 1, s_pages);
        if (!text_complete() && !text_unreachable()) ui_report_add(r, "Loading full text");
        return;
    }
    if (s_long || s_cut) ui_report_add(r, "Read all");
    if (!a) return;
    for (int k = 0; k < s_n && k < (s_gview ? 3 : 4); k++) {
        const ask_item_t *it = &s_items[(s_sel + k) % s_n];
        ui_report_add(r, "%s%s%s", k == 0 ? "> " : "", label_of(it, a), it->hold_ms ? " HOLD" : "");
    }
    ui_report_add(r, "%d options", s_n);
}

/* A text label: `lines` high, cut with dots beyond that. Positioned by its top-left corner. */
static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, lv_text_align_t align, int x, int y, int w, int lines)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(f));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ui_ask_init(lv_obj_t *screen)
{
    int lh = lv_font_get_line_height(FONT_BODY);
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* who asks: the project's sigil, as on HOME, the agent's mark beside it */
    s_sigil = ui_sigil_create(s_root, UI_CX, SIGIL_Y, SIGIL_R);
    s_mark[0] = ui_glyph_create(s_root, FONT_ICON_S, UI_CX - MARK_DX, SIGIL_Y);
    s_mark[1] = ui_glyph_create(s_root, FONT_ICON_S, UI_CX + MARK_DX, SIGIL_Y);

    /* what is asked: content, so text (rule 1) */
    s_title = text(s_root, FONT_BODY_STRONG, COL_TEXT2, LV_TEXT_ALIGN_CENTER, UI_CX - TITLE_W / 2, BLOCK_Y0, TITLE_W, 1);
    s_title_y_shown = BLOCK_Y0;
    s_text = text(s_root, FONT_BODY, COL_TEXT, LV_TEXT_ALIGN_CENTER, UI_CX - TEXT_W / 2, BLOCK_Y0 + 26, TEXT_W, 1);
    s_text_w = TEXT_W;
    s_more = ui_glyph_create(s_root, FONT_ICON_M, UI_CX, BLOCK_Y0 + BLOCK_H - MORE_H / 2);
    /* "Always allow": the rule as it will be saved, under where and how wide; never an ellipsis */
    s_scope = ui_glyph_create(s_root, FONT_ICON_M, UI_CX - RULE_MARK_DX, BLOCK_Y0 + 20);
    s_breadth = ui_glyph_create(s_root, FONT_ICON_M, UI_CX + RULE_MARK_DX, BLOCK_Y0 + 20);
    s_grule = text(s_root, FONT_BODY, COL_TEXT, LV_TEXT_ALIGN_CENTER, UI_CX - TEXT_W / 2, BLOCK_Y0 + 40, TEXT_W, RULE_LINES);
    lv_label_set_long_mode(s_grule, LV_LABEL_LONG_MODE_CLIP); /* a rule that does not fit locks instead */
    s_gsign = ui_glyph_create(s_root, FONT_ICON_L, UI_CX, BLOCK_Y0 + BLOCK_H / 2);
    ui_glyph_set(s_gsign, ICON_ALWAYS ICON_REFUSED ICON_COMPUTER, lv_color_hex(RGB_PERMISSION)); /* not here: on the computer */

    /* the reader: a window READ_LINES tall over the whole text */
    s_read = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_read);
    lv_obj_remove_flag(s_read, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_read, READ_W, READ_LINES * lh);
    lv_obj_set_pos(s_read, UI_CX - READ_W / 2, READ_Y);
    s_read_lbl = lv_label_create(s_read);
    lv_obj_set_style_text_font(s_read_lbl, FONT_BODY, 0);
    lv_obj_set_style_text_color(s_read_lbl, COL_TEXT, 0);
    lv_obj_set_style_text_align(s_read_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_read_lbl, READ_W);
    lv_label_set_text_static(s_read_lbl, "");
    s_load = ui_glyph_create(s_root, FONT_ICON_S, UI_CX, LOAD_Y);
    ui_glyph_set(s_load, ICON_WORKING, COL_TEXT2);
    s_pips = ui_pips_create(s_root, UI_CX, PIPS_Y);

    /* what can be done: discs (rule 5) */
    for (int i = 0; i < ASK_MAX_ITEMS; i++) s_disc[i] = ui_disc_create(s_root, P_X, P_Y, DOCK_R);
    s_seldisc = ui_disc_create(s_root, SPINE_X, ROW_SEL_Y, ROW_SEL_R);
    for (int k = 0; k < 2; k++) {
        int y = ROW_SEL_Y + (k ? ROW_NB_DY : -ROW_NB_DY);
        s_nb[k] = ui_disc_create(s_root, SPINE_X, y, ROW_NB_R);
        s_row[k ? 2 : 0] = text(s_root, FONT_BODY, COL_TEXT2, LV_TEXT_ALIGN_LEFT, SPINE_X + ROW_SEL_R + ROW_GAP, y - lh / 2, ROW_NB_W, 1);
    }
    s_row[1] = text(s_root, FONT_HEAD, COL_TEXT, LV_TEXT_ALIGN_LEFT, SPINE_X + ROW_SEL_R + ROW_GAP,
                    ROW_SEL_Y - lv_font_get_line_height(FONT_HEAD) / 2, ROW_SEL_W, 1);
    s_row_key = (1 << 8) | 1;

    /* a result: one hero glyph (rule 7); under it what was said or saved, and where it is now */
    s_hero = ui_glyph_create(s_root, FONT_ICON_HERO, UI_CX, HERO_Y);
    s_res = text(s_root, FONT_BODY_STRONG, COL_TEXT, LV_TEXT_ALIGN_CENTER, UI_CX - 150, RES_TEXT_Y - 12, 300, 1);
    s_resmark_y = RES_MARK_Y;
    s_resmarks = ui_glyph_create(s_root, FONT_ICON_M, UI_CX, RES_MARK_Y);

    /* rule 6: A opens the request on the computer, and its hint is that item; B steps to the next item */
    s_key[0] = ui_key_hint_create(s_root, UI_KEY_A);
    s_key[1] = ui_key_hint_create(s_root, UI_KEY_B);
    ui_key_hint_set(s_key[0], ICON_COMPUTER);
    ui_key_hint_set(s_key[1], ICON_NEXT);

    lv_obj_t *hide[] = {s_title, s_text, s_more, s_scope, s_breadth, s_grule, s_gsign, s_read, s_load, s_seldisc, s_nb[0],
                        s_nb[1], s_row[0], s_row[1], s_row[2], s_hero, s_res, s_resmarks};
    for (size_t i = 0; i < sizeof hide / sizeof hide[0]; i++) lv_obj_add_flag(hide[i], LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < ASK_MAX_ITEMS; i++) lv_obj_add_flag(s_disc[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
