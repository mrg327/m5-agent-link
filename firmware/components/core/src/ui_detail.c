/* Session detail, doubling as the attention card when an approval is pending.
 * Approve needs a 1.2 s hold (fill bar); Deny and Open on PC are plain taps. */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_internal.h"
#include "ui_ring.h"
#include "ui_theme.h"

static lv_obj_t *s_ring, *s_agent, *s_title, *s_chip, *s_preview, *s_approve, *s_fill, *s_approve_l, *s_deny, *s_open;
static uint32_t s_sid, s_rid;
static bool s_attn, s_holding;
static uint32_t s_hold_t0;

static const session_t *current(void)
{
    uint32_t sid;
    return ui_selected(&sid) ? model_find(sid) : NULL;
}

static void set_hold_fill(int32_t w)
{
    lv_obj_set_width(s_fill, w);
}

static void hold_cancel(void)
{
    if (!s_holding) return;
    s_holding = false;
    set_hold_fill(0);
}

static void pill(lv_obj_t *p, int w, const char *text, uint32_t rgb)
{
    lv_obj_set_size(p, w, 64);
    lv_obj_set_style_radius(p, 32, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p, COL_SURFACE2, 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(rgb), 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = theme_label(p, 20, COL_TEXT);
    lv_label_set_text(l, text);
    lv_obj_center(l);
}

static void refresh(void)
{
    const session_t *s = current();
    if (!s) {
        hold_cancel();
        if (ui_current() == SCR_DETAIL) ui_goto(SCR_GLANCE, NAV_BACK);
        return;
    }
    const attention_t *a = model_attention_for_sid(s->sid);
    bool changed = a ? a->rid != s_rid : s_attn;
    s_sid = s->sid;
    s_rid = a ? a->rid : 0;
    s_attn = a != NULL;
    if (changed) hold_cancel();

    uint32_t rgb = theme_state_rgb(s->state);
    ring_set_solid(s_ring, lv_color_hex(rgb));

    char buf[96], age[16] = "";
    snprintf(buf, sizeof buf, "%s  %s", theme_agent_symbol(s->agent), theme_agent_name(s->agent));
    lv_label_set_text(s_agent, buf);
    theme_session_label(buf, sizeof buf, s, true);
    lv_label_set_text(s_title, buf);

    uint32_t now = theme_now_unix();
    if (now && s->tst && now >= s->tst) theme_elapsed(age, sizeof age, now - s->tst);
    snprintf(buf, sizeof buf, "%s%s%s", theme_state_label(s), age[0] ? theme_sep() : "", age);
    lv_label_set_text(s_chip, buf);
    lv_obj_set_style_text_color(s_chip, lv_color_hex(rgb), 0);

    const char *text = a ? (a->text[0] ? a->text : a->title) : s->summary;
    lv_label_set_text(s_preview, text[0] ? text : s->tool);

    bool approval = a && !a->answer_sent && (a->acts & (1u << ACT_APPROVE));
    bool deny = a && !a->answer_sent && (a->acts & (1u << ACT_DENY));
    approval ? lv_obj_remove_flag(s_approve, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(s_approve, LV_OBJ_FLAG_HIDDEN);
    deny ? lv_obj_remove_flag(s_deny, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(s_deny, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(lv_obj_get_parent(s_approve));
}

static void on_show(void)
{
    hold_cancel();
    s_attn = false;
    s_rid = 0;
    refresh();
}

static bool approve_ready(void) { return s_attn && !lv_obj_has_flag(s_approve, LV_OBJ_FLAG_HIDDEN); }

static void confirm(uint32_t hold_ms)
{
    hold_cancel();
    if (!approve_ready()) return;
    g_ui_actions->approve(s_sid, s_rid, hold_ms);
    refresh();
}

static void tick(uint32_t now_ms)
{
    (void)now_ms;
    if (!s_holding) return;
    uint32_t held = lv_tick_elaps(s_hold_t0);
    if (held >= UI_HOLD_MS) {
        confirm(held);
        return;
    }
    set_hold_fill((int32_t)(held * lv_obj_get_width(s_approve) / UI_HOLD_MS));
}

static bool gesture(const gesture_evt_t *g)
{
    switch (g->kind) {
    case G_PRESS:
        if (approve_ready() && ui_hit(s_approve, g->x, g->y, 6)) {
            s_holding = true;
            s_hold_t0 = lv_tick_get();
            dbg_event("ui", "hold_start", "\"sid\":%u", (unsigned)s_sid);
        }
        return false;
    case G_RELEASE:
        if (s_holding) {
            dbg_event("ui", "hold_cancel", "\"sid\":%u,\"dur_ms\":%u", (unsigned)s_sid, (unsigned)g->dur_ms);
            hold_cancel();
        }
        return false;
    case G_LONG_PRESS:
        if (g->dur_ms >= UI_HOLD_MS) confirm(g->dur_ms); /* injected hold */
        return true;
    case G_TAP:
        if (s_attn && !lv_obj_has_flag(s_deny, LV_OBJ_FLAG_HIDDEN) && ui_hit(s_deny, g->x, g->y, 6)) {
            g_ui_actions->deny(s_sid, s_rid);
            refresh();
        } else if (ui_hit(s_open, g->x, g->y, 6)) {
            g_ui_actions->focus(s_sid);
        }
        return true;
    case G_DOUBLE_TAP: g_ui_actions->focus(s_sid); return true;
    case G_SWIPE_RIGHT:
        g_ui_actions->ack(s_sid);
        ui_goto(SCR_LIST, NAV_BACK);
        return true;
    case G_SWIPE_LEFT:
    case G_BTN_B: ui_goto(SCR_LIST, NAV_BACK); return true;
    case G_BTN_A: g_ui_actions->focus(s_sid); return true;
    default: return false;
    }
}

static void report(ui_report_t *r)
{
    const session_t *s = current();
    if (!s) return;
    char buf[80];
    if (s_attn) {
        snprintf(r->screen, sizeof r->screen, "attention");
        r->has_rid = true;
        r->rid = s_rid;
    }
    theme_session_label(buf, sizeof buf, s, false);
    ui_report_add(r, "%s", buf);
    ui_report_add(r, "%s", lv_label_get_text(s_chip));
    ui_report_add(r, "%s", lv_label_get_text(s_preview));
    if (!lv_obj_has_flag(s_approve, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "Approve");
    if (!lv_obj_has_flag(s_deny, LV_OBJ_FLAG_HIDDEN)) ui_report_add(r, "Deny");
    ui_report_add(r, "Open on PC");
}

ui_screen_t g_detail = {.on_show = on_show, .refresh = refresh, .tick = tick, .gesture = gesture, .report = report};

void ui_detail_init(void)
{
    lv_obj_t *scr = g_detail.root = ui_new_screen();
    s_ring = ring_create(scr);

    lv_obj_t *col = lv_obj_create(scr);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 320, 410);
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_agent = theme_label(col, 16, COL_MUTED);
    s_title = theme_label(col, 24, COL_TEXT);
    lv_obj_set_width(s_title, 320);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_title, 30);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    s_chip = theme_label(col, 20, COL_MUTED);
    s_preview = theme_label(col, 16, COL_TEXT);
    lv_obj_set_width(s_preview, 280);
    lv_obj_set_height(s_preview, 62);
    lv_label_set_long_mode(s_preview, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s_preview, LV_TEXT_ALIGN_CENTER, 0);

    s_approve = lv_obj_create(col);
    pill(s_approve, 200, "", RGB_SUCCESS);
    lv_obj_remove_flag(s_approve, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_clip_corner(s_approve, true, 0);
    lv_obj_set_style_pad_all(s_approve, 0, 0);
    s_fill = lv_obj_create(s_approve);
    lv_obj_remove_style_all(s_fill);
    lv_obj_set_size(s_fill, 0, 64);
    lv_obj_set_pos(s_fill, 0, 0);
    lv_obj_set_style_bg_opa(s_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_fill, lv_color_hex(0x1f6f33), 0);
    s_approve_l = lv_obj_get_child(s_approve, 0);
    lv_obj_move_foreground(s_approve_l);
    lv_label_set_text(s_approve_l, "Hold to approve");
    lv_obj_set_style_text_color(s_approve_l, COL_TEXT, 0);

    s_deny = lv_obj_create(col);
    pill(s_deny, 140, "Deny", RGB_DANGER);
    s_open = lv_obj_create(col);
    pill(s_open, 180, "Open on PC", RGB_ACCENT);
}
