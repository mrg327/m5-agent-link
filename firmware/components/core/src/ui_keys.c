/* The KEYS screen (issue 1e57): a swipe keyboard for one session. Opened by "Type..." at the end of
 * the Reply list. The watch only draws the keys and records strokes (keys.c); the daemon decodes
 * them into words and types them into the terminal, then reports candidates back. A tap on a
 * candidate in the top cap swaps the typed word, B deletes the last word, A held 1.2 s sends
 * (Enter). B held or the A+B chord leave, and the daemon erases what was not sent. No text is
 * composed or shown here beyond the candidates. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "gesture.h"
#include "keys.h"
#include "nav.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define HEADER_Y 66
#define STATUS_Y 96
#define CAND_Y 124
#define CAND_X 38
#define CAND_W 128
#define CAND_STEP 132
#define CAND_H 34
#define N_SLOTS 3
#define TRAIL_PTS 64
#define FADE_MS 250
#define BADGE_R 188
#define BADGE_A_DEG 220
#define BADGE_B_DEG 140

static lv_obj_t *s_root, *s_draw, *s_header, *s_status, *s_slot[N_SLOTS], *s_trail;
static lv_obj_t *s_badge[2], *s_key[2], *s_word[2];
static char s_header_txt[64], s_status_txt[32], s_slot_txt[N_SLOTS][KEYS_CAND_LEN];
static lv_point_precise_t s_pts[TRAIL_PTS];
static uint32_t s_sid, s_now, s_cands_rev, s_lift_ms;
static int s_trail_n;
static bool s_holding, s_ramp_started, s_was_down;
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

static const char *err_text(int err)
{
    static const char *const t[] = {"Out of date", "Busy", "Not kitty", "No window", "Timed out", "Failed"};
    return err >= 0 && err < 6 ? t[err] : "Failed";
}

static void on_draw(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    keys_key_t k[KEYS_MAX_KEYS];
    int n = keys_layout(keys_layer(), k, KEYS_MAX_KEYS);
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_COVER;
    r.radius = 8;
    lv_draw_label_dsc_t t;
    lv_draw_label_dsc_init(&t);
    t.opa = LV_OPA_COVER;
    t.align = LV_TEXT_ALIGN_CENTER;
    t.text_local = 1;
    for (int i = 0; i < n; i++) {
        bool special = k[i].cp < 0x20 || k[i].cp == KEYS_CP_SPACE;
        char txt[8] = {(char)k[i].cp, 0};
        const char *s = txt;
        if (k[i].cp == KEYS_CP_LAYER) s = keys_layer() ? "abc" : "123";
        else if (k[i].cp == KEYS_CP_BKSP) s = LV_SYMBOL_BACKSPACE;
        else if (k[i].cp == KEYS_CP_SPACE) s = "space";
        r.bg_color = special ? COL_SURFACE : COL_SURFACE2;
        lv_area_t a = {k[i].x + 2, k[i].y + 2, k[i].x + k[i].w - 3, k[i].y + k[i].h - 3};
        lv_draw_rect(l, &r, &a);
        t.font = theme_font(special ? 16 : 22);
        t.color = special ? COL_MUTED : COL_TEXT;
        t.text = s;
        int lh = lv_font_get_line_height(t.font);
        lv_area_t ta = {a.x1, a.y1 + (a.y2 - a.y1 + 1 - lh) / 2, a.x2, a.y2};
        lv_draw_label(l, &t, &ta);
    }
}

static void render(void)
{
    const session_t *s = model_find(s_sid);
    char buf[64];
    const char *base = s ? theme_basename(s->repo) : "";
    snprintf(buf, sizeof buf, "Type%s%s", theme_sep(), base[0] ? base : "(no folder)");
    theme_set_text(s_header, s_header_txt, sizeof s_header_txt, buf);
    const keys_cands_t *c = keys_cands();
    theme_set_text(s_status, s_status_txt, sizeof s_status_txt, c->err >= 0 ? err_text(c->err) : c->n ? "" : "Swipe the letters");
    theme_set_text_color(s_status, c->err >= 0 ? lv_color_hex(RGB_AMBER) : COL_MUTED);
    if (c->rev == s_cands_rev) return;
    s_cands_rev = c->rev;
    for (int i = 0; i < N_SLOTS; i++) {
        bool on = i < c->n;
        theme_set_text(s_slot[i], s_slot_txt[i], KEYS_CAND_LEN, on ? c->word[i] : "");
        theme_set_text_color(s_slot[i], i == c->typed ? lv_color_hex(RGB_ASK) : COL_TEXT);
        theme_set_bg_color(s_slot[i], i == c->typed ? lv_color_mix(lv_color_hex(RGB_ASK), lv_color_black(), 50) : COL_SURFACE2);
        theme_set_opa(s_slot[i], LV_OPA_COVER);
        show(s_slot[i], on);
    }
}

void ui_keys_open(uint32_t sid, uint32_t now)
{
    s_sid = sid;
    s_now = now;
    s_holding = s_ramp_started = s_was_down = false;
    s_trail_n = 0;
    s_cands_rev = (uint32_t)-1;
    s_header_txt[0] = s_status_txt[0] = 0;
    ring_set_still(ui_home_ring(), true);
    if (!keys_open(sid, now)) {
        g_ui_actions->reject();
        ui_toast("Offline", RGB_AMBER);
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
    ring_set_progress(ui_home_ring(), 0, RGB_ASK);
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
    case NAV_COMMIT: /* a short A press: sending needs the hold */
        ui_toast("Hold to send", RGB_ASK);
        g_ui_actions->reject();
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
        if (k[i].cp == KEYS_CP_LAYER && p->x >= k[i].x && p->x < k[i].x + k[i].w && p->y >= k[i].y && p->y < k[i].y + k[i].h)
            return true;
    return false;
}

/* What a finished stroke means: the layer key and the candidates are local, the rest is the daemon's. */
static void classify(const keys_stroke_t *st)
{
    const keys_pt_t *p0 = &st->pts[0];
    bool tap = keys_is_tap(st);
    if (tap && on_layer_key(p0)) {
        keys_set_layer(keys_layer() ? 0 : 1);
        g_ui_actions->tick();
        lv_obj_invalidate(s_draw);
        return;
    }
    if (tap && p0->y < KEYS_TOP_Y) {
        int i = slot_at(p0->x);
        if (i >= 0 && i < keys_cands()->n && i < N_SLOTS) {
            if (keys_pick(i)) g_ui_actions->tick();
            else g_ui_actions->reject();
        }
        return;
    }
    if (!keys_send_stroke(st)) g_ui_actions->reject();
}

static void update_trail(void)
{
    bool down;
    const keys_stroke_t *l = keys_live(&down);
    if (down) {
        s_was_down = true;
        int n = l->n < TRAIL_PTS ? l->n : TRAIL_PTS;
        if (n >= 2 && n != s_trail_n) {
            for (int i = 0; i < n; i++) s_pts[i] = (lv_point_precise_t){l->pts[l->n - n + i].x, l->pts[l->n - n + i].y};
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
    const keys_stroke_t *st;
    while ((st = keys_next_stroke()) != NULL) {
        classify(st);
        keys_stroke_done();
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
            ring_set_progress(ui_home_ring(), (int)(el * 1000 / NAV_HOLD_MS), RGB_ASK);
            if (el >= NAV_HOLD_MS) {
                s_holding = s_ramp_started = false;
                ring_set_progress(ui_home_ring(), 0, RGB_ASK);
                if (keys_send(el)) {
                    g_ui_actions->sent();
                    ui_toast("Sent", RGB_ASK);
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
    if (s_header_txt[0]) ui_report_add(r, "%s", s_header_txt);
    if (s_status_txt[0]) ui_report_add(r, "%s", s_status_txt);
    const keys_cands_t *c = keys_cands();
    for (int i = 0; i < c->n && i < N_SLOTS; i++) ui_report_add(r, "%s%s", i == c->typed ? "> " : "", c->word[i]);
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
    s_header = theme_label(s_root, 16, COL_MUTED);
    lv_obj_set_size(s_header, 280, lv_font_get_line_height(theme_font(16)));
    lv_obj_set_style_text_align(s_header, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_header, UI_CX - 140, HEADER_Y);
    s_status = theme_label(s_root, 16, COL_MUTED);
    lv_obj_set_size(s_status, 280, lv_font_get_line_height(theme_font(16)));
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_status, UI_CX - 140, STATUS_Y);
    for (int i = 0; i < N_SLOTS; i++) {
        s_slot[i] = theme_label(s_root, 20, COL_TEXT);
        lv_obj_set_size(s_slot[i], CAND_W, CAND_H);
        lv_label_set_long_mode(s_slot[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(s_slot[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_opa(s_slot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_slot[i], 8, 0);
        lv_obj_set_style_pad_top(s_slot[i], (CAND_H - lv_font_get_line_height(theme_font(20))) / 2, 0);
        lv_obj_set_pos(s_slot[i], CAND_X + CAND_STEP * i, CAND_Y);
        lv_obj_add_flag(s_slot[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_trail = lv_line_create(s_root);
    lv_obj_set_pos(s_trail, 0, 0);
    lv_obj_set_style_line_width(s_trail, 6, 0);
    lv_obj_set_style_line_color(s_trail, lv_color_hex(RGB_ASK), 0);
    lv_obj_set_style_line_rounded(s_trail, true, 0);
    lv_obj_add_flag(s_trail, LV_OBJ_FLAG_HIDDEN);
    static const char *const words[2] = {"SEND", "DEL"};
    for (int i = 0; i < 2; i++)
        s_badge[i] = theme_key_badge(s_root, i == 0 ? "A" : "B", words[i], i == 0, i == 0 ? BADGE_A_DEG : BADGE_B_DEG, BADGE_R,
                                     &s_key[i], &s_word[i]);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}
