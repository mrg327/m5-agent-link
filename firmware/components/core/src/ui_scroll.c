/* The SCROLL screen (UI-V2 §4.4): the centre is a trackpad for the focused terminal's scrollback.
 * A drag step is one wheel line, a release at speed glides on, B pages, A jumps back to the live
 * bottom. The first step on an idle link goes out at once, later ones are summed into one
 * cmd_op_scroll per NAV_SCROLL_COALESCE_MS; the daemon's scroll_pos corrects the counter, fills the
 * index ring's position track and (privacy ≥ preview) the one-line preview.
 * Capsules look: one big number, two chevrons, a muted line of terminal text, circular key caps. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "hal.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define HEADER_Y 80
#define CHEV_UP_Y 120
#define COUNT_Y 174
#define CAPTION_Y 222
#define PREVIEW_Y 258
#define CHEV_DN_Y 296 /* clear of the key badges at ~333 */
#define HINT_Y 364
#define CHEVRON_LIT_MS 350  /* a chevron glows this long after a move in its direction */
#define STOP_GAP_MS 400     /* the end-of-scrollback thud repeats at most this often */
#define UNKNOWN_TOTAL_SOFT 100 /* total unknown: the track arc is lines/(lines+this), so 100 lines = half */

static lv_obj_t *s_root, *s_header, *s_chev_up, *s_chev_dn, *s_count, *s_caption, *s_preview, *s_hint;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static uint32_t s_sid, s_now, s_last_input;
static char s_header_txt[64], s_count_txt[24], s_caption_txt[32], s_preview_txt[64];

/* position: the daemon's last word, nudged locally by what we sent since */
static uint32_t s_lines_above, s_total;
static bool s_at_bottom = true, s_has_total, s_have_pos;
/* coalescer: pending lines or pages (pages win); one command per NAV_SCROLL_COALESCE_MS */
static int s_pend_lines, s_pend_pages;
static uint32_t s_sent_ms;
static bool s_sent_any;
static int s_detent_lines; /* finger-driven lines since the last detent */
/* glide: velocity (px/s, up < 0), sub-step travel carried between ticks, clock */
static float s_glide_v, s_glide_px;
static uint32_t s_glide_ms, s_glide_t0;
static bool s_caught; /* the current touch stopped a glide: its tap must not leave */
static int s_last_dir; /* -1 up (older), +1 down (newer): which chevron and which end can "stop" */
static uint32_t s_lit_until, s_stop_ms;

static const session_t *session(void) { return model_find(s_sid); }

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static bool preview_allowed(void) { return model_settings()->privacy != PRIVACY_META; }

static void render(void)
{
    const session_t *s = session();
    uint32_t rgb = s ? theme_session_rgb(s) : RGB_ACCENT;
    char buf[64];
    if (s_at_bottom || s_lines_above == 0) {
        theme_set_text(s_count, s_count_txt, sizeof s_count_txt, "Live");
        theme_set_text(s_caption, s_caption_txt, sizeof s_caption_txt, "at the bottom");
    } else {
        snprintf(buf, sizeof buf, "-%u lines", (unsigned)s_lines_above);
        theme_set_text(s_count, s_count_txt, sizeof s_count_txt, buf);
        theme_set_text(s_caption, s_caption_txt, sizeof s_caption_txt, "above the bottom");
    }
    bool lit = s_now < s_lit_until;
    theme_set_text_color(s_chev_up, lit && s_last_dir < 0 ? lv_color_hex(rgb) : COL_MUTED);
    theme_set_text_color(s_chev_dn, lit && s_last_dir > 0 ? lv_color_hex(rgb) : COL_MUTED);
    bool prev = preview_allowed() && s_preview_txt[0];
    show(s_preview, prev);
    /* the track: a thin grey ring with an arc for how far above the bottom the view sits */
    int permille = 0;
    if (s_lines_above > 0) {
        if (s_has_total && s_total > 0) permille = (int)((uint64_t)s_lines_above * 1000 / s_total);
        else permille = (int)((uint64_t)s_lines_above * 1000 / (s_lines_above + UNKNOWN_TOTAL_SOFT));
    }
    ring_set_track(ui_home_ring(), permille, RGB_ACCENT);
    theme_set_text_color(s_word[0], lv_color_hex(rgb));
}

bool ui_scroll_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = session();
    if (!s || s->state == SESS_ENDED) return false;
    char buf[64];
    const char *base = theme_basename(s->repo);
    char host[24];
    theme_host_prefix(host, sizeof host, s);
    snprintf(buf, sizeof buf, "%s%s%s%s", host, theme_agent_name(s->agent), theme_sep(), base[0] ? base : "(no folder)");
    theme_set_text(s_header, s_header_txt, sizeof s_header_txt, buf);
    render();
    return true;
}

void ui_scroll_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_now = s_last_input = now;
    s_lines_above = s_total = 0;
    s_at_bottom = true;
    s_has_total = s_have_pos = false;
    s_pend_lines = s_pend_pages = 0;
    s_sent_any = s_caught = false;
    s_detent_lines = 0;
    s_glide_v = 0;
    s_last_dir = 0;
    s_lit_until = s_stop_ms = 0;
    s_preview_txt[0] = 0;
    lv_label_set_text(s_preview, "");
    if (g_ui_actions->sectors) g_ui_actions->sectors(0); /* the rim is unused here */
    ui_scroll_refresh(now);
    show(s_root, true);
    hal_link_interactive(true);
    dbg_event("ui", "scroll", "\"sid\":%u,\"preview_allowed\":%s", (unsigned)sid, preview_allowed() ? "true" : "false");
}

void ui_scroll_close(void)
{
    s_glide_v = 0;
    hal_link_interactive(false);
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
    s_lit_until = s_now + CHEVRON_LIT_MS;
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

bool ui_scroll_act(nav_action_t a, const gesture_evt_t *g)
{
    if (g->kind != G_LIFT && g->kind != G_SHAKE) s_last_input = s_now;
    switch (a) {
    case NAV_SCROLL_CATCH:
        s_caught = s_glide_v != 0;
        glide_stop();
        return true;
    case NAV_SCROLL_DRAG: push(nav_scroll_lines(g->dy), true); return true;
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
    if (s_lit_until && now >= s_lit_until) {
        s_lit_until = 0;
        render();
    }
    return now - s_last_input > NAV_SCROLL_TIMEOUT_MS;
}

void ui_scroll_on_pos(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total, uint32_t total,
                      const char *preview)
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
    dbg_event("ui", "scroll_pos", "\"lines_above\":%u,\"at_bottom\":%s,\"total\":%d,\"preview\":%s", (unsigned)lines_above,
              at_bottom ? "true" : "false", has_total ? (int)total : -1, preview[0] && preview_allowed() ? "shown" : "none");
    render();
}

void ui_scroll_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "scroll");
    r->has_sel_sid = session() != NULL;
    r->sel_sid = s_sid;
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    ui_report_add(r, "%s", s_count_txt);
    ui_report_add(r, "%s", s_caption_txt);
    if (!lv_obj_has_flag(s_preview, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "%s", s_preview_txt);
}

static lv_obj_t *line(lv_obj_t *parent, int px, lv_color_t col, int y, int w)
{
    lv_obj_t *l = theme_label(parent, px, col);
    lv_obj_set_size(l, w, lv_font_get_line_height(theme_font(px)));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, UI_CX - w / 2, y);
    return l;
}

lv_obj_t *ui_scroll_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_header = line(s_root, 16, COL_MUTED, HEADER_Y, 280);
    s_chev_up = line(s_root, 24, COL_MUTED, CHEV_UP_Y, 60);
    lv_label_set_text(s_chev_up, LV_SYMBOL_UP);
    s_count = line(s_root, 32, COL_TEXT, COUNT_Y, 300);
    s_caption = line(s_root, 16, COL_MUTED, CAPTION_Y, 300);
    s_preview = line(s_root, 20, COL_MUTED, PREVIEW_Y, theme_chord(PREVIEW_Y + 12, 28));
    s_chev_dn = line(s_root, 24, COL_MUTED, CHEV_DN_Y, 60);
    lv_label_set_text(s_chev_dn, LV_SYMBOL_DOWN);
    s_hint = line(s_root, 16, COL_MUTED, HINT_Y, 300);
    lv_label_set_text(s_hint, "drag like a trackpad");
    static const char *const words[2] = {"TO BOTTOM", "PAGE DOWN"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? UI_BADGE_A_DEG : UI_BADGE_B_DEG,
                                     UI_BADGE_R, &s_key[i], &s_word[i]);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
