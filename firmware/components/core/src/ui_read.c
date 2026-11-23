/* The READ screen in Sigil (docs/design/GLYPHS.md §5; LAST-MESSAGE.md §1): the agent's last
 * message, fetched from the daemon as it is read (detail.c, proto 0.18), then what to do about it.
 *
 * The reader is the message and nothing else in words (a summary: rule 1): the session's sigil and
 * its agent's mark at the top, seven lines of the text, and on the key-hint row the page dots, with
 * a small ring beside them that fills as the rest of the message arrives (the error glyph when it
 * did not). Button B's hint is the caret while there is more to read and the reply arrow on the
 * last page. Past the last page the actions are discs (rule 5): an orbit arched over the sigil,
 * Dictate, Reply, Read again, Open on PC, and under the sigil the end of the message, as a reminder
 * of what is being answered.
 *
 * Nothing here sends anything to the agent: a page turn only reads, and the discs only open VOICE
 * or the Reply list, whose own holds and taps send. Until the whole message has arrived the reader
 * shows what it has (the preview first). Every glyph is named in words in the report (rule 9), as
 * before. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "detail.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"

/* Geometry: centres unless a top is named. */
#define SIGIL_Y 50
#define SIGIL_R 22
#define MARK_Y 84 /* the agent's mark, as on the deck */
/* top of the text: seven lines, 320 px wide, as the legibility pass set them */
#define READ_Y 124
#define READ_W 320
#define READ_LINES 7
#define PIPS_Y UI_KEY_Y /* the page dots: on the key-hint row, between the two hints */
#define LOAD_R 9        /* the ring that fills as the message arrives */
#define LOAD_W 3
#define LOAD_GAP 20                     /* from the last page dot to the ring's centre */
#define LOAD_HALF (LOAD_R + LOAD_W + 2) /* half the side of the ring's own small parent */
/* the end: an orbit over the sigil, the message's last lines under it */
#define ARCH_SPAN 222
#define ARCH_PITCH 50
#define END_SIGIL_Y 204
#define END_SIGIL_R 40
#define TAIL_Y 262 /* top of three lines */
#define TAIL_W 250
#define TAIL_LINES 3

enum { END_DICTATE = 1, END_REPLY, END_AGAIN, END_OPEN };
#define END_MAX 4

typedef struct {
    const char *label, *glyph;
    uint32_t rgb; /* its colour when selected; 0 = the session's */
    int id;
} end_item_t;

static lv_obj_t *s_root, *s_page_box, *s_end_box;
static lv_obj_t *s_sigil, *s_marks, *s_read, *s_lbl, *s_pips, *s_load, *s_load_err; /* the reader */
static lv_obj_t *s_orbit, *s_end_sigil, *s_tail;                                    /* the end */
static lv_obj_t *s_hint[2]; /* [0] = A, [1] = B */
static end_item_t s_items[END_MAX];
static int s_n, s_sel;
static char s_header_txt[64], s_title_txt[40], s_marks_txt[24];
MODEL_EXT_BSS static char s_tail_txt[120];
static uint32_t s_sid, s_lm, s_now, s_page_ms, s_opened_ms;
static int s_page_i, s_pages, s_drag_acc, s_scrub_last, s_load_x = -1;
static bool s_drag_paged, s_end;
/* message bytes the reader was last laid out with, and the transfer's state then */
static uint32_t s_loaded_got;
static detail_state_t s_loaded_st;

static void show(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static const session_t *session(void) { return model_find(s_sid); }

/* The whole message as far as it has arrived, else its preview. */
static const char *text(void)
{
    const session_t *s = session();
    const char *t = detail_msg_text(s_sid, s_lm);
    return t ? t : s ? s->lp : "";
}

static bool complete(void) { return detail_msg_state(s_sid, s_lm) == DETAIL_DONE; }
static bool failed(void) { return detail_msg_state(s_sid, s_lm) == DETAIL_FAILED; }

/* Button B's hint, by the word it had: the glyph is the vocabulary's. */
static void set_b(const char *word) { ui_key_hint_set(s_hint[1], theme_glyph(word)); }

/* How much of the message is here: a ring that fills beside the page dots (no total known yet: an
 * empty ring), the error glyph when the rest did not load, nothing once it is all here. */
static void loading(void)
{
    bool done = complete(), bad = failed();
    int shown_pips = s_pages > UI_PIPS_MAX ? UI_PIPS_MAX : s_pages;
    int x = shown_pips > 1 ? UI_CX + (shown_pips - 1) * UI_PIPS_PITCH / 2 + LOAD_GAP : UI_CX;
    if (x != s_load_x) { /* the ring and the glyph follow the end of the dots */
        s_load_x = x;
        lv_obj_set_pos(s_load, x - LOAD_HALF, PIPS_Y - LOAD_HALF);
        ui_glyph_set_pos(s_load_err, x, PIPS_Y);
    }
    show(s_load, !done && !bad);
    show(s_load_err, bad);
    if (done || bad) return;
    uint32_t got = detail_msg_got(s_sid, s_lm), total = detail_msg_total(s_sid, s_lm);
    ui_arc_set(lv_obj_get_child(s_load, 0), total ? (int)((uint64_t)got * 1000 / total) : 0,
               COL_TEXT, COL_TEXT3);
}

static void page(void)
{
    int lh = lv_font_get_line_height(FONT_BODY);
    lv_obj_set_y(s_lbl, -s_page_i * READ_LINES * lh);
    bool last = s_page_i == s_pages - 1, done = complete() || failed();
    ui_pips_set(s_pips, s_pages, s_page_i);
    loading();
    set_b(last && done ? "Reply" : "More");
    s_page_ms = s_now;
}

/* Measures the text again (it grows as the message arrives); the page on screen stays. */
static void load(void)
{
    const char *t = text();
    int lh = lv_font_get_line_height(FONT_BODY);
    lv_point_t sz;
    lv_text_get_size(&sz, t, FONT_BODY, 0, 0, READ_W, LV_TEXT_FLAG_NONE);
    int lines = sz.y / lh;
    /* no page of blank lines at the end */
    for (size_t n = strlen(t); n > 0 && t[n - 1] == '\n' && lines > 1; n--) lines--;
    s_pages = lines <= READ_LINES ? 1 : (lines + READ_LINES - 1) / READ_LINES;
    if (s_page_i >= s_pages) s_page_i = s_pages - 1;
    s_loaded_got = detail_msg_got(s_sid, s_lm);
    s_loaded_st = detail_msg_state(s_sid, s_lm);
    lv_label_set_text_static(s_lbl, t);
}

/* The discs of the end, the selected one in its colour. */
static void end_draw(void)
{
    const session_t *s = session();
    ui_orbit_item_t o[END_MAX];
    for (int i = 0; i < s_n; i++) o[i] = (ui_orbit_item_t){s_items[i].glyph, s_items[i].rgb, false};
    if (s) ui_orbit_set_accent(s_orbit, theme_session_rgb(s));
    ui_orbit_set_items(s_orbit, o, s_n);
    ui_orbit_select(s_orbit, s_sel);
}

/* The end of the message: Dictate and Reply (what the user came for), Read again, Open on PC. */
static void end_open(void)
{
    const session_t *s = session();
    s_n = 0;
    if (model_voice()) s_items[s_n++] = (end_item_t){"Dictate", ICON_MIC, RGB_ASK, END_DICTATE};
    if (model_can_type() && (model_reply_count() > 0 || (s && s->n_rp > 0) || model_keys()))
        s_items[s_n++] = (end_item_t){"Reply", ICON_REPLY, RGB_ASK, END_REPLY};
    s_items[s_n++] = (end_item_t){"Read again", ICON_READ, 0, END_AGAIN};
    s_items[s_n++] = (end_item_t){"Open on PC", ICON_COMPUTER, 0, END_OPEN};
    s_sel = 0;
    end_draw();
    /* the message's end stays in view under the sigil: what is being answered */
    const char *t = text();
    size_t len = strlen(t);
    while (len && (t[len - 1] == '\n' || t[len - 1] == ' ')) len--;
    size_t from = len > 60 ? len - 60 : 0;
    /* never mid-character */
    while (from && from < len && ((uint8_t)t[from] & 0xc0) == 0x80) from++;
    const char *nl = memchr(t + from, '\n', len - from);
    /* start on a line of its own */
    if (from && nl && (size_t)(nl - t) + 1 < len) from = (size_t)(nl - t) + 1;
    while (from < len && (t[from] == '\n' || t[from] == ' ')) from++;
    snprintf(s_tail_txt, sizeof s_tail_txt, "%s%.*s", from ? "\xE2\x80\xA6" : "", (int)(len - from),
             t + from);
    lv_label_set_text_static(s_tail, s_tail_txt);
    show(s_page_box, false);
    show(s_end_box, true);
    s_end = true;
    s_page_ms = s_now;
    set_b("Next");
    snprintf(s_title_txt, sizeof s_title_txt, "Answer it");
    dbg_event("ui", "read_end", "\"sid\":%u,\"items\":%d", (unsigned)s_sid, s_n);
}

static void end_close(void)
{
    s_end = false;
    show(s_end_box, false);
    show(s_page_box, true);
    snprintf(s_title_txt, sizeof s_title_txt, "Last message");
    page();
}

bool ui_read_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = session();
    /* a new prompt clears the message; a new stop replaces it: either way this text is no longer
     * the one */
    if (!s || s->state == SESS_ENDED || !s->lm || s->lm != s_lm || !s->lp[0]) return false;
    const char *base = theme_basename(s->repo);
    char host[24], marks[sizeof s_marks_txt];
    theme_host_prefix(host, sizeof host, s);
    /* who said it, in words: the agent and the project; on the glass the sigil and the agent's
     * mark */
    snprintf(s_header_txt, sizeof s_header_txt, "%s%s%s%s", host, theme_agent_name(s->agent),
             theme_sep(), base[0] ? base : "(no folder)");
    ui_sigil_set_name(s_sigil, base);
    ui_sigil_set_name(s_end_sigil, base);
    snprintf(marks, sizeof marks, "%s%s", s->host ? ICON_G_WEB " " : "",
             theme_agent_glyph(s->agent));
    theme_set_text(s_marks, s_marks_txt, sizeof s_marks_txt, marks);
    if (s_end) end_draw(); /* its colour follows the session */
    return true;
}

void ui_read_open(uint32_t sid, uint32_t now)
{
    const session_t *s = model_find(sid);
    s_sid = sid;
    s_lm = s ? s->lm : 0;
    s_now = s_opened_ms = now;
    s_page_i = 0;
    s_end = false;
    s_header_txt[0] = 0;
    show(s_end_box, false);
    show(s_page_box, true);
    snprintf(s_title_txt, sizeof s_title_txt, "Last message");
    if (s) detail_want_msg(s, now); /* the whole of it, from the daemon */
    ui_read_refresh(now);
    load();
    page();
    ui_home_rim_mode(true); /* a rim drag dials through the end picker */
    ring_set_still(ui_home_ring(), true);
    show(s_root, true);
    /* reading what it said is seeing it */
    if (s && s->state == SESS_DONE && !s->seen) g_ui_actions->ack(sid);
    dbg_event("ui", "read", "\"sid\":%u,\"lm\":%u,\"pages\":%d", (unsigned)sid, (unsigned)s_lm,
              s_pages);
}

void ui_read_close(void)
{
    show(s_root, false);
    ring_set_still(ui_home_ring(), !ui_animations_allowed());
}

bool ui_read_back(void)
{
    if (!s_end) return false;
    end_close();
    return true;
}

/* The discs of the end reach past the centre zone (r 150): ui.c lets a touch on one through as a
 * centre touch. */
bool ui_read_hit(int x, int y)
{
    return s_end && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN) &&
           ui_orbit_hit(s_orbit, x, y) >= 0;
}

/* The next page; past the last one (all of it here) the end picker. */
static void next_page(void)
{
    if (s_page_i < s_pages - 1) {
        s_page_i++;
        page();
    } else if (complete() || failed()) {
        end_open();
    } else {
        return; /* the rest is still arriving: the next page appears when it has */
    }
    g_ui_actions->tick();
}

static void end_select(int i, const char *via, void (*haptic)(void))
{
    if (!s_n) return;
    i = ((i % s_n) + s_n) % s_n;
    if (i == s_sel) return;
    s_sel = i;
    ui_orbit_select(s_orbit, i);
    dbg_event("ui", "item", "\"idx\":%d,\"via\":\"%s\"", i, via);
    if (haptic) haptic();
}

static void end_commit(const end_item_t *it)
{
    const session_t *s = session();
    if (!it || !s) return;
    dbg_event("ui", "commit", "\"sid\":%u,\"id\":%d", (unsigned)s_sid, it->id);
    g_ui_actions->tick();
    switch (it->id) {
    case END_DICTATE: /* typed into the session, or queued if it is working again (proto 0.17) */
        ui_go_voice(0,
                    (s->state == SESS_WORK || s->state == SESS_START) && model_peer_minor() >= 17);
        break;
    case END_REPLY: ui_go_replies(); break;
    case END_AGAIN:
        s_page_i = 0;
        end_close();
        break;
    case END_OPEN:
        g_ui_actions->focus(s_sid);
        ui_go_home();
        break;
    }
}

bool ui_read_act(nav_action_t a, const gesture_evt_t *g)
{
    switch (a) {
    case NAV_HOLD_PRESS:
        s_drag_acc = 0;
        s_drag_paged = false;
        return true;
    case NAV_HOLD_RELEASE: return true;
    case NAV_HOLD_LONG:
        /* held on a disc: nothing */
        if (s_end && ui_orbit_hit(s_orbit, g->x, g->y) >= 0) return true;
        ui_go_back(); /* held still: the end picker closes first, then the reader */
        return true;
    case NAV_FOCUS:
        g_ui_actions->focus(s_sid);
        ui_go_home();
        return true;
    case NAV_COMMIT:
        /* the tap that opened it, or one right behind a page or the picker appearing, means
         * nothing */
        if (g->kind == G_TAP &&
            (s_now - s_page_ms < NAV_CARD_ARM_MS || s_now - s_opened_ms < NAV_CARD_ARM_MS))
            return true;
        if (!s_end) {
            next_page();
            return true;
        }
        {
            int hit = g->kind == G_TAP ? ui_orbit_hit(s_orbit, g->x, g->y) : -1;
            /* the disc you touch is the one you get */
            if (hit >= 0 && hit != s_sel) end_select(hit, "tap", NULL);
            end_commit(s_sel < s_n ? &s_items[s_sel] : NULL);
        }
        return true;
    case NAV_NEXT:
        if (!s_end) next_page();
        else end_select(s_sel + 1, "btn", g_ui_actions->tick);
        return true;
    case NAV_SCROLL_DRAG: {
        int d = nav_list_drag(&s_drag_acc, g->dy);
        if (!d) return true;
        if (s_end) {
            end_select(s_sel + d, "drag", g_ui_actions->detent);
            return true;
        }
        if (s_drag_paged) return true; /* one page per touch */
        s_drag_paged = true;
        if (d > 0) next_page();
        else if (s_page_i > 0) {
            s_page_i--;
            page();
            g_ui_actions->tick();
        }
        return true;
    }
    case NAV_SCRUB: {
        int d = nav_list_scrub(&s_scrub_last, g->kind, g->x);
        if (d && s_end) end_select(s_sel + d, "scrub", g_ui_actions->detent);
        return true;
    }
    default: return false;
    }
}

void ui_read_tick(uint32_t now)
{
    s_now = now;
    const session_t *s = session();
    if (s) detail_want_msg(s, now); /* resumes a transfer a lost link interrupted */
    if (!s_end && (detail_msg_got(s_sid, s_lm) != s_loaded_got ||
                   detail_msg_state(s_sid, s_lm) != s_loaded_st)) {
        load(); /* more of the message arrived */
        page();
    }
}

void ui_read_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "read");
    r->has_sel_sid = true;
    r->sel_sid = s_sid;
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    ui_report_add(r, "%s", s_title_txt);
    if (s_end) {
        for (int k = 0; k < s_n && k < 4; k++)
            ui_report_add(r, "%s%s", k == 0 ? "> " : "", s_items[(s_sel + k) % s_n].label);
        return;
    }
    ui_report_add(r, "Reading %d of %d", s_page_i + 1, s_pages);
    if (!complete()) ui_report_add(r, failed() ? "Not loaded" : "Loading");
    /* the first visible line of the page, so a script can tell what is on screen */
    const char *t = text();
    for (int skip = s_page_i * READ_LINES; skip > 0 && *t; skip--) {
        /* by line breaks: close enough for a check */
        const char *nl = strchr(t, '\n');
        if (!nl) break;
        t = nl + 1;
    }
    ui_report_add(r, "%.40s", t);
}

/* A see-through parent: a view of the screen, or a window over a part. */
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *ui_read_init(lv_obj_t *screen)
{
    int lh = lv_font_get_line_height(FONT_BODY), clh = lv_font_get_line_height(FONT_CAPTION);
    s_root = box(screen, 0, 0, UI_SIZE, UI_SIZE);

    /* the reader: who said it, the text, where in it */
    s_page_box = box(s_root, 0, 0, UI_SIZE, UI_SIZE);
    s_sigil = ui_sigil_create(s_page_box, UI_CX, SIGIL_Y, SIGIL_R);
    s_marks = lv_label_create(s_page_box);
    lv_obj_set_style_text_font(s_marks, FONT_CAPTION, 0);
    lv_obj_set_style_text_color(s_marks, COL_TEXT3, 0);
    lv_obj_set_style_text_align(s_marks, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(s_marks, 120, clh);
    lv_obj_set_pos(s_marks, UI_CX - 60, MARK_Y - clh / 2);
    lv_label_set_text(s_marks, "");
    /* a window READ_LINES tall over the whole message */
    s_read = box(s_page_box, UI_CX - READ_W / 2, READ_Y, READ_W, READ_LINES * lh);
    s_lbl = lv_label_create(s_read);
    lv_obj_set_style_text_font(s_lbl, FONT_BODY, 0);
    lv_obj_set_style_text_color(s_lbl, COL_TEXT, 0);
    /* prose and lists read from a left edge */
    lv_obj_set_style_text_align(s_lbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(s_lbl, READ_W);
    lv_label_set_text_static(s_lbl, "");
    s_pips = ui_pips_create(s_page_box, UI_CX, PIPS_Y);
    /* the loading ring is an arc in a small parent that moves it to the end of the dots */
    s_load = box(s_page_box, UI_CX - LOAD_HALF, PIPS_Y - LOAD_HALF, 2 * LOAD_HALF + 1,
                 2 * LOAD_HALF + 1);
    ui_arc_create(s_load, LOAD_HALF, LOAD_HALF, LOAD_R, LOAD_W);
    s_load_err = ui_glyph_create(s_page_box, FONT_ICON_S, UI_CX, PIPS_Y);
    ui_glyph_set(s_load_err, ICON_ERROR, lv_color_hex(RGB_ERROR));
    lv_obj_add_flag(s_load, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_load_err, LV_OBJ_FLAG_HIDDEN);

    /* the end: the actions arched over the sigil, the message's last lines under it */
    s_end_box = box(s_root, 0, 0, UI_SIZE, UI_SIZE);
    s_orbit = ui_orbit_create(s_end_box);
    ui_orbit_set_arch(s_orbit, ARCH_SPAN, ARCH_PITCH);
    s_end_sigil = ui_sigil_create(s_end_box, UI_CX, END_SIGIL_Y, END_SIGIL_R);
    s_tail = lv_label_create(s_end_box);
    lv_obj_set_style_text_font(s_tail, FONT_BODY, 0);
    lv_obj_set_style_text_color(s_tail, COL_TEXT2, 0);
    lv_obj_set_style_text_align(s_tail, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_tail, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s_tail, TAIL_W, TAIL_LINES * lh);
    lv_obj_set_pos(s_tail, UI_CX - TAIL_W / 2, TAIL_Y);
    lv_label_set_text_static(s_tail, "");
    lv_obj_add_flag(s_end_box, LV_OBJ_FLAG_HIDDEN);

    /* rule 6: A opens the session on the computer, as on the deck; B reads on */
    s_hint[0] = ui_key_hint_create(s_root, UI_KEY_A);
    ui_key_hint_set(s_hint[0], ICON_COMPUTER);
    s_hint[1] = ui_key_hint_create(s_root, UI_KEY_B);
    ui_key_hint_set(s_hint[1], ICON_CARET_DOWN);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
