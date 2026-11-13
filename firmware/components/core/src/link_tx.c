/* Builders for device → daemon messages other than `cmd` (which lives in link.c). */
#include <string.h>

#include "link_internal.h"

MODEL_EXT_BSS static uint8_t s_txbuf[LINK_MAX_MSG]; /* encoded here, then copied by the transport: PSRAM */

static uint32_t set_hdr(struct hdr_r *h, bool has_re, uint32_t re)
{
    uint32_t id = ++g_link.tx_id;
    h->hdr_uint1uint = id;
    h->hdr_uint2uint_present = has_re;
    h->hdr_uint2uint.hdr_uint2uint = re;
    return id;
}

static bool send_env(hal_link_kind_t to, const char *what)
{
    size_t len = 0;
    int rc = cbor_encode_envelope(s_txbuf, sizeof s_txbuf, &g_tx_env, &len);
    if (rc != ZCBOR_SUCCESS) {
        dbg_event_at(DBG_ERR, "link", "tx_encode_failed", "\"msg\":\"%s\",\"rc\":%d", what, rc);
        return false;
    }
    bool ok = hal_link_send(to, s_txbuf, len);
    if (strcmp(what, "screenshot_chunk") != 0 && strcmp(what, "audio") != 0)
        dbg_event("link", "tx", "\"msg\":\"%s\",\"id\":%u,\"len\":%u,\"sent\":%s", what, (unsigned)g_link.tx_id,
                  (unsigned)len, ok ? "true" : "false");
    return ok;
}

static struct zcbor_string zstr(const char *s)
{
    return (struct zcbor_string){.value = (const uint8_t *)s, .len = strlen(s)};
}

static int device_class(const hal_board_info_t *bi)
{
    if (strcmp(bi->board, "sim") == 0) return device_class_sim_m_c;
    if (strncmp(bi->board, "m5stopwatch", 11) == 0) return device_class_watch_m_c;
    return device_class_other_m_c;
}

static void fill_caps(struct caps *c, const hal_board_info_t *bi)
{
    if (bi->has_display) {
        c->display_caps_m_present = true;
        struct display_caps *d = &c->display_caps_m.display_caps_m;
        d->display_caps_disp_shape_m.disp_shape_choice = bi->shape == HAL_SHAPE_ROUND ? disp_shape_round_m_c : disp_shape_rect_m_c;
        d->display_caps_uint1uint = bi->width;
        d->display_caps_uint2uint = bi->height;
        d->display_caps_uint3bool = bi->color;
    }
    size_t n = 0;
    if (bi->has_touch) c->caps_input_kind_m_l_input_kind_m[n++].input_kind_choice = input_kind_touch_m_c;
    if (bi->num_buttons) c->caps_input_kind_m_l_input_kind_m[n++].input_kind_choice = input_kind_btn_m_c;
    if (bi->has_imu) c->caps_input_kind_m_l_input_kind_m[n++].input_kind_choice = input_kind_imu_m_c;
    c->caps_input_kind_m_l_input_kind_m_count = n;
    if (bi->num_buttons) {
        c->caps_uint2uint_present = true;
        c->caps_uint2uint.caps_uint2uint = bi->num_buttons;
    }
    c->caps_uint3bool = bi->has_haptic;
    c->caps_uint4bool = bi->has_speaker;
    c->caps_uint5bool = bi->has_mic;
    c->caps_uint6bool = bi->has_battery;
    c->caps_uint7uint = LINK_MAX_MSG;
    /* Only features this firmware implements: voice when there is a mic, keys when there is a touch screen, no badge yet. */
    static const int feats[] = {feature_focus_m_c, feature_ack_m_c, feature_approve_m_c, feature_reply_m_c,
                                feature_debug_m_c, feature_scroll_m_c};
    n = 0;
    for (size_t i = 0; i < sizeof feats / sizeof *feats; i++) c->caps_feature_m_l_feature_m[n++].feature_choice = feats[i];
    if (bi->has_mic) c->caps_feature_m_l_feature_m[n++].feature_choice = feature_voice_m_c; /* dictation */
    if (bi->has_touch && bi->has_display) c->caps_feature_m_l_feature_m[n++].feature_choice = feature_keys_m_c; /* KEYS swipe keyboard */
    c->caps_feature_m_l_feature_m_count = n;
}

bool link_tx_hello(hal_link_kind_t to)
{
    const hal_board_info_t *bi = hal_board_info();
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_hello_m_c;
    struct msg_hello *m = &g_tx_env.envelope_msg_hello_m;
    set_hdr(&m->msg_hello_hdr_m, false, 0);
    struct hello *h = &m->msg_hello_hello_m;
    h->hello_major_l_major = 0;
    h->hello_major_l_minor = 22; /* 0.4: welcome.replies, cmd_op_type; 0.5: session q / rp; 0.6: voice; 0.7: keys;
                                  * 0.8: action_dictate on attention, dictate with rid answers that card;
                                  * 0.9: jobs (agent_shell, session eta / exit, cmd_op_rerun);
                                  * 0.10: gauges (usage message, session ctx / mdl / pm / pr / prs);
                                  * 0.11: answer window (attention left / win / len, detail chunks, clear
                                  *       handed_back, focus with a rid hands it back);
                                  * 0.12: presence (attention hush, session hush);
                                  * 0.13: standing rules (attention rule / scope / wide, approve with rh);
                                  * 0.14: done summary (session fc / la / lr / tp / dur);
                                  * 0.15: firmware update (fw_begin / fw_chunk / fw_ack);
                                  * 0.16: transcripts up to 2047 bytes (VOICE_TEXT_MAX);
                                  * 0.17: queued messages (session qd, type / dictate with q, cmd_op_unqueue);
                                  * 0.18: last message (session lm / ll / lp, detail without a rid) and
                                  *       questions (attention qs, cmd_op_answer);
                                  * 0.19: power report (device to daemon only);
                                  * 0.20: trackpad (welcome.pointer, cmd_op_pointer);
                                  * 0.21: delta firmware update (fw_begin.patch);
                                  * 0.22: agent graph (cmd graph, graph) */
    h->hello_device_info_m.device_info_device_class_m.device_class_choice = device_class(bi);
    h->hello_device_info_m.device_info_uint1tstr = zstr(bi->board);
    h->hello_device_info_m.device_info_uint2tstr = zstr(bi->fw_version);
    h->hello_device_info_m.device_info_uint3bstr = (struct zcbor_string){.value = bi->uid, .len = sizeof bi->uid};
    fill_caps(&h->hello_caps_m, bi);
    if (bi->has_battery) {
        h->hello_uint3uint_present = true;
        h->hello_uint3uint.hello_uint3uint = hal_battery_read().percent;
    }
    return send_env(to, "hello");
}

bool link_send_audio_start(uint32_t aid, uint32_t sid)
{
    if (!g_link.welcomed) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_audio_start_m_c;
    struct msg_audio_start *m = &g_tx_env.envelope_msg_audio_start_m;
    set_hdr(&m->msg_audio_start_hdr_m, false, 0);
    m->msg_audio_start_audio_start_m.audio_start_uint0uint = aid;
    m->msg_audio_start_audio_start_m.audio_start_audio_codec_m.audio_codec_choice = audio_codec_adpcm16k_m_c;
    m->msg_audio_start_audio_start_m.audio_start_uint2uint_present = sid != 0;
    m->msg_audio_start_audio_start_m.audio_start_uint2uint.audio_start_uint2uint = sid;
    return send_env(g_link.kind, "audio_start");
}

bool link_send_audio(uint32_t aid, uint32_t seq, const uint8_t *data, size_t len)
{
    if (!g_link.welcomed || len > 240) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_audio_m_c;
    struct msg_audio *m = &g_tx_env.envelope_msg_audio_m;
    set_hdr(&m->msg_audio_hdr_m, false, 0);
    m->msg_audio_audio_m.audio_uint0uint = aid;
    m->msg_audio_audio_m.audio_uint1uint = seq;
    m->msg_audio_audio_m.audio_uint2bstr = (struct zcbor_string){.value = data, .len = len};
    return send_env(g_link.kind, "audio");
}

bool link_send_log(int lvl, const char *msg)
{
    if (!g_link.welcomed) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_log_m_c;
    struct msg_log *m = &g_tx_env.envelope_msg_log_m;
    set_hdr(&m->msg_log_hdr_m, false, 0);
    m->msg_log_log_m.log_level_m.log_level_choice = lvl;
    m->msg_log_log_m.log_uint1tstr = (struct zcbor_string){.value = (const uint8_t *)msg, .len = strlen(msg)};
    return send_env(g_link.kind, "log");
}

bool link_send_audio_end(uint32_t aid)
{
    if (!g_link.welcomed) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_audio_end_m_c;
    struct msg_audio_end *m = &g_tx_env.envelope_msg_audio_end_m;
    set_hdr(&m->msg_audio_end_hdr_m, false, 0);
    m->msg_audio_end_audio_end_m.audio_end_uint0uint = aid;
    return send_env(g_link.kind, "audio_end");
}

bool link_send_kb_start(uint32_t kid, uint32_t sid, const uint8_t *keys, size_t len)
{
    if (!g_link.welcomed || len > 640) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_kb_start_m_c;
    struct msg_kb_start *m = &g_tx_env.envelope_msg_kb_start_m;
    set_hdr(&m->msg_kb_start_hdr_m, false, 0);
    m->msg_kb_start_kb_start_m.kb_start_uint0uint = kid;
    m->msg_kb_start_kb_start_m.kb_start_uint1uint = sid;
    m->msg_kb_start_kb_start_m.kb_start_uint2bstr = (struct zcbor_string){.value = keys, .len = len};
    return send_env(g_link.kind, "kb_start");
}

bool link_send_kb_stroke(uint32_t kid, uint32_t seq, const uint8_t *pts, size_t len)
{
    if (!g_link.welcomed || len > 1200) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_kb_stroke_m_c;
    struct msg_kb_stroke *m = &g_tx_env.envelope_msg_kb_stroke_m;
    set_hdr(&m->msg_kb_stroke_hdr_m, false, 0);
    m->msg_kb_stroke_kb_stroke_m.kb_stroke_uint0uint = kid;
    m->msg_kb_stroke_kb_stroke_m.kb_stroke_uint1uint = seq;
    m->msg_kb_stroke_kb_stroke_m.kb_stroke_uint2bstr = (struct zcbor_string){.value = pts, .len = len};
    return send_env(g_link.kind, "kb_stroke");
}

bool link_tx_pong(uint32_t re)
{
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_pong_m_c;
    set_hdr(&g_tx_env.envelope_msg_pong_m.msg_pong_hdr_m, true, re);
    return send_env(g_link.kind, "pong");
}

bool link_tx_ping(void)
{
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_ping_m_c;
    set_hdr(&g_tx_env.envelope_msg_ping_m.msg_ping_hdr_m, false, 0);
    return send_env(g_link.kind, "ping");
}

bool link_send_ui_state(const ui_report_t *r, uint32_t re)
{
    if (!g_link.welcomed) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_dbg_ui_state_m_c;
    struct msg_dbg_ui_state *m = &g_tx_env.envelope_msg_dbg_ui_state_m;
    set_hdr(&m->msg_dbg_ui_state_hdr_m, true, re);
    struct dbg_ui_state *s = &m->msg_dbg_ui_state_dbg_ui_state_m;
    s->dbg_ui_state_uint0tstr = zstr(r->screen);
    if (r->has_sel_sid) {
        s->dbg_ui_state_uint1uint_present = true;
        s->dbg_ui_state_uint1uint.dbg_ui_state_uint1uint = r->sel_sid;
    }
    if (r->has_rid) {
        s->dbg_ui_state_uint2uint_present = true;
        s->dbg_ui_state_uint2uint.dbg_ui_state_uint2uint = r->rid;
    }
    for (size_t i = 0; i < r->n_visible && i < 8; i++) s->dbg_ui_state_tstr_l_tstr[i] = zstr(r->visible[i]);
    s->dbg_ui_state_tstr_l_tstr_count = r->n_visible < 8 ? r->n_visible : 8;
    s->dbg_ui_state_uint4tstr = zstr(r->power);
    s->dbg_ui_state_uint5uint_present = true;
    s->dbg_ui_state_uint5uint.dbg_ui_state_uint5uint = r->fps;
    return send_env(g_link.kind, "dbg_ui_state");
}

bool link_send_screenshot_chunk(uint16_t w, uint16_t h, uint32_t offset, const uint8_t *data, size_t len,
                                bool last, uint32_t re)
{
    if (!g_link.welcomed) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_dbg_screenshot_chunk_m_c;
    struct msg_dbg_screenshot_chunk *m = &g_tx_env.envelope_msg_dbg_screenshot_chunk_m;
    set_hdr(&m->msg_dbg_screenshot_chunk_hdr_m, true, re);
    struct dbg_screenshot_chunk *c = &m->msg_dbg_screenshot_chunk_dbg_screenshot_chunk_m;
    c->dbg_screenshot_chunk_uint0uint = w;
    c->dbg_screenshot_chunk_uint1uint = h;
    c->dbg_screenshot_chunk_uint2uint = offset;
    c->dbg_screenshot_chunk_uint3bstr = (struct zcbor_string){.value = data, .len = len};
    c->dbg_screenshot_chunk_uint4bool = last;
    return send_env(g_link.kind, "screenshot_chunk");
}

bool link_send_fw_ack(int st, uint32_t next, const char *msg)
{
    if (!g_link.welcomed) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_fw_ack_m_c;
    struct msg_fw_ack *m = &g_tx_env.envelope_msg_fw_ack_m;
    set_hdr(&m->msg_fw_ack_hdr_m, false, 0);
    m->msg_fw_ack_fw_ack_m.fw_ack_fw_status_m.fw_status_choice = st;
    m->msg_fw_ack_fw_ack_m.fw_ack_uint1uint = next;
    if (msg && msg[0]) {
        m->msg_fw_ack_fw_ack_m.fw_ack_uint2tstr_present = true;
        m->msg_fw_ack_fw_ack_m.fw_ack_uint2tstr.fw_ack_uint2tstr = zstr(msg);
    }
    dbg_event("ota", "ota_ack", "\"st\":%d,\"next\":%u", st, (unsigned)next);
    return send_env(g_link.kind, "fw_ack");
}

bool link_send_power(const link_power_t *p)
{
    if (!g_link.welcomed || model_peer_minor() < LINK_POWER_MINOR) return false;
    memset(&g_tx_env, 0, sizeof g_tx_env);
    g_tx_env.envelope_choice = envelope_msg_power_m_c;
    struct msg_power *m = &g_tx_env.envelope_msg_power_m;
    set_hdr(&m->msg_power_hdr_m, false, 0);
    struct power *b = &m->msg_power_power_m;
    b->power_uint0uint = p->mv;
    b->power_uint1uint = p->mvf;
    b->power_uint2uint = p->vin;
    b->power_uint3bool = p->chg;
    b->power_uint4bool = p->usb;
    b->power_uint5uint = p->pct;
    b->power_uint6uint = p->on;
    b->power_uint7uint = p->dim;
    b->power_uint8uint = p->off;
    b->power_uint9uint = p->deep;
    b->power_uint10uint = p->lk;
    b->power_uint11uint = p->ble;
    b->power_uint12uint = p->itvl;
    b->power_uint13uint = p->seq;
    b->power_uint14uint = p->nd;
    return send_env(g_link.kind, "power");
}
