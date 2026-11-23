/* The deck's centre in Sigil (docs/design/GLYPHS.md §4), the reference screen for the primitives of
 * ui_kit.h.
 *
 * A session is read top to bottom without a word of chrome:
 *   - its project's sigil, and under it one small row: the agent's mark (and what else marks the
 *     session: remote, snoozed, quiet), or a sign in colour when something is wrong (offline with
 *     its age, connecting, battery low);
 *   - its state as one hero glyph in the state colour, the time in that state as dots round it, a
 *     job's progress as an arc inside the dots;
 *   - what it says: the summary, the done line or the last message, or what it asks (text: rule 1);
 *   - between the two key hints: the agent graph, the context gauge, the book disc that opens READ.
 * A tap on the hero glyph of a session that needs you opens ASK (nav.c: any centre tap does), as
 * the Answer / Review button did; the report still names it so. The empty deck is the clock alone;
 * the ambient face is the clock, dim, with the sigil and a small state glyph under it.
 *
 * Pure presentation: it reads the model and the deck's selection (ui_home_internal.h); slots,
 * selection and actions live in ui_home.c. What every glyph means is said in words in the report
 * (rule 9), as before. Redraw discipline (UI-V2 §8a): only change-only setters; on a tick a new
 * minute repaints one dot. */
#include <stdio.h>
#include <string.h>

#include "hal.h"
#include "ui_done.h"
#include "ui_gauges.h"
#include "ui_home_internal.h"
#include "ui_kit.h"
#include "ui_theme.h"

/* Geometry: centres, top to bottom. The content circle is r 205; the key hints sit at (101, 365)
 * and (365, 365), so below y 345 a row is only as wide as the gap between them. */
#define SIGIL_Y 50
#define SIGIL_R 22
#define MARK_Y 84 /* the marks row: 20 px glyphs, grey */
#define SIGN_Y 88 /* a sign in its place: one 28 px glyph in colour, and its tally */
#define SIGN_PX 28
#define HERO_Y 206 /* the state glyph, the dots and the progress arc share this centre */
#define DOTS_R 88
#define PROG_R 76 /* a job's progress: inside the dots, round the glyph */
#define PROG_W 4
/* text rows: the summary's first line, the done line, a request's title */
#define LINE1_Y 316
#define LINE2_Y 340
#define LINE3_Y 364   /* only a request has a third row */
#define SUMMARY_W 300 /* the first row */
#define LINE2_W 240   /* its ends stop short of the key hints' rings, which begin at y 345 */
#define LINE3_W 200   /* between the key hints */
#define SLOT_X 129    /* the slot between the key hints: x 129..337 */
#define SLOT_W 208
#define STRIP_Y 374 /* the agent graph, under a one-line summary */
/* ui_graph.c: the strip is 300 x 56, its newest call at the right edge */
#define STRIP_H 56
#define STRIP_W 300
#define CTX_Y 372     /* the context gauge: under the text ... */
#define CTX_Y_LOW 412 /* ... or under the graph or the book disc, when one holds the slot */
#define READ_Y 380    /* the book disc */
#define READ_R 24
#define CLOCK_Y 222 /* the empty deck: the clock alone */
#define CLOCK_Y_AMBIENT 190
/* the ambient face: sigil and state glyph, side by side under the clock */
#define AMBIENT_Y 262
#define AMBIENT_DX 28
#define BATT_LOW 15
#define AMBIENT_GREY 0x737373 /* white at 45 % */
/* Where the embedded parts draw inside the parent they are given (their own constants): the card
 * places each by moving a full-size parent. */
#define GAUGES_OWN_Y 219 /* ui_gauges.c: the context capsule */
#define DONE_OWN_Y 246   /* ui_done.c: the done line */

typedef enum { LOOK_CARD, LOOK_EMPTY, LOOK_AMBIENT } look_t;
typedef enum { SIGN_NONE, SIGN_OFFLINE, SIGN_CONNECTING, SIGN_BATTERY } sign_t;

static lv_obj_t *s_centre, *s_sigil, *s_stack, *s_marks, *s_sign, *s_age, *s_dots, *s_prog, *s_hero;
static lv_obj_t *s_line1, *s_line2, *s_req_title, *s_req1, *s_req2, *s_gauge_box, *s_done_box,
    *s_strip_clip, *s_read;
static lv_obj_t *s_clock, *s_ready, *s_amb_sigil, *s_amb_glyph;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2]; /* [0] = A, [1] = B */

static bool s_ambient, s_quiet_shown, s_clock_set, s_back_shown, s_time_shown, s_read_shown;
static uint32_t s_minute, s_batt_next;
static look_t s_look = LOOK_CARD;
static sign_t s_sign_kind;
static int s_prog_pm = -1, s_gauge_y, s_clock_y, s_sign_x = -1;

/* What the card says in words, for the report (rule 9): the screen draws the glyphs. */
static char s_cap_words[64], s_name_words[40], s_state_words[24], s_time_words[48], s_sub_words[64],
    s_sign_words[32];
static const char *s_btn_word = "";
/* The texts on screen, and the label caches of their rows (in PSRAM: they are the card's only large
 * statics). */
static char s_req_title_txt[48], s_clock_txt[8], s_marks_txt[40];
MODEL_EXT_BSS static char s_sum_txt[176], s_req_text_txt[176];
MODEL_EXT_BSS static char s_line1_txt[96], s_line2_txt[96], s_req1_txt[96], s_req2_txt[96];
static bool s_sum_one_line;

/* ───────── small helpers ───────── */

static lv_obj_t *mk(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, int cy, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    /* one row: the dots need the fixed height */
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    int lh = lv_font_get_line_height(f);
    lv_obj_set_size(l, w, lh);
    lv_obj_set_pos(l, UI_CX - w / 2, cy - lh / 2);
    lv_label_set_text(l, "");
    return l;
}

/* A full-size, see-through parent for a part that places itself: moving the parent moves the
 * part. */
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static bool shown(lv_obj_t *o) { return !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }
static bool offline(void) { return !ui_link_connected(); }

/* The wall clock in local time; false until it has been set (the RTC reads 0 before that). */
static bool local_clock(unsigned *h, unsigned *m, uint32_t *minute)
{
    uint32_t now = theme_now_unix();
    if (now <= 100000) return false;
    uint32_t local = now + (uint32_t)((int32_t)model_settings()->tz_off_min * 60);
    *h = local / 3600 % 24;
    *m = local / 60 % 60;
    *minute = local / 60;
    return true;
}

/* Text for two rows of different widths: `first` gets the words of `text` that fit `w` px (one word
 * at least; its row cuts a longer one with dots), and the rest is returned. Line breaks count as
 * spaces. */
static const char *split(const char *text, const lv_font_t *f, int w, char *first, size_t cap)
{
    size_t n = strlen(text), best = 0;
    if (n >= cap) n = cap - 1;
    memcpy(first, text, n);
    first[n] = 0;
    for (size_t i = 0; i < n; i++)
        if (first[i] == '\n' || first[i] == '\r' || first[i] == '\t') first[i] = ' ';
    for (size_t i = 1; i <= n; i++) {
        if (i < n && first[i] != ' ') continue;
        if (first[i - 1] == ' ') continue; /* a run of spaces: measured at its first one */
        char keep = first[i];
        lv_point_t sz;
        first[i] = 0;
        lv_text_get_size(&sz, first, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        first[i] = keep;
        if (sz.x > w && best) break;
        best = i;
        if (sz.x > w) break;
    }
    first[best] = 0;
    const char *rest = text + best;
    while (*rest == ' ' || *rest == '\n' || *rest == '\r' || *rest == '\t') rest++;
    return rest;
}

/* The agent's last message can be read here (LAST-MESSAGE.md): a finished, unmerged agent session
 * with a preview (none under privacy meta, none from an older daemon) and no request or hand-back
 * on its card. */
static bool has_message(const session_t *s, const home_slot_t *sl)
{
    return s && sl && !sl->n_merged && !s->back && !ui_home_askable_of(s) && !theme_is_job(s) &&
           s->lm && s->lp[0] && s->state != SESS_WORK && s->state != SESS_START &&
           s->state != SESS_ENDED;
}

bool ui_home_read_hit(int x, int y) { return s_read_shown && ui_disc_hit(s_read, x, y); }

/* The graph strip, where it shows: its window between the key hints (the strip itself is wider). */
bool ui_home_strip_hit(int x, int y)
{
    return x >= SLOT_X - UI_DISC_SLACK && x <= SLOT_X + SLOT_W + UI_DISC_SLACK &&
           ui_graph_strip_hit(x, y);
}

/* ───────── the row under the sigil: marks, or a sign ───────── */

/* A sign, by precedence: offline (how long, as a tally) or connecting (the transport's glyph), then
 * a low battery. Otherwise the row carries `marks` in grey. The ambient face shows neither. */
static void update_marks(const char *marks)
{
    const char *icon = "";
    uint32_t rgb = 0, age_min = 0, down = ui_link_down_ms();
    int k = ui_link_transport();
    sign_t kind = SIGN_NONE;
    if (offline()) {
        rgb = RGB_AMBER;
        if (down && model_count() > 0) {
            char age[16];
            theme_elapsed(age, sizeof age, down / 1000);
            snprintf(s_sign_words, sizeof s_sign_words, "Offline%s%s", theme_sep(), age);
            kind = SIGN_OFFLINE;
            icon = ICON_OFFLINE;
            age_min = down / 60000;
        } else if (k < 0) {
            snprintf(s_sign_words, sizeof s_sign_words, "Offline");
            kind = SIGN_OFFLINE;
            icon = ICON_OFFLINE;
        } else {
            snprintf(s_sign_words, sizeof s_sign_words, "Connecting\xE2\x80\xA6");
            kind = SIGN_CONNECTING;
            icon = k == HAL_LINK_USB    ? ICON_USB
                   : hal_link_is_ble(k) ? ICON_BLUETOOTH
                                        : ICON_COMPUTER;
        }
    } else {
        hal_battery_t b = hal_battery_read();
        if (b.percent <= BATT_LOW && !b.charging) {
            rgb = RGB_ERROR;
            kind = SIGN_BATTERY;
            icon = ICON_BATTERY_WARN;
            snprintf(s_sign_words, sizeof s_sign_words, "%u%%", b.percent);
        }
    }
    if (s_look == LOOK_AMBIENT) kind = SIGN_NONE; /* the ambient face is the clock and one mark */
    s_sign_kind = kind;
    s_quiet_shown = model_quiet_active(theme_now_unix());
    show(s_sign, kind != SIGN_NONE);
    show(s_age, kind != SIGN_NONE && age_min > 0);
    show(s_marks, kind == SIGN_NONE && s_look != LOOK_AMBIENT);
    if (kind != SIGN_NONE) { /* the glyph and its tally, centred together */
        int tw = age_min ? ui_tally_width(age_min) : 0, gw = SIGN_PX, gap = tw ? 8 : 0;
        int x = UI_CX - (gw + gap + tw) / 2 + gw / 2;
        if (x != s_sign_x) {
            s_sign_x = x;
            ui_glyph_set_pos(s_sign, x, SIGN_Y);
        }
        ui_glyph_set(s_sign, icon, lv_color_hex(rgb));
        if (age_min) {
            ui_tally_set_pos(s_age, x + gw / 2 + gap + tw / 2, SIGN_Y);
            ui_tally_set(s_age, age_min, lv_color_hex(rgb));
        }
        return;
    }
    char buf[40];
    snprintf(buf, sizeof buf, "%s%s%s", marks, marks[0] && s_quiet_shown ? " " : "",
             s_quiet_shown ? ICON_QUIET : "");
    theme_set_text(s_marks, s_marks_txt, sizeof s_marks_txt, buf);
}

/* ───────── time in state: dots, and a job's arc ───────── */

/* Time in the current state (UI-V2 §10) as dots, one a minute; the report says it in words. While
 * offline the clock is frozen at the moment the link dropped, so a stale card does not keep
 * counting as if it were live. */
static void update_time(const session_t *s, bool merged, lv_color_t col)
{
    uint32_t now = theme_now_unix() - ui_link_down_ms() / 1000, from = s->tst ? s->tst : s->t0,
             secs = 0;
    char buf[40] = "";
    bool job = !merged && theme_is_job(s), ago = false, timed = false;
    /* a job's estimate: an arc that fills, in the state colour; change-only, a degree at a time */
    int pm = job ? theme_job_progress(s, now) : -1;
    s_prog_pm = pm;
    show(s_prog, pm >= 0);
    if (pm >= 0) ui_arc_set(s_prog, pm, col, COL_FILL2);
    if (job) {
        theme_job_time(buf, sizeof buf, s, now);
        /* then the dots are how long it ran */
        bool ended = s->state == SESS_DONE || s->state == SESS_ERROR;
        timed = s->t0 && (ended || now >= s->t0);
        if (timed) secs = !ended ? now - s->t0 : s->tst >= s->t0 ? s->tst - s->t0 : 0;
    } else if (!merged && from && now >= from &&
               (s->state == SESS_DONE || s->state == SESS_ERROR)) {
        /* a finished agent: how long ago it ended (an age, not a second duration next to the
         * summary's turn time) */
        theme_elapsed(buf, sizeof buf, now - from);
        secs = now - from;
        ago = timed = true;
    } else if (!merged && from && now >= from) {
        theme_elapsed_long(buf, sizeof buf, now - from);
        secs = now - from;
        timed = true;
    }
    bool snoozed = !merged && model_session_snoozed(s, theme_now_unix());
    if (snoozed)
        snprintf(s_time_words, sizeof s_time_words, "%s%ssnoozed", buf, buf[0] ? theme_sep() : "");
    else if (ago && secs < 60) snprintf(s_time_words, sizeof s_time_words, "just now");
    else snprintf(s_time_words, sizeof s_time_words, "%s%s", buf, ago ? " ago" : "");
    s_time_shown = buf[0] != 0 || snoozed;
    show(s_dots, timed);
    if (timed) ui_dots_set(s_dots, secs / 60, col);
}

/* ───────── looks ───────── */

/* One opacity for the centre: only the ambient face without a clock keeps the v2 dimmed card.
 * Offline is drawn in grey instead (no layer to blend), so its sign stays bright. */
static void apply_dim(void)
{ theme_set_opa(s_centre, s_ambient && s_look == LOOK_CARD ? LV_OPA_30 : LV_OPA_COVER); }

/* Ambient burn-in guard: the centre drifts 1 px each minute, round a 2 x 2 square. */
static void apply_shift(void)
{
    int step = s_ambient ? (int)(s_minute & 3) : 0;
    int x = s_ambient && (step == 1 || step == 2), y = s_ambient && step >= 2;
    if (lv_obj_get_x(s_centre) != x || lv_obj_get_y(s_centre) != y) lv_obj_set_pos(s_centre, x, y);
}

static void set_looks(look_t look)
{
    s_look = look;
    bool card = look == LOOK_CARD;
    show(s_hero, card);
    show(s_clock, !card && s_clock_set);
    show(s_ready, look == LOOK_EMPTY && !s_clock_set);
    int y = look == LOOK_AMBIENT ? CLOCK_Y_AMBIENT : CLOCK_Y;
    if (!card && y != s_clock_y) {
        s_clock_y = y;
        lv_obj_set_y(s_clock, y - lv_font_get_line_height(FONT_NUM) / 2);
    }
    if (look != LOOK_AMBIENT) {
        show(s_amb_sigil, false);
        show(s_amb_glyph, false);
    }
    if (card) return;
    lv_obj_t *const off[] = {s_sigil, s_stack,     s_dots, s_prog, s_line1,
                             s_line2, s_req_title, s_req1, s_req2, s_read};
    for (size_t i = 0; i < sizeof off / sizeof off[0]; i++) show(off[i], false);
    for (int i = 0; i < 2; i++) show(s_badge[i], false);
    s_read_shown = false;
    s_prog_pm = -1;
    ui_done_update(NULL, false, false);
    ui_gauges_card_update(NULL, false, false);
    ui_graph_strip_update(NULL, false);
}

static void draw_clock(void)
{
    unsigned h, m;
    char buf[8] = "";
    s_clock_set = local_clock(&h, &m, &s_minute);
    if (s_clock_set) snprintf(buf, sizeof buf, "%02u:%02u", h, m);
    theme_set_text(s_clock, s_clock_txt, sizeof s_clock_txt, buf);
}

void ui_home_card_set_ambient(bool ambient)
{
    s_ambient = ambient;
    ui_home_card_update();
}

/* a width nothing reaches: split() then only turns line breaks into spaces */
#define ONE_ROW (1 << 20)

/* The summary on one row (the graph has the rest of the room) or on two. */
static void set_summary(const char *text, bool one_line, lv_color_t col)
{
    if (strcmp(s_sum_txt, text) || one_line != s_sum_one_line) {
        /* split only when the text changes */
        char first[sizeof s_sum_txt], second[sizeof s_sum_txt];
        s_sum_one_line = one_line;
        snprintf(s_sum_txt, sizeof s_sum_txt, "%s", text);
        const char *rest =
            split(text, FONT_BODY, one_line ? ONE_ROW : SUMMARY_W, first, sizeof first);
        split(rest, FONT_BODY, ONE_ROW, second, sizeof second);
        theme_set_text(s_line1, s_line1_txt, sizeof s_line1_txt, first);
        theme_set_text(s_line2, s_line2_txt, sizeof s_line2_txt, second);
    }
    theme_set_text_color(s_line1, col);
    theme_set_text_color(s_line2, col);
    show(s_line1, true);
    show(s_line2, !one_line && s_line2_txt[0] != 0);
}

void ui_home_card_update(void)
{
    draw_clock();
    const session_t *s = ui_home_sel_session();
    int n = ui_home_slot_count();
    bool dark = s_ambient && s_clock_set;
    lv_color_t amb = lv_color_hex(AMBIENT_GREY);

    if (!s || n == 0) { /* the empty deck: the clock alone says it */
        set_looks(LOOK_EMPTY);
        snprintf(s_sub_words, sizeof s_sub_words, "Nothing running");
        theme_set_text_color(s_clock, dark ? amb : COL_TEXT);
        ui_glyph_set(s_ready, ICON_DONE, dark ? amb : COL_TEXT);
        update_marks("");
        ui_gauges_face_update(true, dark); /* GAUGES.md: "5h 42%" on the idle face */
        apply_dim();
        apply_shift();
        return;
    }
    const home_slot_t *sl = ui_home_sel_slot();
    const attention_t *req = ui_home_askable_of(s);
    /* The request decides what the user is told: its kind, not the session's sub-state, names
     * it. */
    /* the request went back to the computer (its window ran out, or Open on PC): the terminal asks
     * now */
    bool back = !sl->n_merged && !req && s->back;
    const char *word = sl->n_merged ? "Done"
                       : req  ? (req->kind == ATT_APPROVAL ? "Needs approval" : "Has a question")
                       : back ? "On your computer"
                       : s->state == SESS_INPUT ? "Waiting"
                                                : theme_state_word(s);
    const char *hero = sl->n_merged ? ICON_DONE
                       : req        ? (req->kind == ATT_APPROVAL ? ICON_PERMISSION : ICON_QUESTION)
                       : back       ? ICON_COMPUTER
                       : s->state == SESS_INPUT ? ICON_WAIT
                                                : theme_state_glyph(s);
    /* who it is, in words for the report: the agent, then the branch; merged finished sessions are
     * a count */
    char marks[32] = "";
    if (sl->n_merged) {
        snprintf(s_cap_words, sizeof s_cap_words, "%d sessions", sl->n_merged);
        /* under the stack glyph: a count (rule 4) */
        snprintf(marks, sizeof marks, "%d", sl->n_merged);
    } else {
        char host[24];
        theme_host_prefix(host, sizeof host, s); /* a remote session leads with its host */
        if (s->branch[0])
            snprintf(s_cap_words, sizeof s_cap_words, "%s%s%s%s", host, theme_agent_name(s->agent),
                     theme_sep(), s->branch);
        else snprintf(s_cap_words, sizeof s_cap_words, "%s%s", host, theme_agent_name(s->agent));
        /* on glass: remote, the agent's mark, snoozed; a failed job adds its exit status, which has
         * no picture */
        int len = snprintf(marks, sizeof marks, "%s%s%s", s->host ? ICON_G_WEB " " : "",
                           theme_agent_glyph(s->agent),
                           model_session_snoozed(s, theme_now_unix()) ? " " ICON_IDLE : "");
        if (theme_is_job(s) && s->state == SESS_ERROR)
            snprintf(marks + len, sizeof marks - (size_t)len, " %u", (unsigned)s->exit_code);
    }
    const char *base = sl->n_merged ? "" : theme_basename(s->repo);
    snprintf(s_name_words, sizeof s_name_words, "%s",
             sl->n_merged ? "Done"
             : !base[0]   ? "(no folder)"
                          : base);
    snprintf(s_state_words, sizeof s_state_words, "%s", word);

    if (dark) { /* ambient: the clock, the sigil and the state, all dim: "webapp · Working" */
        set_looks(LOOK_AMBIENT);
        snprintf(s_sub_words, sizeof s_sub_words, "%s%s%s", s_name_words, theme_sep(), word);
        theme_set_text_color(s_clock, amb);
        show(s_amb_sigil, !sl->n_merged);
        show(s_amb_glyph, true);
        ui_sigil_set_name(s_amb_sigil, base);
        ui_sigil_set_color(s_amb_sigil, amb);
        ui_glyph_set_pos(s_amb_glyph, sl->n_merged ? UI_CX : UI_CX + AMBIENT_DX, AMBIENT_Y);
        ui_glyph_set(s_amb_glyph, hero, amb);
        update_marks("");
        ui_gauges_face_update(true, true);
        apply_dim();
        apply_shift();
        return;
    }
    set_looks(LOOK_CARD);
    ui_gauges_face_update(false, false);
    bool off = offline();
    uint32_t rgb =
        req ? (req->kind == ATT_APPROVAL ? RGB_PERMISSION : RGB_ASK) : theme_session_rgb(s);
    /* a stale card is grey: only its sign has colour */
    lv_color_t sc = off ? COL_TEXT3 : lv_color_hex(rgb);

    show(s_sigil, !sl->n_merged);
    show(s_stack, sl->n_merged != 0);
    ui_sigil_set_name(s_sigil, base);
    ui_sigil_set_color(s_sigil, off ? COL_TEXT3 : COL_TEXT);
    ui_glyph_set(s_stack, ICON_SESSIONS, off ? COL_TEXT3 : COL_TEXT);
    ui_glyph_set(s_hero, hero, sc);
    s_back_shown = back;
    update_time(s, sl->n_merged != 0, off ? COL_TEXT3 : theme_dim(rgb, 85));
    update_marks(marks);

    bool msg = has_message(s, sl), strip = false;
    if (req) {
        /* what is being asked, in place of the summary; the tool name when the daemon sent no
         * title */
        char first[sizeof s_req_text_txt];
        theme_set_text(s_req_title, s_req_title_txt, sizeof s_req_title_txt,
                       /* neither: the hero glyph already says which kind */
                       req->title[0] ? req->title : s->tool);
        if (strcmp(s_req_text_txt, req->text)) {
            snprintf(s_req_text_txt, sizeof s_req_text_txt, "%s", req->text);
            const char *rest = split(req->text, FONT_BODY, LINE2_W, first, sizeof first);
            theme_set_text(s_req1, s_req1_txt, sizeof s_req1_txt, first);
            split(rest, FONT_BODY, ONE_ROW, first, sizeof first);
            theme_set_text(s_req2, s_req2_txt, sizeof s_req2_txt, first);
        }
        show(s_line1, false);
        show(s_line2, false);
        ui_done_update(NULL, false, false);
        show(s_req_title, true);
        show(s_req1, req->text[0] != 0);
        show(s_req2, s_req2_txt[0] != 0);
        theme_set_text_color(s_req_title, off ? COL_TEXT2 : COL_TEXT);
        ui_graph_strip_update(s, false);
    } else {
        char sum[sizeof s->lp + 4];
        show(s_req_title, false);
        show(s_req1, false);
        show(s_req2, false);
        /* DONE-SUMMARY.md: the done line takes the stale last step's place. LAST-MESSAGE.md: a
         * finished agent shows the start of what it said, in its own words, and the book disc */
        bool done_line = ui_done_update(s, !msg && !sl->n_merged && !back && !theme_is_job(s), off);
        /* the agent graph (GRAPH.md §5): between the key hints, when nothing else holds the slot */
        strip = ui_graph_strip_update(s, !msg && !sl->n_merged);
        /* a job says what runs: its command line, or just the program when the daemon withholds
         * that */
        theme_display_text(sum, sizeof sum,
                           msg                                 ? s->lp
                           : sl->n_merged || back              ? ""
                           : theme_is_job(s) && !s->summary[0] ? s->tool
                                                               : s->summary);
        if (done_line) {
            show(s_line1, false);
            show(s_line2, false);
        } else {
            set_summary(sum, strip, off ? COL_TEXT3 : msg ? COL_TEXT : COL_TEXT2);
        }
    }
    /* what a tap does, in the words the buttons had: the hero glyph is Review / Answer, the book
     * disc is Read (not while offline: the message is fetched when it is opened) */
    s_read_shown = msg && !off;
    s_btn_word = req            ? (req->kind == ATT_APPROVAL ? "Review" : "Answer")
                 : s_read_shown ? "Read"
                                : "";
    show(s_read, s_read_shown);
    /* GAUGES.md: the context capsule under the text (a job has none); a request needs the room */
    int gy = strip || s_read_shown ? CTX_Y_LOW : CTX_Y;
    if (gy != s_gauge_y) {
        s_gauge_y = gy;
        lv_obj_set_y(s_gauge_box, gy - GAUGES_OWN_Y);
    }
    ui_gauges_card_update(s, !sl->n_merged && !req && !theme_is_job(s), off);
    show(s_badge[0], true);
    show(s_badge[1], n > 1);
    apply_dim();
    apply_shift();
}

/* Every 500 ms: the time in state moves on (a dot a minute), the offline age ticks, the clock face
 * moves on at the minute, and the quiet mark goes when quiet runs out. */
void ui_home_card_tick(void)
{
    const session_t *s = ui_home_sel_session();
    const home_slot_t *sl = ui_home_sel_slot();
    unsigned h, m;
    uint32_t minute;
    bool set = local_clock(&h, &m, &minute);
    if (set != s_clock_set || (set && minute != s_minute && (s_look != LOOK_CARD || s_ambient))) {
        ui_home_card_update();
        return;
    }
    uint32_t now_ms = (uint32_t)lv_tick_get();
    bool batt = (int32_t)(now_ms - s_batt_next) >= 0;
    if (batt) s_batt_next = now_ms + 10000;
    if (ui_link_down_ms() || batt || s_quiet_shown != model_quiet_active(theme_now_unix())) {
        /* every setter is change-only: this repaints what moved, if anything */
        ui_home_card_update();
        return;
    }
    if (s && sl && s_look == LOOK_CARD && !sl->n_merged) {
        const attention_t *req = ui_home_askable_of(s);
        uint32_t rgb =
            req ? (req->kind == ATT_APPROVAL ? RGB_PERMISSION : RGB_ASK) : theme_session_rgb(s);
        update_time(s, false, offline() ? COL_TEXT3 : theme_dim(rgb, 85));
    }
}

void ui_home_card_report(ui_report_t *r)
{
    const session_t *s = ui_home_sel_session();
    if (s_look == LOOK_CARD && s) {
        ui_report_add(r, "%s", s_cap_words);
        ui_report_add(r, "%s", s_name_words);
        ui_report_add(r, "%s", s_state_words);
        if (s_time_shown) ui_report_add(r, "%s", s_time_words);
        if (s_back_shown) ui_report_add(r, "Answer it there");
        if (s_prog_pm >= 0) ui_report_add(r, "progress %d%%", s_prog_pm / 10);
        ui_gauges_card_report(r);
        ui_done_report(r);
        if (shown(s_line1)) ui_report_add(r, "%s", s_sum_txt);
        if (shown(s_req_title)) ui_report_add(r, "%s", s_req_title_txt);
        if (shown(s_req1)) ui_report_add(r, "%s", s_req_text_txt);
    } else {
        ui_report_add(r, "%s", s_sub_words);
        if (s_clock_set) ui_report_add(r, "%s", s_clock_txt);
        if (!s) ui_report_add(r, "No sessions");
        ui_gauges_face_report(r);
    }
    if (s_sign_kind != SIGN_NONE) ui_report_add(r, "%s", s_sign_words);
    if (s_look == LOOK_CARD && s_btn_word[0]) ui_report_add(r, "%s", s_btn_word);
    ui_graph_strip_report(r);
    if (s_quiet_shown) ui_report_add(r, "quiet");
}

void ui_home_card_create(lv_obj_t *centre)
{
    s_centre = centre;
    /* who: the sigil (a stack glyph for merged finished sessions), and the row under it */
    s_sigil = ui_sigil_create(s_centre, UI_CX, SIGIL_Y, SIGIL_R);
    s_stack = ui_glyph_create(s_centre, FONT_ICON_L, UI_CX, SIGIL_Y);
    s_marks = mk(s_centre, FONT_CAPTION, COL_TEXT3, MARK_Y, 200);
    s_sign = ui_glyph_create(s_centre, FONT_ICON_M, UI_CX, SIGN_Y);
    s_age = ui_tally_create(s_centre, UI_CX, SIGN_Y);

    /* how it is: the hero glyph, the time round it, a job's progress between the two */
    s_dots = ui_dots_create(s_centre, UI_CX, HERO_Y, DOTS_R);
    s_prog = ui_arc_create(s_centre, UI_CX, HERO_Y, PROG_R, PROG_W);
    s_hero = ui_glyph_create(s_centre, FONT_ICON_HERO, UI_CX, HERO_Y);

    /* what it says or asks: text (rule 1) */
    s_line1 = mk(s_centre, FONT_BODY, COL_TEXT2, LINE1_Y, SUMMARY_W);
    s_line2 = mk(s_centre, FONT_BODY, COL_TEXT2, LINE2_Y, LINE2_W);
    s_req_title = mk(s_centre, FONT_BODY_STRONG, COL_TEXT, LINE1_Y, SUMMARY_W);
    s_req1 = mk(s_centre, FONT_BODY, COL_TEXT2, LINE2_Y, LINE2_W);
    s_req2 = mk(s_centre, FONT_BODY, COL_TEXT2, LINE3_Y, LINE3_W);

    /* the parts other files draw, each in a parent that puts it in its slot */
    s_gauge_y = CTX_Y;
    s_gauge_box = box(s_centre, 0, CTX_Y - GAUGES_OWN_Y, UI_SIZE, UI_SIZE);
    ui_gauges_card_create(s_gauge_box);
    ui_gauges_face_create(s_centre);
    s_done_box = box(s_centre, 0, LINE1_Y - DONE_OWN_Y, UI_SIZE, UI_SIZE);
    ui_done_create(s_done_box);
    /* the graph strip is 300 px wide and fills from its right edge: it sits in a window as wide as
     * the gap between the key hints, its right edge on the window's, so the newest calls are the
     * ones in sight */
    s_strip_clip = box(s_centre, SLOT_X, STRIP_Y - STRIP_H / 2, SLOT_W, STRIP_H);
    lv_obj_t *strip_at = box(s_strip_clip, SLOT_W - (UI_CX + STRIP_W / 2), 0, UI_SIZE, STRIP_H);
    ui_graph_strip_create(strip_at, STRIP_H / 2);

    s_read = ui_disc_create(s_centre, UI_CX, READ_Y, READ_R);
    ui_disc_set_glyph(s_read, ICON_READ);

    /* the clock (the empty deck, the ambient face) and what stands in for it before it is set */
    s_clock = mk(s_centre, FONT_NUM, COL_TEXT, CLOCK_Y, 300);
    s_clock_y = CLOCK_Y;
    s_ready = ui_glyph_create(s_centre, FONT_ICON_XL, UI_CX, CLOCK_Y);
    s_amb_sigil = ui_sigil_create(s_centre, UI_CX - AMBIENT_DX, AMBIENT_Y, SIGIL_R);
    s_amb_glyph = ui_glyph_create(s_centre, FONT_ICON_M, UI_CX + AMBIENT_DX, AMBIENT_Y);

    /* rule 6: what the buttons do, at the buttons. A opens the session on the computer (the desktop
     * glyph: the vocabulary's "Open" is the dots of a menu, and the session menu is a tap, not A);
     * B is the next session. */
    static const char *const words[2] = {"Open", "Next"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_centre, i == 0 ? "A" : "B", words[i], i == 0, 0, 0,
                                     &s_key[i], &s_word[i]);
    ui_key_hint_set(s_badge[0], ICON_COMPUTER);
    lv_obj_t *hide[] = {s_stack, s_sign, s_age,  s_dots,  s_prog,  s_line2,     s_req_title,
                        s_req1,  s_req2, s_read, s_clock, s_ready, s_amb_sigil, s_amb_glyph};
    for (size_t i = 0; i < sizeof hide / sizeof hide[0]; i++)
        lv_obj_add_flag(hide[i], LV_OBJ_FLAG_HIDDEN);
}
