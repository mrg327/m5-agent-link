/* The SCROLL screen in Sigil (docs/design/GLYPHS.md §5; UI-V2 §4.4 for what it does): the centre is
 * a trackpad for the focused terminal's scrollback. A drag step is one wheel line, a release at
 * speed glides on, B pages, A jumps back to the live bottom. The first step on an idle link goes
 * out at once, later ones are summed into one cmd_op_scroll per NAV_SCROLL_COALESCE_MS; the
 * daemon's scroll_pos corrects the position and (privacy >= preview) brings the line at the top of
 * the view.
 *
 * What is on the glass:
 *   - whose terminal: the project's sigil and the agent's mark under it, as on the deck;
 *   - the scrollback as a column of bars, one a line, each as long as its line number makes it, so
 *     the column moves with the finger like the text it stands for. The row in the middle is where
 *     the view is: the terminal's own line when the daemon sends one (text: terminal content), a
 *     bright bar otherwise. Rows under it are the lines down to the live bottom; there are none
 *     when the view is live, and the live glyph stands there instead, in the session's colour. Rows
 *     above stop at the top of the scrollback;
 *   - how far up, as geometry (rule 4): a track bent along the left of the screen with a thumb, at
 *     the lower end when live. It is on the left because the finger comes in from the right. No
 *     number;
 *   - the hints: A is the live glyph (back to the bottom, and out), B a page down; neither while
 *     live.
 * The report still says "[142] lines up" / "Live" and the preview (rule 9). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "hal.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"

/* Geometry: centres. */
#define SIGIL_Y 50
#define SIGIL_R 22
#define MARK_Y 84     /* the agent's mark, as on the deck */
#define VIEW_Y 228    /* the row where the view is: the preview line, or a bright bar */
#define PREVIEW_W 330 /* from the column's left edge: it is a line of the column */
#define ROWS 5        /* bars above the view, and as many under it */
#define ROW_PITCH 18
#define ROW_GAP 28 /* from the view row to the first bar */
#define BAR_H 6
#define BAR_X 100     /* a bar starts here plus its indent ... */
#define BAR_INDENT 16 /* ... of 0 to 3 of these ... */
#define BAR_MIN 56    /* ... and is this long at least, */
#define BAR_VAR 144   /* this much longer at most: it ends at x 348 or before */
#define MAP_X0 (BAR_X - 4)
#define MAP_X1 (BAR_X + 3 * BAR_INDENT + BAR_MIN + BAR_VAR + 4)
#define MAP_HALF (ROW_GAP + (ROWS - 1) * ROW_PITCH + BAR_H) /* the column's half height */
#define LIVE_Y 300                                          /* the live glyph, under the view row */
/* the position track: left of the column, from 235 degrees (live) up to 305 */
#define TRACK_R 182
#define TRACK_W 10
#define TRACK_START 235
#define TRACK_SPAN 70
#define THUMB_LEN 150   /* per mille of the track */
#define STOP_GAP_MS 400 /* the end-of-scrollback thud repeats at most this often */
/* total unknown: the thumb sits at lines/(lines+this), so 100 lines = half way */
#define UNKNOWN_TOTAL_SOFT 100

static int s_fast_port = -1; /* the port asked for the short interval, -1 none */
static lv_obj_t *s_root, *s_sigil, *s_marks, *s_map, *s_live, *s_preview, *s_track, *s_key[2];
static uint32_t s_sid, s_now, s_last_input;
static char s_header_txt[64], s_count_txt[24], s_preview_txt[64], s_marks_txt[24];

/* position: the daemon's last word, nudged locally by what we sent since */
static uint32_t s_lines_above, s_total;
static bool s_dragged; /* a drag step went out during the current touch */
static bool s_at_bottom = true, s_has_total, s_have_pos;
/* what the column shows now: it is repainted only when one of these changes */
static uint32_t s_map_lines, s_map_top;
static bool s_map_text;
/* coalescer: pending lines or pages (pages win); one command per NAV_SCROLL_COALESCE_MS */
static int s_pend_lines, s_pend_pages;
static uint32_t s_sent_ms;
static bool s_sent_any;
static int s_detent_lines; /* finger-driven lines since the last detent */
/* glide: velocity (px/s, up < 0), sub-step travel carried between ticks, clock */
static float s_glide_v, s_glide_px;
static uint32_t s_glide_ms, s_glide_t0;
static bool s_caught;  /* the current touch stopped a glide: its tap must not leave */
static int s_last_dir; /* -1 up (older), +1 down (newer): which end can "stop" */
static uint32_t s_stop_ms;

static const session_t *session(void) { return model_find(s_sid); }

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static bool preview_allowed(void) { return model_settings()->privacy != PRIVACY_META; }
static bool live(void) { return s_at_bottom || s_lines_above == 0; }

/* ───────── the column of bars ───────── */

/* A line's bar, from its number: the same line is the same bar wherever it is on screen, so the
 * column moves as a whole when the view does. An indent and a length, like a line of a program. */
static void bar_of(uint32_t line, int *x, int *w)
{
    uint32_t h = line * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    *x = BAR_X + (int)(h & 3) * BAR_INDENT;
    *w = BAR_MIN + (int)((h >> 8) % (BAR_VAR + 1));
}

static void bar(lv_layer_t *l, int x, int cy, int w, int h, lv_color_t c)
{
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_COVER;
    r.bg_color = c;
    r.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {x, cy - h / 2, x + w - 1, cy + h / 2 - 1};
    lv_draw_rect(l, &r, &a);
}

/* Row k above the view (k > 0) is line lines_above + k, an older one; row k under it is a newer
 * one, down to line 0, the live bottom. The further from the view, the dimmer. */
static void on_map_draw(lv_event_t *e)
{
    static const uint8_t shade[ROWS] = {0x58, 0x48, 0x38, 0x2a, 0x1e};
    lv_layer_t *l = lv_event_get_layer(e);
    int x, w;
    for (int k = 1; k <= ROWS; k++) {
        lv_color_t c = lv_color_make(shade[k - 1], shade[k - 1], (uint8_t)(shade[k - 1] + 4));
        int dy = ROW_GAP + (k - 1) * ROW_PITCH;
        uint32_t up = s_map_lines + (uint32_t)k;
        if (!s_map_top || up <= s_map_top) { /* nothing above the top of the scrollback */
            bar_of(up, &x, &w);
            bar(l, x, VIEW_Y - dy, w, BAR_H, c);
        }
        if (s_map_lines >= (uint32_t)k) { /* nothing under the live bottom */
            bar_of(s_map_lines - (uint32_t)k, &x, &w);
            bar(l, x, VIEW_Y + dy, w, BAR_H, c);
        }
    }
    if (!s_map_text) {
        /* no line to show (privacy, or none sent): the view row is the brightest bar */
        bar_of(s_map_lines, &x, &w);
        bar(l, x, VIEW_Y, w, BAR_H + 2, COL_TEXT);
    }
}

static void render(void)
{
    const session_t *s = session();
    uint32_t rgb = s ? theme_session_rgb(s) : RGB_ACCENT;
    bool at_live = live();
    show(s_live, at_live);
    /* at the bottom neither Bottom nor Page does anything */
    for (int i = 0; i < 2; i++) show(s_key[i], !(at_live && s_have_pos));
    if (at_live) {
        ui_glyph_set(s_live, ICON_LIVE, lv_color_hex(rgb));
        s_count_txt[0] = 0;
    } else {
        /* in words: the report */
        snprintf(s_count_txt, sizeof s_count_txt, "%u", (unsigned)s_lines_above);
    }
    bool prev = preview_allowed() && s_preview_txt[0];
    show(s_preview, prev);
    /* the column: only when the view moved, the top became known or the row's text came or went */
    uint32_t lines = at_live ? 0 : s_lines_above, top = s_has_total && s_total > 0 ? s_total : 0;
    if (lines != s_map_lines || top != s_map_top || prev != s_map_text) {
        s_map_lines = lines;
        s_map_top = top;
        s_map_text = prev;
        lv_obj_invalidate(s_map);
    }
    /* the thumb: how far above the bottom the view sits */
    int permille = 0;
    if (!at_live) {
        if (s_has_total && s_total > 0) permille = (int)((uint64_t)s_lines_above * 1000 / s_total);
        else
            permille = (int)((uint64_t)s_lines_above * 1000 / (s_lines_above + UNKNOWN_TOTAL_SOFT));
    }
    ui_track_set(s_track, permille, THUMB_LEN, lv_color_hex(rgb), COL_FILL2);
}

bool ui_scroll_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = session();
    if (!s || s->state == SESS_ENDED) return false;
    const char *base = theme_basename(s->repo);
    char host[24], marks[24];
    theme_host_prefix(host, sizeof host, s);
    snprintf(s_header_txt, sizeof s_header_txt, "%s%s%s%s", host, theme_agent_name(s->agent),
             theme_sep(), base[0] ? base : "(no folder)");
    ui_sigil_set_name(s_sigil, base);
    snprintf(marks, sizeof marks, "%s%s", s->host ? ICON_G_WEB " " : "",
             theme_agent_glyph(s->agent)); /* remote: the globe first */
    theme_set_text(s_marks, s_marks_txt, sizeof s_marks_txt, marks);
    render();
    return true;
}

void ui_scroll_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_now = s_last_input = now;
    s_dragged = false;
    s_lines_above = s_total = 0;
    s_at_bottom = true;
    s_has_total = s_have_pos = false;
    s_pend_lines = s_pend_pages = 0;
    s_sent_any = s_caught = false;
    s_detent_lines = 0;
    s_glide_v = 0;
    s_last_dir = 0;
    s_stop_ms = 0;
    s_preview_txt[0] = 0;
    lv_label_set_text(s_preview, "");
    if (g_ui_actions->sectors) g_ui_actions->sectors(0); /* the rim is unused here */
    /* ... and says so: one grey ring, no sessions to scrub */
    ring_set_track(ui_home_ring(), 0, RGB_ACCENT);
    ui_scroll_refresh(now);
    show(s_root, true);
    s_fast_port = ui_link_transport();
    if (s_fast_port >= 0) hal_link_interactive((hal_link_kind_t)s_fast_port, true);
    dbg_event("ui", "scroll", "\"sid\":%u,\"preview_allowed\":%s", (unsigned)sid,
              preview_allowed() ? "true" : "false");
}

void ui_scroll_close(void)
{
    s_glide_v = 0;
    if (s_fast_port >= 0) hal_link_interactive((hal_link_kind_t)s_fast_port, false);
    s_fast_port = -1;
    show(s_root, false);
    ring_set_still(ui_home_ring(), !ui_animations_allowed());
}

/* One command for whatever is pending; pages take precedence over the lines dragged before them. */
static void flush(void)
{
    if (!s_pend_lines && !s_pend_pages) return;
    if (s_pend_pages) g_ui_actions->scroll(s_sid, SCROLL_PAGES, s_pend_pages);
    else g_ui_actions->scroll(s_sid, SCROLL_LINES, s_pend_lines);
    s_pend_lines = s_pend_pages = 0;
    s_sent_ms = s_now;
    s_sent_any = true;
}

/* Leading edge: an idle link sends at once; within the window the steps wait and are summed. */
static void maybe_flush(void)
{
    if (!s_sent_any || s_now - s_sent_ms >= NAV_SCROLL_COALESCE_MS) flush();
}

static void moved(int dir)
{
    s_last_dir = dir;
    s_last_input = s_now;
}

/* The scrollback ends here: one thud, rate limited, instead of a detent. */
static void stop_haptic(void)
{
    if (s_stop_ms && s_now - s_stop_ms < STOP_GAP_MS) return;
    s_stop_ms = s_now;
    g_ui_actions->scroll_stop();
}

static void glide_stop(void) { s_glide_v = s_glide_px = 0; }

static int clamp(int v, int lim) { return v < -lim ? -lim : v > lim ? lim : v; }

/* Lines from the finger (detents, like wheel notches) or from the glide (silent). */
static void push(int lines, bool finger)
{
    if (!lines) return;
    int dir = lines < 0 ? -1 : 1;
    if (dir > 0 && s_at_bottom && s_have_pos) { /* already live: nothing to scroll down to */
        glide_stop();
        moved(dir);
        stop_haptic();
        render();
        return;
    }
    s_pend_lines = clamp(s_pend_lines + lines, NAV_SCROLL_MAX_LINES);
    /* predicted position until the daemon answers */
    if (lines < 0) s_lines_above += (uint32_t)-lines;
    else s_lines_above = (uint32_t)lines > s_lines_above ? 0 : s_lines_above - (uint32_t)lines;
    s_at_bottom = s_lines_above == 0;
    moved(dir);
    if (finger && (s_detent_lines += lines < 0 ? -lines : lines) >= NAV_SCROLL_DETENT_LINES) {
        s_detent_lines %= NAV_SCROLL_DETENT_LINES;
        g_ui_actions->detent();
    }
    maybe_flush();
    render();
}

static void page(int dir)
{
    glide_stop();
    if (dir > 0 && s_at_bottom && s_have_pos) {
        moved(dir);
        stop_haptic();
        render();
        return;
    }
    s_pend_pages = clamp(s_pend_pages + dir, NAV_SCROLL_MAX_PAGES);
    s_pend_lines = 0; /* a page supersedes the steps still waiting */
    moved(dir);
    g_ui_actions->detent();
    maybe_flush();
    render();
}

static void fling(int vy)
{
    float v = (float)clamp(vy, NAV_GLIDE_MAX);
    if (fabsf(v) < NAV_GLIDE_MIN_START) return;
    s_glide_v = v;
    s_glide_px = 0;
    s_glide_ms = s_glide_t0 = s_now;
    dbg_event("ui", "scroll_glide", "\"v\":%d", (int)v);
}

/* The glide's travel this tick, exactly integrated over the exponential decay, in whole steps. */
static void glide(void)
{
    if (s_glide_v == 0) return;
    uint32_t dt = s_now - s_glide_ms;
    if (!dt) return;
    s_glide_ms = s_now;
    s_last_input = s_now; /* gliding counts as activity for the timeout */
    float k = expf(-(float)dt / NAV_GLIDE_TAU_MS);
    s_glide_px += s_glide_v * NAV_GLIDE_TAU_MS * (1.0f - k) / 1000.0f;
    s_glide_v *= k;
    int steps = (int)(s_glide_px / GESTURE_DRAG_STEP_PX);
    s_glide_px -= (float)(steps * GESTURE_DRAG_STEP_PX);
    if (fabsf(s_glide_v) < NAV_GLIDE_STOP || s_now - s_glide_t0 >= NAV_GLIDE_CAP_MS) glide_stop();
    push(nav_scroll_lines(steps * GESTURE_DRAG_STEP_PX), false);
}

bool ui_scroll_caught(void) { return s_caught; }
bool ui_scroll_dragged(void) { return s_dragged; }

bool ui_scroll_act(nav_action_t a, const gesture_evt_t *g)
{
    if (g->kind != G_LIFT && g->kind != G_SHAKE) s_last_input = s_now;
    switch (a) {
    case NAV_SCROLL_CATCH:
        s_caught = s_glide_v != 0;
        s_dragged = false;
        glide_stop();
        return true;
    case NAV_SCROLL_DRAG:
        s_dragged = true;
        push(nav_scroll_lines(g->dy), true);
        return true;
    case NAV_SCROLL_FLING: fling(g->vy); return true;
    case NAV_PAGE_NEWER: page(1); return true;
    case NAV_TO_BOTTOM:
        glide_stop();
        s_pend_lines = s_pend_pages = 0;
        g_ui_actions->scroll(s_sid, SCROLL_TO_END, 1);
        g_ui_actions->tick();
        ui_go_home();
        return true;
    default: return false;
    }
}

bool ui_scroll_tick(uint32_t now)
{
    s_now = now;
    if (!session()) return true;
    glide();
    if (s_pend_lines || s_pend_pages) maybe_flush();
    return now - s_last_input > NAV_SCROLL_TIMEOUT_MS;
}

void ui_scroll_on_pos(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total,
                      uint32_t total, const char *preview)
{
    if (sid && sid != s_sid) return;
    bool was_bottom = s_have_pos && s_at_bottom;
    s_lines_above = lines_above;
    s_at_bottom = at_bottom;
    s_has_total = has_total;
    s_total = total;
    s_have_pos = true;
    if (preview_allowed()) theme_set_text(s_preview, s_preview_txt, sizeof s_preview_txt, preview);
    else if (s_preview_txt[0]) theme_set_text(s_preview, s_preview_txt, sizeof s_preview_txt, "");
    /* an end reached by this move: the thud (UI-V2 §5), once per arrival at it */
    bool at_top = has_total && total > 0 && lines_above >= total;
    if ((at_bottom && !was_bottom && s_last_dir > 0) || (at_top && s_last_dir < 0)) {
        stop_haptic();
        glide_stop();
    }
    dbg_event("ui", "scroll_pos", "\"lines_above\":%u,\"at_bottom\":%s,\"total\":%d,\"preview\":%s",
              (unsigned)lines_above, at_bottom ? "true" : "false", has_total ? (int)total : -1,
              preview[0] && preview_allowed() ? "shown" : "none");
    render();
}

void ui_scroll_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "scroll");
    r->has_sel_sid = session() != NULL;
    r->sel_sid = s_sid;
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    /* the number brackets keep "1" apart from "11" */
    if (s_count_txt[0]) ui_report_add(r, "[%s] lines up", s_count_txt);
    else ui_report_add(r, "Live");
    if (!lv_obj_has_flag(s_preview, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_preview_txt);
}

static lv_obj_t *line(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, int cy, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    int lh = lv_font_get_line_height(f);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_size(l, w, lh);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, UI_CX - w / 2, cy - lh / 2);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ui_scroll_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* whose terminal */
    s_sigil = ui_sigil_create(s_root, UI_CX, SIGIL_Y, SIGIL_R);
    s_marks = line(s_root, FONT_CAPTION, COL_TEXT3, MARK_Y, 120);

    /* the scrollback: the column, the view's own line over its middle row, the live glyph under
     * it */
    s_map = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_map);
    lv_obj_remove_flag(s_map, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_map, MAP_X0, VIEW_Y - MAP_HALF);
    lv_obj_set_size(s_map, MAP_X1 - MAP_X0, 2 * MAP_HALF);
    lv_obj_add_event_cb(s_map, on_map_draw, LV_EVENT_DRAW_MAIN, NULL);
    s_preview = line(s_root, FONT_BODY, COL_TEXT, VIEW_Y, PREVIEW_W);
    lv_obj_set_style_text_align(s_preview, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_x(s_preview, BAR_X);
    s_live = ui_glyph_create(s_root, FONT_ICON_XL, UI_CX, LIVE_Y);

    /* how far up */
    s_track = ui_track_create(s_root, UI_CX, UI_CX, TRACK_R, TRACK_W, TRACK_START, TRACK_SPAN);

    /* rule 6: A goes back to the live bottom, B is a page down */
    s_key[0] = ui_key_hint_create(s_root, UI_KEY_A);
    ui_key_hint_set(s_key[0], ICON_LIVE);
    s_key[1] = ui_key_hint_create(s_root, UI_KEY_B);
    ui_key_hint_set(s_key[1], ICON_DOWN);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
