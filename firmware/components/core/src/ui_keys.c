/* The KEYS screen in Sigil (docs/design/GLYPHS.md §5; issue 1e57 for what it does): a swipe
 * keyboard for one session. Opened by Keyboard at the end of the Reply list. The watch only draws
 * the keys and records strokes (keys.c); the daemon decodes them into words and types them into the
 * terminal, then reports candidates back.
 *
 * What is on the glass:
 *   - who gets the words: the keyboard glyph beside the project's sigil;
 *   - the candidates (text: rule 1), the typed one on a yellow capsule; a tap on one swaps the
 *     typed word. Before anything is typed, a drawn swipe in their place says what to do ("Swipe
 *     the letters");
 *   - a refusal as a sign above them (rule 8): the keyboard and an hourglass (busy), the terminal
 *     and the prohibit glyph (not kitty), ...;
 *   - the keys: bare letters (text entry: rule 1), no boxes; the three keys that are not characters
 *     are capsules with a glyph: the layer key shows the characters it leads to, space a space bar,
 *     delete the backspace glyph;
 *   - under the keys the two hints: A is the paper plane inside a second ring, the ring a hold
 *     fills (A held 1.2 s sends, Enter; a short press only lights that ring), B deletes the last
 *     word.
 * B held, a double tap below the keys or the A+B chord leave, and the daemon erases what was not
 * sent. Sent is a burst (rule 7). No text is composed or shown here beyond the candidates. The
 * report names all of it in the words the screen used to show (rule 9). */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "gesture.h"
#include "keys.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define HEAD_Y 48
#define HEAD_SIGIL_R 22
#define HEAD_GLYPH_X (UI_CX - 28) /* a 28 px glyph, a 10 px gap, the sigil: centred as a pair */
#define HEAD_SIGIL_X (UI_CX + 19)
#define SIGN_Y 104  /* a refusal */
#define SWIPE_Y 120 /* the drawn swipe, before anything is typed */
#define CAND_Y 132  /* the candidates' row: its top; the keys begin at KEYS_TOP_Y 170 */
#define CAND_H 32
#define CAND_X 38 /* three slots of 128 px, 4 px apart: where a tap picks one */
#define CAND_W 128
#define CAND_STEP 132
#define CAND_PAD 14 /* the typed word's capsule: this much wider than the word on each side */
#define N_SLOTS 3
#define TRAIL_PTS 64
#define FADE_MS 250
#define SPECIAL_H 40 /* the capsule of a key that is not a character */
#define SPECIAL_PAD 4
/* Under the key block (it ends at KEYS_BOTTOM_Y 386) the glass is 100 px wide: the two hints sit
 * there side by side, their hold rings inside r 207. */
#define HINT_Y 409
#define HINT_DX 44
/* A's second ring: where ui_kit.c draws a key hint's hold (3 px out, 3 wide) */
#define HINT_RING 25
#define FLASH_MS 1800 /* how long a sign or a lit ring answers a gesture */

static lv_obj_t *s_root, *s_draw, *s_head, *s_sigil, *s_swipe, *s_sign, *s_slot[N_SLOTS], *s_trail;
static lv_obj_t *s_key_a, *s_key_b, *s_ring_a;
static char s_header_txt[64], s_status_txt[32], s_slot_txt[N_SLOTS][KEYS_CAND_LEN];
static lv_point_precise_t s_pts[TRAIL_PTS];
static uint32_t s_sid, s_now, s_cands_rev, s_lift_ms;
static int s_trail_n;
static bool s_holding, s_ramp_started, s_was_down;
static uint32_t s_hold_t0, s_bottom_tap_ms;
static bool s_bottom_tapped;
/* what the screen answers a gesture with, in words, while it shows: "Hold to send", "Offline" */
static const char *s_flash = "";
static uint32_t s_flash_until;
static const char *s_sign_shown; /* the words whose sign is on screen (NULL: none) */

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* The hold fills the ring round A's hint and the bezel ring. */
static void hold_progress(int permille)
{
    ui_disc_set_hold(s_key_a, permille);
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

static const char *err_text(int err)
{
    static const char *const t[] = {"Out of date", "Busy",      "Not kitty",
                                    "No window",   "Timed out", "Failed"};
    return err >= 0 && err < 6 ? t[err] : "Failed";
}

/* ───────── the keys ───────── */

static void capsule(lv_layer_t *l, int cx, int cy, int w, int h, lv_color_t c)
{
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_COVER;
    r.bg_color = c;
    r.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {cx - w / 2, cy - h / 2, cx + w / 2 - 1, cy + h / 2 - 1};
    lv_draw_rect(l, &r, &a);
}

static void on_draw(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    const lv_area_t *clip = &l->_clip_area;
    keys_key_t k[KEYS_MAX_KEYS];
    int n = keys_layout(keys_layer(), k, KEYS_MAX_KEYS);
    lv_draw_label_dsc_t t;
    lv_draw_label_dsc_init(&t);
    t.opa = LV_OPA_COVER;
    t.align = LV_TEXT_ALIGN_CENTER;
    t.text_local = 1;
    for (int i = 0; i < n; i++) {
        if (k[i].x > clip->x2 || k[i].x + k[i].w <= clip->x1 || k[i].y > clip->y2 ||
            k[i].y + k[i].h <= clip->y1)
            continue;
        int cx = k[i].x + k[i].w / 2, cy = k[i].y + k[i].h / 2;
        char txt[8] = {(char)k[i].cp, 0};
        const char *s = txt;
        bool special = k[i].cp < 0x20 || k[i].cp == KEYS_CP_SPACE;
        if (special) capsule(l, cx, cy, k[i].w - 2 * SPECIAL_PAD, SPECIAL_H, COL_FILL1);
        if (k[i].cp == KEYS_CP_BKSP) {
            ui_glyph_draw(l, FONT_ICON_M, ICON_BACKSPACE, cx, cy, COL_TEXT2, LV_OPA_COVER);
            continue;
        }
        if (k[i].cp == KEYS_CP_SPACE) { /* a space bar: a low bracket */
            capsule(l, cx, cy + 5, 44, 3, COL_TEXT3);
            capsule(l, cx - 21, cy + 2, 3, 9, COL_TEXT3);
            capsule(l, cx + 21, cy + 2, 3, 9, COL_TEXT3);
            continue;
        }
        /* the characters the key leads to */
        if (k[i].cp == KEYS_CP_LAYER) s = keys_layer() ? "abc" : "123";
        t.font = special ? FONT_CAPTION : FONT_HEAD;
        t.color = special ? COL_TEXT2 : COL_TEXT;
        t.text = s;
        int lh = lv_font_get_line_height(t.font);
        lv_area_t ta = {k[i].x, cy - lh / 2, k[i].x + k[i].w - 1, cy + lh / 2};
        lv_draw_label(l, &t, &ta);
    }
}

/* "Swipe the letters", drawn: a stroke across a few keys and the fingertip at its end. */
static void on_swipe_draw(lv_event_t *e)
{
    static const int8_t pt[][2] = {{-58, 6}, {-34, -9}, {-8, 8}, {20, -9}, {50, 3}};
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = theme_dim(RGB_ASK, 45);
    d.width = 6;
    d.round_start = d.round_end = 1;
    for (size_t i = 0; i + 1 < sizeof pt / sizeof pt[0]; i++) {
        d.p1.x = cx + pt[i][0];
        d.p1.y = cy + pt[i][1];
        d.p2.x = cx + pt[i + 1][0];
        d.p2.y = cy + pt[i + 1][1];
        lv_draw_line(l, &d);
    }
    capsule(l, cx + 50, cy + 3, 18, 18, lv_color_hex(RGB_ASK));
}

/* ───────── what is above the keys ───────── */

/* The sign, by precedence: what answers a gesture just made, then the daemon's refusal. */
static void set_sign(const char *words, uint32_t rgb)
{
    if (words == s_sign_shown) return;
    s_sign_shown = words;
    show(s_sign, words != NULL);
    if (!words) return;
    const char *g = theme_glyph(words);
    ui_sign_set(s_sign, g ? g : ICON_ERROR, NULL, rgb);
}

/* The typed candidate lies on a capsule as wide as its word; the others are bare words. */
static void set_slot(int i, const char *word, bool on, bool typed)
{
    lv_obj_t *l = s_slot[i];
    bool changed = strcmp(s_slot_txt[i], word) != 0;
    theme_set_text(l, s_slot_txt[i], KEYS_CAND_LEN, word);
    theme_set_text_color(l, typed ? COL_ON_ACCENT : COL_TEXT);
    theme_set_bg_color(l, typed ? lv_color_hex(RGB_ASK) : COL_BG);
    show(l, on);
    if (!changed) return;
    lv_point_t sz;
    lv_text_get_size(&sz, word, FONT_BODY_STRONG, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int w = sz.x + 2 * CAND_PAD;
    if (w > CAND_W) w = CAND_W;
    lv_obj_set_width(l, w);
    lv_obj_set_x(l, CAND_X + CAND_STEP * i + (CAND_W - w) / 2);
}

static void render(void)
{
    const session_t *s = model_find(s_sid);
    const char *base = s ? theme_basename(s->repo) : "";
    snprintf(s_header_txt, sizeof s_header_txt, "Type%s%s", theme_sep(),
             base[0] ? base : "(no folder)");
    ui_sigil_set_session(s_sigil, base, s ? s->sid : 0);
    const keys_cands_t *c = keys_cands();
    const char *status = c->err >= 0 ? err_text(c->err) : c->n ? "" : "Swipe the letters";
    snprintf(s_status_txt, sizeof s_status_txt, "%s", status);
    bool offline = s_flash[0] && !strcmp(s_flash, "Offline");
    set_sign(offline ? s_flash : c->err >= 0 ? err_text(c->err) : NULL, RGB_AMBER);
    show(s_swipe, !offline && c->err < 0 && !c->n);
    /* A's second ring: dim, lit while a short press is answered with "Hold to send" */
    ui_arc_set(s_ring_a, 0, COL_TEXT, s_flash[0] && !offline ? COL_TEXT : theme_dim(RGB_KEY_A, 40));
    if (c->rev == s_cands_rev) return;
    s_cands_rev = c->rev;
    for (int i = 0; i < N_SLOTS; i++)
        set_slot(i, i < c->n ? c->word[i] : "", i < c->n, i == c->typed);
}

static void flash(const char *words)
{
    s_flash = words;
    s_flash_until = s_now + FLASH_MS;
    dbg_event("ui", "keys_flash", "\"text\":%s", dbg_q(words));
}

void ui_keys_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_now = now;
    s_holding = s_ramp_started = s_was_down = s_bottom_tapped = false;
    s_trail_n = 0;
    s_cands_rev = (uint32_t)-1;
    s_header_txt[0] = s_status_txt[0] = 0;
    s_flash = "";
    ui_disc_set_hold(s_key_a, 0);
    ring_set_still(ui_home_ring(), true);
    if (!keys_open(sid, now)) {
        g_ui_actions->reject();
        flash("Offline");
    }
    show(s_trail, false);
    lv_obj_invalidate(s_draw);
    render();
    show(s_root, true);
    dbg_event("ui", "keys", "\"sid\":%u,\"kid\":%u", (unsigned)sid, (unsigned)keys_kid());
}

/* Always erases: whatever was typed and not sent is discarded, however the screen is left. */
void ui_keys_close(void)
{
    hold_cancel();
    keys_close(true);
    hold_progress(0);
    ring_set_still(ui_home_ring(), !ui_animations_allowed());
    show(s_root, false);
}

bool ui_keys_refresh(uint32_t now)
{
    s_now = now;
    const session_t *s = model_find(s_sid);
    if (!s || s->state == SESS_ENDED) return false;
    render();
    return true;
}

bool ui_keys_act(nav_action_t a, const gesture_evt_t *g)
{
    (void)g;
    switch (a) {
    case NAV_COMMIT: /* a short A press: sending needs the hold, and the ring to fill lights up */
        flash("Hold to send");
        g_ui_actions->reject();
        render();
        return true;
    case NAV_HOLD_PRESS: /* the long-press event: 0.6 s of the hold have already passed */
        s_holding = true;
        s_ramp_started = false;
        s_hold_t0 = s_now - GESTURE_LONG_PRESS_MS;
        return true;
    case NAV_ERASE:
        if (keys_del()) g_ui_actions->tick();
        else g_ui_actions->reject();
        render();
        return true;
    default: return false;
    }
}

static int slot_at(int x)
{
    int i = (x - CAND_X) / CAND_STEP;
    return x >= CAND_X && i < N_SLOTS && x < CAND_X + i * CAND_STEP + CAND_W ? i : -1;
}

static bool on_layer_key(const keys_pt_t *p)
{
    keys_key_t k[KEYS_MAX_KEYS];
    int n = keys_layout(keys_layer(), k, KEYS_MAX_KEYS);
    for (int i = 0; i < n; i++)
        if (k[i].cp == KEYS_CP_LAYER && p->x >= k[i].x && p->x < k[i].x + k[i].w &&
            p->y >= k[i].y && p->y < k[i].y + k[i].h)
            return true;
    return false;
}

/* What a finished stroke means: the layer key, the candidates and the strip below the keys are
 * local, the rest is the daemon's. Returns true when the stroke left the keyboard. */
static bool classify(const keys_stroke_t *st)
{
    const keys_pt_t *p0 = &st->pts[0];
    bool tap = keys_is_tap(st);
    bool bottom = tap && p0->y >= KEYS_BOTTOM_Y;
    if (bottom && s_bottom_tapped && s_now - s_bottom_tap_ms <= KEYS_BACK_DOUBLE_MS) {
        s_bottom_tapped = false;
        dbg_event("keys", "back", "\"how\":\"double_tap\"");
        ui_go_home(); /* leaves KEYS like B held: ui_keys_close erases what was not sent */
        g_ui_actions->tick();
        return true;
    }
    s_bottom_tapped = bottom;
    s_bottom_tap_ms = s_now;
    if (bottom) return false; /* a single tap down there types nothing */
    if (tap && on_layer_key(p0)) {
        keys_set_layer(keys_layer() ? 0 : 1);
        g_ui_actions->tick();
        lv_obj_invalidate(s_draw);
        return false;
    }
    if (tap && p0->y < KEYS_TOP_Y) {
        int i = slot_at(p0->x);
        if (i >= 0 && i < keys_cands()->n && i < N_SLOTS) {
            if (keys_pick(i)) g_ui_actions->tick();
            else g_ui_actions->reject();
        }
        return false;
    }
    if (!keys_send_stroke(st)) g_ui_actions->reject();
    return false;
}

static void update_trail(void)
{
    bool down;
    const keys_stroke_t *l = keys_live(&down);
    if (down) {
        s_was_down = true;
        int n = l->n < TRAIL_PTS ? l->n : TRAIL_PTS;
        if (n >= 2 && n != s_trail_n) {
            for (int i = 0; i < n; i++)
                s_pts[i] = (lv_point_precise_t){l->pts[l->n - n + i].x, l->pts[l->n - n + i].y};
            lv_line_set_points(s_trail, s_pts, (uint32_t)n);
            theme_set_opa(s_trail, LV_OPA_COVER);
            show(s_trail, true);
        }
        s_trail_n = n;
        return;
    }
    if (s_was_down) {
        s_was_down = false;
        s_lift_ms = s_now;
    }
    if (s_trail_n && !lv_obj_has_flag(s_trail, LV_OBJ_FLAG_HIDDEN)) {
        uint32_t el = s_now - s_lift_ms;
        if (el >= FADE_MS) {
            show(s_trail, false);
            s_trail_n = 0;
        } else {
            theme_set_opa(s_trail, (lv_opa_t)(255 - 255 * el / FADE_MS));
        }
    }
}

void ui_keys_tick(uint32_t now)
{
    s_now = now;
    if (s_flash[0] && (int32_t)(now - s_flash_until) >= 0) s_flash = "";
    const keys_stroke_t *st;
    while ((st = keys_next_stroke()) != NULL) {
        bool left = classify(st);
        keys_stroke_done();
        if (left) return;
    }
    update_trail();
    if (s_holding) {
        if (!g_ui_actions->button_down(0)) {
            hold_cancel(); /* A let go before the hold completed */
        } else {
            uint32_t el = now - s_hold_t0;
            if (el >= NAV_HOLD_SHOW_MS && !s_ramp_started) {
                s_ramp_started = true;
                g_ui_actions->hold_start();
            }
            hold_progress((int)(el * 1000 / NAV_HOLD_MS));
            if (el >= NAV_HOLD_MS) {
                s_holding = s_ramp_started = false;
                hold_progress(0);
                if (keys_send(el)) {
                    g_ui_actions->sent();
                    ui_burst("Sent", NULL, RGB_ASK); /* rule 7 */
                } else {
                    g_ui_actions->reject();
                }
            }
        }
    }
    render();
}

void ui_keys_report(ui_report_t *r)
{
    snprintf(r->screen, sizeof r->screen, "keys");
    r->has_sel_sid = true;
    r->sel_sid = s_sid;
    if (s_flash[0]) ui_report_add(r, "%s", s_flash); /* the lit ring, or the sign */
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    if (s_status_txt[0]) ui_report_add(r, "%s", s_status_txt);
    const keys_cands_t *c = keys_cands();
    for (int i = 0; i < c->n && i < N_SLOTS; i++)
        ui_report_add(r, "%s%s", i == c->typed ? "> " : "", c->word[i]);
}

lv_obj_t *ui_keys_init(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_draw = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_draw);
    lv_obj_set_size(s_draw, UI_SIZE, UI_SIZE);
    lv_obj_remove_flag(s_draw, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_draw, on_draw, LV_EVENT_DRAW_MAIN, NULL);

    /* who gets the words */
    s_head = ui_glyph_create(s_root, FONT_ICON_M, HEAD_GLYPH_X, HEAD_Y);
    ui_glyph_set(s_head, ICON_KEYBOARD, COL_TEXT2);
    s_sigil = ui_sigil_create(s_root, HEAD_SIGIL_X, HEAD_Y, HEAD_SIGIL_R);

    /* the drawn swipe, the candidates, a refusal's sign over them */
    s_swipe = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_swipe);
    lv_obj_remove_flag(s_swipe, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_swipe, 141, 31);
    lv_obj_set_pos(s_swipe, UI_CX - 70, SWIPE_Y - 15);
    lv_obj_add_event_cb(s_swipe, on_swipe_draw, LV_EVENT_DRAW_MAIN, NULL);
    for (int i = 0; i < N_SLOTS; i++) {
        lv_obj_t *l = s_slot[i] = lv_label_create(s_root);
        lv_obj_set_style_text_font(l, FONT_BODY_STRONG, 0);
        lv_obj_set_style_text_color(l, COL_TEXT, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(l, CAND_W, CAND_H);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(l, COL_BG, 0);
        lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_top(l, (CAND_H - lv_font_get_line_height(FONT_BODY_STRONG)) / 2, 0);
        lv_obj_set_pos(l, CAND_X + CAND_STEP * i, CAND_Y);
        lv_label_set_text(l, "");
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
    s_sign = ui_sign_create(s_root, UI_CX, SIGN_Y);
    lv_obj_add_flag(s_sign, LV_OBJ_FLAG_HIDDEN);

    s_trail = lv_line_create(s_root);
    lv_obj_set_pos(s_trail, 0, 0);
    lv_obj_set_style_line_width(s_trail, 6, 0);
    lv_obj_set_style_line_color(s_trail, lv_color_hex(RGB_ASK), 0);
    lv_obj_set_style_line_rounded(s_trail, true, 0);
    lv_obj_add_flag(s_trail, LV_OBJ_FLAG_HIDDEN);

    /* rule 6: what the buttons do. The key block reaches down to where the hints usually are, so
     * they sit in the glass below it, side by side. A sends on a hold: the second ring round its
     * hint is the ring to fill. */
    s_ring_a = ui_arc_create(s_root, UI_CX - HINT_DX, HINT_Y, HINT_RING, 3);
    ui_arc_set(s_ring_a, 0, COL_TEXT, theme_dim(RGB_KEY_A, 40));
    s_key_a = ui_key_hint_create(s_root, UI_KEY_A);
    ui_key_hint_set(s_key_a, ICON_SENT);
    ui_disc_set_pos(s_key_a, UI_CX - HINT_DX, HINT_Y);
    s_key_b = ui_key_hint_create(s_root, UI_KEY_B);
    ui_key_hint_set(s_key_b, ICON_BACKSPACE);
    ui_disc_set_pos(s_key_b, UI_CX + HINT_DX, HINT_Y);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
