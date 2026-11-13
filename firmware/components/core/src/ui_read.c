/* The READ screen (LAST-MESSAGE.md §1): the agent's last message in the paged reader, fetched from the daemon
 * as it is read (detail.c, proto 0.18), then Dictate / Reply at its end. Nothing here sends anything to the agent:
 * a page turn only reads, and the end picker only opens VOICE or the Reply list, whose own holds and taps send.
 * Until the whole message has arrived the reader shows what it has (the preview first) and says it is loading. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "detail.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "ui_wheel.h"

#define CAPTION_Y 58 /* as ASK: caption, title, then the text */
#define TITLE_Y 80
#define READ_Y 112
#define READ_W 320
#define READ_LINES 7
#define END_WHEEL WHEEL_LAYOUT_BUTTON_TALL(280)
#define END_TEXT_Y 132 /* the end picker: the message's last line(s) above it, as a reminder */

enum { END_DICTATE = 1, END_REPLY, END_AGAIN, END_OPEN };

static lv_obj_t *s_root, *s_header, *s_title, *s_read, *s_lbl, *s_page, *s_tail, *s_wheel;
static lv_obj_t *s_badge[2], *s_key[2], *s_wlbl[2];
static char s_header_txt[64], s_title_txt[40], s_page_txt[64], s_tail_txt[120];
static uint32_t s_sid, s_lm, s_now, s_page_ms, s_opened_ms;
static int s_page_i, s_pages, s_drag_acc, s_scrub_last;
static bool s_drag_paged, s_end;
static uint32_t s_loaded_got; /* message bytes the reader was last laid out with, and the transfer's state then */
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

static void set_b(const char *word) { theme_key_badge_set_word(s_badge[1], s_wlbl[1], word, UI_BADGE_B_DEG, UI_BADGE_R); }

static void page(void)
{
    int lh = lv_font_get_line_height(FONT_BODY);
    lv_obj_set_y(s_lbl, -s_page_i * READ_LINES * lh);
    bool last = s_page_i == s_pages - 1, done = complete() || failed();
    if (!done) snprintf(s_page_txt, sizeof s_page_txt, "%d of %d%s", s_page_i + 1, s_pages, last ? " · loading" : "+");
    else if (failed()) snprintf(s_page_txt, sizeof s_page_txt, "%d of %d · the rest did not load", s_page_i + 1, s_pages);
    else snprintf(s_page_txt, sizeof s_page_txt, "%d of %d", s_page_i + 1, s_pages);
    lv_label_set_text(s_page, s_pages > 1 || !done ? s_page_txt : "");
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
    for (size_t n = strlen(t); n > 0 && t[n - 1] == '\n' && lines > 1; n--) lines--; /* no page of blank lines at the end */
    s_pages = lines <= READ_LINES ? 1 : (lines + READ_LINES - 1) / READ_LINES;
    if (s_page_i >= s_pages) s_page_i = s_pages - 1;
    s_loaded_got = detail_msg_got(s_sid, s_lm);
    s_loaded_st = detail_msg_state(s_sid, s_lm);
    lv_label_set_text_static(s_lbl, t);
}

/* The end of the message: Dictate and Reply (what the user came for), Read again, Open on PC. */
static void end_open(void)
{
    const session_t *s = session();
    wheel_item_t w[4];
    int n = 0;
    if (model_voice()) w[n++] = (wheel_item_t){"Dictate", RGB_ASK, false, END_DICTATE, NULL};
    if (model_can_type() && (model_reply_count() > 0 || (s && s->n_rp > 0) || model_keys()))
        w[n++] = (wheel_item_t){"Reply", RGB_ASK, false, END_REPLY, NULL};
    w[n++] = (wheel_item_t){"Read again", 0, false, END_AGAIN, NULL};
    w[n++] = (wheel_item_t){"Open on PC", 0, false, END_OPEN, NULL};
    wheel_set(s_wheel, w, n, 0);
    wheel_set_shown(s_wheel, true);
    /* the message's end stays in view above the choices: what is being answered */
    const char *t = text();
    size_t len = strlen(t);
    while (len && (t[len - 1] == '\n' || t[len - 1] == ' ')) len--;
    size_t from = len > 60 ? len - 60 : 0;
    while (from && from < len && ((uint8_t)t[from] & 0xc0) == 0x80) from++; /* never mid-character */
    const char *nl = memchr(t + from, '\n', len - from);
    if (from && nl && (size_t)(nl - t) + 1 < len) from = (size_t)(nl - t) + 1; /* start on a line of its own */
    while (from < len && (t[from] == '\n' || t[from] == ' ')) from++;
    snprintf(s_tail_txt, sizeof s_tail_txt, "%s%.*s", from ? "\xE2\x80\xA6" : "", (int)(len - from), t + from);
    lv_label_set_text_static(s_tail, s_tail_txt);
    show(s_read, false);
    show(s_page, false);
    show(s_tail, true);
    s_end = true;
    s_page_ms = s_now;
    set_b("Next");
    theme_set_text(s_title, s_title_txt, sizeof s_title_txt, "Answer it");
    dbg_event("ui", "read_end", "\"sid\":%u,\"items\":%d", (unsigned)s_sid, n);
}

static void end_close(void)
{
    s_end = false;
    wheel_set_shown(s_wheel, false);
    show(s_tail, false);
    show(s_read, true);
    show(s_page, true);
    theme_set_text(s_title, s_title_txt, sizeof s_title_txt, "Last message");
    page();
}

bool ui_read_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = session();
    /* a new prompt clears the message; a new stop replaces it: either way this text is no longer the one */
    if (!s || s->state == SESS_ENDED || !s->lm || s->lm != s_lm || !s->lp[0]) return false;
    char buf[80];
    const char *base = theme_basename(s->repo);
    char host[24];
    theme_host_prefix(host, sizeof host, s);
    snprintf(buf, sizeof buf, "%s%s%s%s", host, theme_agent_name(s->agent), theme_sep(), base[0] ? base : "(no folder)");
    theme_set_text(s_header, s_header_txt, sizeof s_header_txt, buf);
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
    wheel_set(s_wheel, NULL, 0, 0);
    wheel_set_shown(s_wheel, false);
    show(s_tail, false);
    show(s_read, true);
    show(s_page, true);
    theme_set_text(s_title, s_title_txt, sizeof s_title_txt, "Last message");
    if (s) detail_want_msg(s, now); /* the whole of it, from the daemon */
    ui_read_refresh(now);
    load();
    page();
    ui_home_rim_mode(true); /* a rim drag dials through the end picker */
    ring_set_still(ui_home_ring(), true);
    show(s_root, true);
    /* reading what it said is seeing it */
    if (s && s->state == SESS_DONE && !s->seen) g_ui_actions->ack(sid);
    dbg_event("ui", "read", "\"sid\":%u,\"lm\":%u,\"pages\":%d", (unsigned)sid, (unsigned)s_lm, s_pages);
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
    int n = wheel_count(s_wheel);
    if (!n) return;
    i = ((i % n) + n) % n;
    if (i == wheel_sel(s_wheel)) return;
    wheel_select(s_wheel, i);
    dbg_event("ui", "item", "\"idx\":%d,\"via\":\"%s\"", i, via);
    if (haptic) haptic();
}

static void end_commit(const wheel_item_t *it)
{
    const session_t *s = session();
    if (!it || !s) return;
    dbg_event("ui", "commit", "\"sid\":%u,\"id\":%d", (unsigned)s_sid, it->id);
    g_ui_actions->tick();
    switch (it->id) {
    case END_DICTATE: /* typed into the session, or queued if it is working again (proto 0.17) */
        ui_go_voice(0, (s->state == SESS_WORK || s->state == SESS_START) && model_peer_minor() >= 17);
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
        if (s_end && wheel_hit(s_wheel, g->x, g->y) >= 0) return true; /* held on a row: nothing */
        ui_go_back(); /* held still: the end picker closes first, then the reader */
        return true;
    case NAV_FOCUS:
        g_ui_actions->focus(s_sid);
        ui_go_home();
        return true;
    case NAV_COMMIT:
        /* the tap that opened it, or one right behind a page or the picker appearing, means nothing */
        if (g->kind == G_TAP && (s_now - s_page_ms < NAV_CARD_ARM_MS || s_now - s_opened_ms < NAV_CARD_ARM_MS)) return true;
        if (!s_end) {
            next_page();
            return true;
        }
        {
            int hit = g->kind == G_TAP ? wheel_hit(s_wheel, g->x, g->y) : -1;
            if (hit >= 0 && hit != wheel_sel(s_wheel)) end_select(hit, "tap", NULL);
            end_commit(wheel_item(s_wheel, wheel_sel(s_wheel)));
        }
        return true;
    case NAV_NEXT:
        if (!s_end) next_page();
        else end_select(wheel_sel(s_wheel) + 1, "btn", g_ui_actions->tick);
        return true;
    case NAV_SCROLL_DRAG: {
        int d = nav_list_drag(&s_drag_acc, g->dy);
        if (!d) return true;
        if (s_end) {
            end_select(wheel_sel(s_wheel) + d, "drag", g_ui_actions->detent);
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
        if (d && s_end) end_select(wheel_sel(s_wheel) + d, "scrub", g_ui_actions->detent);
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
    if (!s_end && (detail_msg_got(s_sid, s_lm) != s_loaded_got || detail_msg_state(s_sid, s_lm) != s_loaded_st)) {
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
        int n = wheel_count(s_wheel), sel = wheel_sel(s_wheel);
        for (int k = 0; k < n && k < 4; k++) ui_report_add(r, "%s%s", k == 0 ? "> " : "", wheel_item(s_wheel, (sel + k) % n)->label);
        return;
    }
    ui_report_add(r, "Reading %d of %d", s_page_i + 1, s_pages);
    if (!complete()) ui_report_add(r, failed() ? "Not loaded" : "Loading");
    /* the first visible line of the page, so a script can tell what is on screen */
    const char *t = text();
    for (int skip = s_page_i * READ_LINES; skip > 0 && *t; skip--) { /* by line breaks: close enough for a check */
        const char *nl = strchr(t, '\n');
        if (!nl) break;
        t = nl + 1;
    }
    ui_report_add(r, "%.40s", t);
}

static lv_obj_t *line(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, int y, int w, int lines)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_size(l, w, lines * lv_font_get_line_height(f));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, UI_CX - w / 2, y);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ui_read_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_header = line(s_root, FONT_CAPTION, COL_TEXT2, CAPTION_Y, theme_chord(CAPTION_Y + 10, 20), 1);
    s_title = line(s_root, FONT_HEAD, COL_TEXT, TITLE_Y, theme_chord(TITLE_Y + 15, 20), 1);
    s_read = lv_obj_create(s_root); /* a window READ_LINES tall over the whole message */
    lv_obj_remove_style_all(s_read);
    lv_obj_remove_flag(s_read, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_read, READ_W, READ_LINES * lv_font_get_line_height(FONT_BODY));
    lv_obj_set_pos(s_read, UI_CX - READ_W / 2, READ_Y);
    s_lbl = lv_label_create(s_read);
    lv_obj_set_style_text_font(s_lbl, FONT_BODY, 0);
    lv_obj_set_style_text_color(s_lbl, COL_TEXT, 0);
    lv_obj_set_style_text_align(s_lbl, LV_TEXT_ALIGN_LEFT, 0); /* prose and lists read from a left edge */
    lv_obj_set_width(s_lbl, READ_W);
    lv_label_set_text_static(s_lbl, "");
    s_page = line(s_root, FONT_CAPTION, COL_TEXT2, READ_Y + READ_LINES * lv_font_get_line_height(FONT_BODY) + 6, 300, 1);
    s_tail = line(s_root, FONT_BODY, COL_TEXT2, END_TEXT_Y, 320, 2);
    show(s_tail, false);
    s_wheel = wheel_create_at(s_root, END_WHEEL);
    wheel_set_shown(s_wheel, false);
    static const char *const words[2] = {"Open", "More"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_wlbl[i]);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
