#include "model.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"

MODEL_EXT_BSS static session_t s_sessions[MODEL_MAX_SESSIONS];
static bool s_used[MODEL_MAX_SESSIONS];
static attention_t s_att[MODEL_MAX_ATTENTION];
static bool s_att_used[MODEL_MAX_ATTENTION];
static settings_t s_settings;
static uint32_t s_rev;

static char s_host[33];
static char s_replies[MODEL_MAX_REPLIES][64]; /* 24 characters, UTF-8 */
static int s_n_replies;
static bool s_typing;
static model_session_hook_t s_hook;

void model_init(void)
{
    memset(s_used, 0, sizeof s_used);
    memset(s_att_used, 0, sizeof s_att_used);
    memset(&s_settings, 0, sizeof s_settings);
    s_host[0] = 0;
    s_n_replies = 0;
    s_typing = false;
    s_settings.privacy = PRIVACY_PREVIEW;
    s_rev = 1;
}

void model_set_host(const char *host)
{
    snprintf(s_host, sizeof s_host, "%s", host ? host : "");
    s_rev++;
}
const char *model_host(void) { return s_host; }

void model_set_replies(const char *const *replies, int n, bool typing)
{
    s_typing = typing;
    s_n_replies = 0;
    for (int i = 0; i < n && i < MODEL_MAX_REPLIES; i++)
        snprintf(s_replies[s_n_replies++], sizeof s_replies[0], "%s", replies[i]);
    s_rev++;
}

int model_reply_count(void) { return s_n_replies; }
const char *model_reply(int i) { return i >= 0 && i < s_n_replies ? s_replies[i] : ""; }
bool model_can_type(void) { return s_typing; }

static bool s_voice;
void model_set_voice(bool on)
{
    s_voice = on;
    s_rev++;
}
bool model_voice(void) { return s_voice; }

static bool s_keys;
void model_set_keys(bool on)
{
    s_keys = on;
    s_rev++;
}
bool model_keys(void) { return s_keys; }

uint32_t model_rev(void) { return s_rev; }

void model_copy_str(char *dst, size_t cap, const uint8_t *src, size_t len)
{
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

const char *sess_state_name(sess_state_t s)
{
    static const char *const names[] = {"start", "work", "input", "error", "done", "idle", "ended"};
    return (unsigned)s < 7 ? names[s] : "?";
}

const char *agent_name(agent_t a)
{
    switch (a) {
    case AGENT_COPILOT: return "copilot";
    case AGENT_CLAUDE: return "claude";
    case AGENT_CLOUD: return "cloud";
    default: return "other";
    }
}

/* ───────── sessions ───────── */

static int find_slot(uint32_t sid)
{
    for (int i = 0; i < MODEL_MAX_SESSIONS; i++)
        if (s_used[i] && s_sessions[i].sid == sid) return i;
    return -1;
}

static void apply(session_t *dst, const session_t *src, uint32_t present)
{
    if (present & SF_AGENT) dst->agent = src->agent;
    if (present & SF_STATE) dst->state = src->state;
    if (present & SF_SUB) dst->sub = src->sub;
    if (present & SF_REPO) memcpy(dst->repo, src->repo, sizeof dst->repo);
    if (present & SF_BRANCH) memcpy(dst->branch, src->branch, sizeof dst->branch);
    if (present & SF_TOOL) memcpy(dst->tool, src->tool, sizeof dst->tool);
    if (present & SF_T0) dst->t0 = src->t0;
    if (present & SF_TST) dst->tst = src->tst;
    if (present & SF_SUMMARY) memcpy(dst->summary, src->summary, sizeof dst->summary);
    if (present & SF_SEEN) dst->seen = src->seen;
    if (present & SF_HOST) dst->host = src->host;
    if (present & SF_HN) memcpy(dst->hn, src->hn, sizeof dst->hn);
    if (present & SF_SNZ) dst->snz = src->snz;
    if (present & SF_NTF) dst->ntf = src->ntf;
    if (present & SF_N) {
        if (src->n != dst->n) dst->step = STEP_RUNNING; /* a new step runs until its `ok` arrives */
        dst->n = src->n;
    }
    if (present & SF_OK) dst->step = src->step;
    if (present & SF_Q) memcpy(dst->q, src->q, sizeof dst->q);
    if (present & SF_RP) {
        memcpy(dst->rp, src->rp, sizeof dst->rp);
        dst->n_rp = src->n_rp;
    }
    if (present & SF_NO_SCROLL) dst->no_scroll = src->no_scroll;
}

void model_upsert(const session_t *delta, uint32_t present)
{
    int slot = find_slot(delta->sid);
    bool is_new = slot < 0;
    if (slot < 0) {
        for (int i = 0; i < MODEL_MAX_SESSIONS && slot < 0; i++)
            if (!s_used[i]) slot = i;
        if (slot < 0) {
            dbg_event_at(DBG_WARN, "model", "session_dropped", "\"sid\":%u,\"why\":\"pool_full\"", (unsigned)delta->sid);
            return;
        }
        memset(&s_sessions[slot], 0, sizeof s_sessions[slot]);
        s_sessions[slot].sid = delta->sid;
        s_sessions[slot].agent = AGENT_OTHER;
        s_sessions[slot].state = SESS_IDLE;
        s_used[slot] = true;
    }
    session_t *dst = &s_sessions[slot];
    session_t prev = *dst;
    sess_state_t before = dst->state;
    apply(dst, delta, present);
    if (present & (SF_N | SF_OK | SF_SUMMARY | SF_TOOL)) {
        const char *text = (present & SF_SUMMARY) && delta->summary[0] ? delta->summary
                           : (present & SF_TOOL) && delta->tool[0]       ? delta->tool
                           : dst->summary[0]                             ? dst->summary
                                                                         : dst->tool;
        steps_update(&dst->steps, present & SF_N, dst->n, present & SF_OK, dst->step == STEP_OK, text);
    }
    s_rev++;
    dbg_event("model", "session", "\"sid\":%u,\"st\":\"%s\",\"prev\":\"%s\",\"label\":%s,\"host\":%u,\"hn\":%s",
              (unsigned)dst->sid, sess_state_name(dst->state), sess_state_name(before), dbg_q(dst->repo),
              (unsigned)dst->host, dbg_q(dst->hn));
    if (s_hook) s_hook(is_new ? NULL : &prev, dst, present);
}

void model_set_session_hook(model_session_hook_t hook) { s_hook = hook; }

bool model_session_snoozed(const session_t *s, uint32_t now_unix) { return s && now_unix != 0 && s->snz > now_unix; }

void model_replace_all(const session_t *list, size_t n)
{
    memset(s_used, 0, sizeof s_used);
    for (size_t i = 0; i < n && i < MODEL_MAX_SESSIONS; i++) {
        s_sessions[i] = list[i];
        steps_seed(&s_sessions[i].steps, list[i].n, (step_state_t)list[i].step,
                   list[i].summary[0] ? list[i].summary : list[i].tool);
        s_used[i] = true;
    }
    for (int a = 0; a < MODEL_MAX_ATTENTION; a++)
        if (s_att_used[a] && find_slot(s_att[a].sid) < 0) s_att_used[a] = false;
    s_rev++;
    dbg_event("model", "snapshot", "\"sessions\":%u", (unsigned)(n > MODEL_MAX_SESSIONS ? MODEL_MAX_SESSIONS : n));
}

void model_remove(uint32_t sid)
{
    int slot = find_slot(sid);
    if (slot < 0) return;
    s_used[slot] = false;
    for (int a = 0; a < MODEL_MAX_ATTENTION; a++)
        if (s_att_used[a] && s_att[a].sid == sid) s_att_used[a] = false;
    s_rev++;
    dbg_event("model", "gone", "\"sid\":%u", (unsigned)sid);
}

size_t model_count(void)
{
    size_t n = 0;
    for (int i = 0; i < MODEL_MAX_SESSIONS; i++) n += s_used[i];
    return n;
}

const session_t *model_find(uint32_t sid)
{
    int slot = find_slot(sid);
    return slot < 0 ? NULL : &s_sessions[slot];
}

const session_t *model_at(uint8_t slot) { return (slot < MODEL_MAX_SESSIONS && s_used[slot]) ? &s_sessions[slot] : NULL; }

/* Priority from DESIGN §4: needs_input > error > done(unseen) > working > idle > done(seen). */
static int rank(const session_t *s)
{
    switch (s->state) {
    case SESS_INPUT: return 0;
    case SESS_ERROR: return 1;
    case SESS_DONE: return s->seen ? 5 : 2;
    case SESS_WORK:
    case SESS_START: return 3;
    case SESS_IDLE: return 4;
    default: return 6;
    }
}

static bool before(const session_t *a, const session_t *b)
{
    int ra = rank(a), rb = rank(b);
    if (ra != rb) return ra < rb;
    if (a->tst != b->tst) return a->tst > b->tst;
    return a->sid < b->sid;
}

size_t model_sorted(uint8_t *idx)
{
    size_t n = 0;
    for (int i = 0; i < MODEL_MAX_SESSIONS; i++) {
        if (!s_used[i]) continue;
        size_t j = n++;
        while (j > 0 && before(&s_sessions[i], &s_sessions[idx[j - 1]])) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = (uint8_t)i;
    }
    return n;
}

model_counts_t model_counts(void)
{
    model_counts_t c = {0};
    for (int i = 0; i < MODEL_MAX_SESSIONS; i++) {
        if (!s_used[i]) continue;
        switch (s_sessions[i].state) {
        case SESS_INPUT: c.waiting++; break;
        case SESS_ERROR: c.error++; break;
        case SESS_WORK:
        case SESS_START: c.running++; break;
        case SESS_DONE: c.done_unseen += !s_sessions[i].seen; break;
        default: break;
        }
    }
    return c;
}

/* ───────── attention ───────── */

void model_attention_add(const attention_t *a)
{
    int slot = -1;
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i] && s_att[i].rid == a->rid) slot = i; /* same request: replace */
    }
    for (int i = 0; i < MODEL_MAX_ATTENTION && slot < 0; i++)
        if (!s_att_used[i]) slot = i;
    if (slot < 0) slot = 0; /* queue full: oldest is least likely to still be live */
    attention_t old = s_att[slot];
    bool had = s_att_used[slot] && old.rid == a->rid;
    s_att[slot] = *a;
    if (had) { /* a re-sent request keeps the answer already given */
        s_att[slot].answer_sent = old.answer_sent;
        s_att[slot].answer = old.answer;
        s_att[slot].answer_opt = old.answer_opt;
    }
    s_att_used[slot] = true;
    s_rev++;
    dbg_event("model", "attention", "\"rid\":%u,\"sid\":%u,\"kind\":%d", (unsigned)a->rid, (unsigned)a->sid, (int)a->kind);
}

bool model_attention_clear(uint32_t rid)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i] && s_att[i].rid == rid) {
            s_att_used[i] = false;
            s_rev++;
            dbg_event("model", "attention_clear", "\"rid\":%u", (unsigned)rid);
            return true;
        }
    }
    return false;
}

void model_attention_clear_all(void)
{
    int n = 0;
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        n += s_att_used[i];
        s_att_used[i] = false;
    }
    if (!n) return;
    s_rev++;
    dbg_event("model", "attention_clear_all", "\"n\":%d", n);
}

const attention_t *model_attention_find(uint32_t rid)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++)
        if (s_att_used[i] && s_att[i].rid == rid) return &s_att[i];
    return NULL;
}

const attention_t *model_attention_for_sid(uint32_t sid)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++)
        if (s_att_used[i] && s_att[i].sid == sid) return &s_att[i];
    return NULL;
}

const attention_t *model_attention_first(void)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++)
        if (s_att_used[i]) return &s_att[i];
    return NULL;
}

void model_attention_set_answer(uint32_t rid, uint8_t answer, uint8_t opt)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i] && s_att[i].rid == rid) {
            s_att[i].answer_sent = answer != ATT_ANSWER_NONE;
            s_att[i].answer = answer;
            s_att[i].answer_opt = opt;
            s_rev++;
        }
    }
}

void model_attention_expire(uint32_t now_unix)
{
    if (!now_unix) return;
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i] && s_att[i].exp && now_unix > s_att[i].exp) {
            dbg_event("model", "attention_expired", "\"rid\":%u", (unsigned)s_att[i].rid);
            s_att_used[i] = false;
            s_rev++;
        }
    }
}

settings_t *model_settings(void) { return &s_settings; }

void model_settings_changed(void)
{
    s_rev++;
    dbg_event("model", "settings", "\"privacy\":%d,\"alert\":%d,\"quiet_until\":%u", (int)s_settings.privacy,
              (int)s_settings.alert, (unsigned)s_settings.quiet_until);
}

/* now_unix 0 = clock unknown (hal_rtc_get_unix): a timed quiet can never be judged, so it is off. */
bool model_quiet_active(uint32_t now_unix)
{
    return now_unix != 0 && s_settings.quiet_until != 0 && now_unix < s_settings.quiet_until;
}
