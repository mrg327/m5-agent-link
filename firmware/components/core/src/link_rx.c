/* Daemon → device messages: decoded envelope in, model/app notifications out. */
#include <string.h>

#include "link_internal.h"

#define STR(zs, dst) model_copy_str((dst), sizeof(dst), (zs).value, (zs).len)

static const struct hdr_r *env_hdr(const struct envelope_r *e)
{
#define H(n) case envelope_##n##_m_c: return &e->envelope_##n##_m.n##_hdr_m;
    switch (e->envelope_choice) {
    H(msg_hello) H(msg_welcome) H(msg_snapshot) H(msg_session_upd) H(msg_gone) H(msg_attention)
    H(msg_clear) H(msg_cmd) H(msg_result) H(msg_detail) H(msg_cfg) H(msg_batt) H(msg_ping) H(msg_pong)
    H(msg_log) H(msg_audio_start) H(msg_audio) H(msg_audio_end) H(msg_transcript)
    H(msg_dbg_ui_state_req) H(msg_dbg_ui_state) H(msg_dbg_screenshot_req) H(msg_dbg_screenshot_chunk)
    H(msg_dbg_inject_input) H(msg_dbg_set_log_level)
    }
#undef H
    return NULL;
}

/* Wire session → model session; returns the mask of fields that were present. */
static uint32_t convert_session(const struct session *in, session_t *out)
{
    uint32_t p = 0;
    memset(out, 0, sizeof *out);
    out->sid = in->session_uint0uint;
    out->agent = AGENT_OTHER;
    out->state = SESS_IDLE;
    if (in->session_agent_m_present) {
        out->agent = (agent_t)in->session_agent_m.session_agent_m.agent_choice;
        p |= SF_AGENT;
    }
    if (in->session_sess_state_m_present) {
        out->state = (sess_state_t)in->session_sess_state_m.session_sess_state_m.sess_state_choice;
        p |= SF_STATE;
    }
    if (in->session_input_sub_m_present) {
        out->sub = (uint8_t)in->session_input_sub_m.session_input_sub_m.input_sub_choice;
        p |= SF_SUB;
    }
    if (in->session_uint4tstr_present) { STR(in->session_uint4tstr.session_uint4tstr, out->repo); p |= SF_REPO; }
    if (in->session_uint5tstr_present) { STR(in->session_uint5tstr.session_uint5tstr, out->branch); p |= SF_BRANCH; }
    if (in->session_uint6tstr_present) { STR(in->session_uint6tstr.session_uint6tstr, out->tool); p |= SF_TOOL; }
    if (in->session_uint7uint_present) { out->t0 = in->session_uint7uint.session_uint7uint; p |= SF_T0; }
    if (in->session_uint8uint_present) { out->tst = in->session_uint8uint.session_uint8uint; p |= SF_TST; }
    if (in->session_uint9tstr_present) { STR(in->session_uint9tstr.session_uint9tstr, out->summary); p |= SF_SUMMARY; }
    if (in->session_uint17tstr_present) { STR(in->session_uint17tstr.session_uint17tstr, out->q); p |= SF_Q; }
    if (in->session_tstr_l_present) {
        size_t n = in->session_tstr_l.session_tstr_l_tstr_count;
        for (size_t i = 0; i < n && i < 3; i++) STR(in->session_tstr_l.session_tstr_l_tstr[i], out->rp[i]);
        out->n_rp = (uint8_t)(n < 3 ? n : 3);
        p |= SF_RP;
    }
    if (in->session_uint10bool_present) { out->seen = in->session_uint10bool.session_uint10bool; p |= SF_SEEN; }
    if (in->session_uint11uint_present) { out->host = (uint8_t)in->session_uint11uint.session_uint11uint; p |= SF_HOST; }
    if (in->session_uint12tstr_present) { STR(in->session_uint12tstr.session_uint12tstr, out->hn); p |= SF_HN; }
    if (in->session_uint13uint_present) { out->snz = in->session_uint13uint.session_uint13uint; p |= SF_SNZ; }
    if (in->session_uint14bool_present) { out->ntf = in->session_uint14bool.session_uint14bool; p |= SF_NTF; }
    if (in->session_uint15uint_present) { out->n = in->session_uint15uint.session_uint15uint; p |= SF_N; }
    if (in->session_uint16bool_present) {
        out->step = in->session_uint16bool.session_uint16bool ? STEP_OK : STEP_FAIL;
        p |= SF_OK;
    }
    return p;
}

static void apply_cfg(const struct device_cfg *c)
{
    settings_t *s = model_settings();
    if (c->device_cfg_privacy_m_present)
        s->privacy = (privacy_t)c->device_cfg_privacy_m.device_cfg_privacy_m.privacy_choice;
    if (c->device_cfg_alert_mode_m_present)
        s->alert = (alert_mode_t)c->device_cfg_alert_mode_m.device_cfg_alert_mode_m.alert_mode_choice;
    if (c->device_cfg_uint2uint_present) s->quiet_until = c->device_cfg_uint2uint.device_cfg_uint2uint;
    if (c->device_cfg_uint3uint_present) {
        uint32_t b = c->device_cfg_uint3uint.device_cfg_uint3uint;
        hal_display_set_brightness(b > 255 ? 255 : (uint8_t)b);
    }
    if (c->device_cfg_uint4tstr_present) STR(c->device_cfg_uint4tstr.device_cfg_uint4tstr, s->handle);
    dbg_event("link", "cfg", "\"privacy\":%d,\"alert\":%d,\"quiet_until\":%u", (int)s->privacy, (int)s->alert,
              (unsigned)s->quiet_until);
    if (g_link.hooks.on_cfg) g_link.hooks.on_cfg();
}

static void on_welcome(const struct welcome *w)
{
    if (w->welcome_major_l_major != 0) {
        dbg_event_at(DBG_ERR, "link", "proto_mismatch", "\"major\":%u", (unsigned)w->welcome_major_l_major);
        return;
    }
    char host[33];
    STR(w->welcome_host_info_m.host_info_uint1tstr, host);
    model_set_host(host);
    hal_rtc_set_unix(w->welcome_time_sync_m.time_sync_uint0uint);
    model_settings()->tz_off_min = w->welcome_time_sync_m.time_sync_uint1int;
    g_link.welcomed = true;
    dbg_event("link", "welcome", "\"host\":%s,\"unix\":%u,\"tz_off\":%d", dbg_q(host),
              (unsigned)w->welcome_time_sync_m.time_sync_uint0uint, (int)w->welcome_time_sync_m.time_sync_uint1int);
    apply_cfg(&w->welcome_device_cfg_m);
    /* Quick replies (proto 0.4): their presence means the daemon can type into terminals. */
    {
        static char buf[MODEL_MAX_REPLIES][64];
        const char *list[MODEL_MAX_REPLIES];
        int n = 0;
        if (w->welcome_tstr_l_present)
            for (size_t i = 0; i < w->welcome_tstr_l.welcome_tstr_l_tstr_count && n < MODEL_MAX_REPLIES; i++) {
                STR(w->welcome_tstr_l.welcome_tstr_l_tstr[i], buf[n]);
                list[n] = buf[n];
                n++;
            }
        model_set_replies(list, n, w->welcome_tstr_l_present);
    }
    /* A welcome starts a new link, possibly with a restarted daemon whose request ids and
       session ids begin again. A card kept from before could then name a different request
       (issue 20261109-1173), so drop them all; the daemon re-sends the open ones next. */
    model_attention_clear_all();
    if (g_link.hooks.on_link) g_link.hooks.on_link(true);
}

static void on_snapshot(const struct snapshot *s)
{
    static session_t list[MODEL_MAX_SESSIONS];
    size_t n = s->snapshot_session_m_l_session_m_count;
    if (n > MODEL_MAX_SESSIONS) n = MODEL_MAX_SESSIONS;
    for (size_t i = 0; i < n; i++) convert_session(&s->snapshot_session_m_l_session_m[i], &list[i]);
    model_replace_all(list, n);
}

static void on_attention(const struct attention *in)
{
    attention_t a;
    memset(&a, 0, sizeof a);
    a.rid = in->attention_uint0uint;
    a.sid = in->attention_uint1uint;
    a.kind = (att_kind_t)in->attention_att_kind_m.att_kind_choice;
    if (in->attention_uint3tstr_present) STR(in->attention_uint3tstr.attention_uint3tstr, a.title);
    if (in->attention_uint4tstr_present) STR(in->attention_uint4tstr.attention_uint4tstr, a.text);
    if (in->attention_tstr_l_present) {
        size_t n = in->attention_tstr_l.attention_tstr_l_tstr_count;
        for (size_t i = 0; i < n && i < 4; i++) STR(in->attention_tstr_l.attention_tstr_l_tstr[i], a.opts[a.n_opts++]);
    }
    for (size_t i = 0; i < in->attention_action_m_l_action_m_count; i++)
        a.acts |= (uint8_t)(1u << in->attention_action_m_l_action_m[i].action_choice);
    if (in->attention_uint7uint_present) a.exp = in->attention_uint7uint.attention_uint7uint;
    if (in->attention_uint8uint_present) a.grant_mask = (uint8_t)in->attention_uint8uint.attention_uint8uint;
    model_attention_add(&a);
    if (g_link.hooks.on_attention) g_link.hooks.on_attention(model_attention_find(a.rid));
}

static void on_result(const struct msg_result *m, uint32_t re_id, bool has_re)
{
    const struct result *r = &m->msg_result_result_m;
    int op = LINK_OP_UNKNOWN;
    uint32_t sid = 0, rid = 0;
    if (has_re) {
        pending_cmd_t *p = &g_link.pending[re_id % LINK_MAX_PENDING];
        if (p->id == re_id) {
            op = p->op;
            sid = p->sid;
            rid = p->rid;
            p->id = 0;
        }
    }
    int err = r->result_err_m_present ? (int)r->result_err_m.result_err_m.result_err_choice : -1;
    char msg[48] = "";
    if (r->result_uint2tstr_present) STR(r->result_uint2tstr.result_uint2tstr, msg);
    dbg_event("link", "result", "\"ok\":%s,\"err\":%d,\"op\":%d,\"msg\":%s", r->result_uint0bool ? "true" : "false",
              err, op, dbg_q(msg));
    if (g_link.hooks.on_result) g_link.hooks.on_result(op, sid, rid, r->result_uint0bool, err, msg);
    if (r->result_scroll_pos_m_present && g_link.hooks.on_scroll_pos) {
        /* terminal content: handed to the UI, never logged */
        const struct scroll_pos *p = &r->result_scroll_pos_m.result_scroll_pos_m;
        char preview[64] = "";
        if (p->scroll_pos_uint3tstr_present) STR(p->scroll_pos_uint3tstr.scroll_pos_uint3tstr, preview);
        g_link.hooks.on_scroll_pos(sid, p->scroll_pos_uint0uint, p->scroll_pos_uint1bool, p->scroll_pos_uint2uint_present,
                                   p->scroll_pos_uint2uint.scroll_pos_uint2uint, preview);
    }
}

void link_rx_dispatch(const struct envelope_r *e, uint32_t now_ms)
{
    (void)now_ms;
    const struct hdr_r *h = env_hdr(e);
    uint32_t id = h ? h->hdr_uint1uint : 0;
    bool has_re = h && h->hdr_uint2uint_present;
    uint32_t re = has_re ? h->hdr_uint2uint.hdr_uint2uint : 0;
    /* Replay protection arrives with the AEAD layer; until then only report odd ids. */
    if (id <= g_link.last_rx_id)
        dbg_event_at(DBG_WARN, "link", "rx_id_not_monotonic", "\"id\":%u,\"last\":%u", (unsigned)id, (unsigned)g_link.last_rx_id);
    else
        g_link.last_rx_id = id;
    dbg_event_at(DBG_DEBUG, "link", "rx", "\"type\":%d,\"id\":%u", (int)e->envelope_choice, (unsigned)id);

    switch (e->envelope_choice) {
    case envelope_msg_welcome_m_c: on_welcome(&e->envelope_msg_welcome_m.msg_welcome_welcome_m); break;
    case envelope_msg_snapshot_m_c: on_snapshot(&e->envelope_msg_snapshot_m.msg_snapshot_snapshot_m); break;
    case envelope_msg_session_upd_m_c: {
        session_t s;
        uint32_t present = convert_session(&e->envelope_msg_session_upd_m.msg_session_upd_session_m, &s);
        model_upsert(&s, present);
        break;
    }
    case envelope_msg_gone_m_c: model_remove(e->envelope_msg_gone_m.msg_gone_gone_m.gone_uint0uint); break;
    case envelope_msg_attention_m_c: on_attention(&e->envelope_msg_attention_m.msg_attention_attention_m); break;
    case envelope_msg_clear_m_c: {
        const struct clear *c = &e->envelope_msg_clear_m.msg_clear_clear_m;
        if (model_attention_clear(c->clear_uint0uint) && g_link.hooks.on_cleared)
            g_link.hooks.on_cleared(c->clear_uint0uint, (int)c->clear_why_m.clear_why_choice);
        break;
    }
    case envelope_msg_result_m_c: on_result(&e->envelope_msg_result_m, re, has_re); break;
    case envelope_msg_cfg_m_c: apply_cfg(&e->envelope_msg_cfg_m.msg_cfg_device_cfg_m); break;
    case envelope_msg_ping_m_c: link_tx_pong(id); break;
    case envelope_msg_dbg_ui_state_req_m_c: dbg_on_ui_state_req(id); break;
    case envelope_msg_dbg_screenshot_req_m_c: dbg_on_screenshot_req(id); break;
    case envelope_msg_dbg_inject_input_m_c: {
        const struct dbg_inject_input *in = &e->envelope_msg_dbg_inject_input_m.msg_dbg_inject_input_dbg_inject_input_m;
        bool has_xy = in->dbg_inject_input_uint1int_present && in->dbg_inject_input_uint2int_present;
        dbg_on_inject((int)in->dbg_inject_input_input_event_m.input_event_choice, has_xy,
                      has_xy ? in->dbg_inject_input_uint1int.dbg_inject_input_uint1int : 0,
                      has_xy ? in->dbg_inject_input_uint2int.dbg_inject_input_uint2int : 0);
        break;
    }
    case envelope_msg_dbg_set_log_level_m_c:
        dbg_set_level((dbg_level_t)e->envelope_msg_dbg_set_log_level_m.msg_dbg_set_log_level_dbg_set_log_level_m
                          .dbg_set_log_level_log_level_m.log_level_choice);
        break;
    case envelope_msg_detail_m_c:
        dbg_event("link", "detail_ignored", NULL); /* "reveal full text" is not implemented yet */
        break;
    default: break; /* hello, batt, audio, ... are not for the device */
    }
}
