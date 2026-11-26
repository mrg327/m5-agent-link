#include "model.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ui_icons.h"

MODEL_EXT_BSS static session_t s_sessions[MODEL_MAX_SESSIONS];
static bool s_used[MODEL_MAX_SESSIONS];
/* PSRAM: a standing rule makes each ~0.5 KB */
MODEL_EXT_BSS static attention_t s_att[MODEL_MAX_ATTENTION];
/* each request's questions, by slot: ~3.4 KB */
MODEL_EXT_BSS static ask_q_t s_att_qs[MODEL_MAX_ATTENTION][MODEL_MAX_QS];
static bool s_att_used[MODEL_MAX_ATTENTION];
/* What model_attention_clear_all() dropped, so the daemon's re-sends after a welcome are known. */
typedef struct {
    uint32_t rid, sid, hash;
    bool sent, skipped;
    uint8_t answer, answer_opt;
} att_known_t;
static att_known_t s_known[MODEL_MAX_ATTENTION];
static int s_n_known;
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
    s_n_known = 0;
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

static bool s_pointer;
void model_set_pointer(bool on)
{
    s_pointer = on;
    s_rev++;
}
bool model_pointer(void) { return s_pointer; }

uint32_t model_rev(void) { return s_rev; }

void model_utf8_drop_partial(char *s)
{
    size_t n = strlen(s), i = n;
    while (i > 0 && ((uint8_t)s[i - 1] & 0xc0) == 0x80) i--;
    if (i == 0) return;
    uint8_t lead = (uint8_t)s[i - 1];
    size_t need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
    if (n - (i - 1) < need) s[i - 1] = 0;
}

#define HIDDEN_MARK "\xEF\xBF\xBD" /* U+FFFD: text the daemon could not have us draw */

void model_show_hidden(char *s, size_t len)
{
    for (size_t i = 0; i + 3 <= len; i++) {
        if (memcmp(s + i, HIDDEN_MARK, 3) == 0) {
            memcpy(s + i, ICON_EYE_OFF, 3); /* the same length, so the copy stays in place */
            i += 2;
        }
    }
}

void model_copy_str(char *dst, size_t cap, const uint8_t *src, size_t len)
{
    static const char ellipsis[] = "\xE2\x80\xA6";
    if (cap == 0) return;
    bool cut = len >= cap;
    bool mark = cut && cap > sizeof ellipsis;
    if (cut) len = cap - (mark ? sizeof ellipsis : 1);
    memcpy(dst, src, len);
    dst[len] = 0;
    model_utf8_drop_partial(dst);
    if (mark) { /* a cut text never reads as whole (an older daemon cuts by characters) */
        len = strlen(dst);
        while (len > 0 && dst[len - 1] == ' ') len--;
        memcpy(dst + len, ellipsis, sizeof ellipsis);
    }
    model_show_hidden(dst, strlen(dst));
}

uint32_t model_rule_hash(const char *rule, uint8_t scope, bool wide)
{
    uint32_t h = 0x811c9dc5u;
    const uint8_t tail[3] = {0, scope, wide ? 1 : 0};
    for (const uint8_t *p = (const uint8_t *)rule; *p; p++) h = (h ^ *p) * 0x01000193u;
    for (int i = 0; i < 3; i++) h = (h ^ tail[i]) * 0x01000193u;
    return h;
}

void model_attention_set_rule(attention_t *a, const uint8_t *rule, size_t len, uint32_t scope,
                              bool wide)
{
    a->has_rule = false;
    a->rule[0] = 0;
    if (!rule || len == 0 || len > MODEL_RULE_CAP || scope > MODEL_RULE_PROJECT) return;
    /* every byte must show as itself: no control, no UTF-8, no NUL */
    for (size_t i = 0; i < len; i++)
        if (!(rule[i] == '\n' || (rule[i] >= 0x20 && rule[i] < 0x7f))) return;
    if (rule[0] == '\n' || rule[len - 1] == '\n') return;
    memcpy(a->rule, rule, len);
    a->rule[len] = 0;
    a->rule_scope = (uint8_t)scope;
    a->rule_wide = wide;
    a->rule_hash = model_rule_hash(a->rule, a->rule_scope, a->rule_wide);
    a->has_rule = true;
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
    case AGENT_PI: return "pi";
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
    /* the session moved on */
    if ((present & SF_STATE) && src->state != dst->state) dst->back = false;
    /* the last turn's summary is not this one's */
    if (present & SF_STATE) memset(&dst->sum, 0, sizeof dst->sum);
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
    if (present & SF_QD) memcpy(dst->queued, src->queued, sizeof dst->queued);
    if (present & SF_LM) { /* a delta carries only the keys that changed */
        if (src->lm_keys & 1) dst->lm = src->lm;
        if (src->lm_keys & 2) dst->ll = src->ll;
        if (src->lm_keys & 4) memcpy(dst->lp, src->lp, sizeof dst->lp);
    }
    if (present & SF_RP) {
        memcpy(dst->rp, src->rp, sizeof dst->rp);
        dst->n_rp = src->n_rp;
    }
    if (present & SF_ETA) dst->eta = src->eta;
    if (present & SF_EXIT) dst->exit_code = src->exit_code;
    if (present & SF_CTX) {
        dst->ctx_known = src->ctx <= 100; /* MODEL_CTX_UNKNOWN clears it */
        dst->ctx = dst->ctx_known ? src->ctx : 0;
    }
    if (present & SF_MDL) memcpy(dst->mdl, src->mdl, sizeof dst->mdl);
    if (present & SF_PM) {
        dst->pm = src->pm;
        dst->pm_known = true;
    }
    if (present & SF_PR) dst->pr = src->pr;
    if (present & SF_PRS) dst->prs = src->prs;
    if (present & SF_HUSH) dst->hush = src->hush;
    if (present & SF_DONE) dst->sum = src->sum;
    if (present & SF_NO_SCROLL) dst->no_scroll = src->no_scroll;
    if (present & SF_BACK) {
        dst->back = src->back;
        dst->back_why = src->back_why;
    }
}

/* The pool is full (UX-PROFILE C3): frees the slot of the session that matters least, so a new
 * session's request is never invisible. Only a session nothing is pending on may go: no request,
 * not working, not failed, not holding an unseen finish; an ended one first, then the one whose
 * state changed longest ago. The daemon still has it: its next delta brings it back. Returns -1
 * when every session still matters. */
static int evict_slot(void)
{
    int best = -1;
    for (int i = 0; i < MODEL_MAX_SESSIONS; i++) {
        const session_t *s = &s_sessions[i];
        if (!s_used[i] || model_attention_for_sid(s->sid)) continue;
        bool ended = s->state == SESS_ENDED;
        if (!ended && s->state != SESS_IDLE && !(s->state == SESS_DONE && s->seen)) continue;
        if (best >= 0) {
            const session_t *b = &s_sessions[best];
            bool b_ended = b->state == SESS_ENDED;
            if (b_ended && !ended) continue;
            if (b_ended == ended && b->tst <= s->tst) continue;
        }
        best = i;
    }
    if (best < 0) return -1;
    dbg_event_at(DBG_WARN, "model", "session_evicted", "\"sid\":%u,\"why\":\"pool_full\"",
                 (unsigned)s_sessions[best].sid);
    model_remove(s_sessions[best].sid);
    return best;
}

void model_upsert(const session_t *delta, uint32_t present)
{
    int slot = find_slot(delta->sid);
    bool is_new = slot < 0;
    if (slot < 0) {
        for (int i = 0; i < MODEL_MAX_SESSIONS && slot < 0; i++)
            if (!s_used[i]) slot = i;
        if (slot < 0) slot = evict_slot(); /* a new session, and with it its request, stays seen */
        if (slot < 0) {
            dbg_event_at(DBG_WARN, "model", "session_dropped", "\"sid\":%u,\"why\":\"pool_full\"",
                         (unsigned)delta->sid);
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
                           : (present & SF_TOOL) && delta->tool[0]     ? delta->tool
                           : dst->summary[0]                           ? dst->summary
                                                                       : dst->tool;
        steps_update(&dst->steps, present & SF_N, dst->n, present & SF_OK, dst->step == STEP_OK,
                     text);
    }
    s_rev++;
    dbg_event("model", "session",
              "\"sid\":%u,\"st\":\"%s\",\"prev\":\"%s\",\"label\":%s,\"host\":%u,\"hn\":%s",
              (unsigned)dst->sid, sess_state_name(dst->state), sess_state_name(before),
              dbg_q(dst->repo), (unsigned)dst->host, dbg_q(dst->hn));
    if (s_hook) s_hook(is_new ? NULL : &prev, dst, present);
}

void model_set_session_hook(model_session_hook_t hook) { s_hook = hook; }

bool model_session_snoozed(const session_t *s, uint32_t now_unix)
{ return s && now_unix != 0 && s->snz > now_unix; }

void model_replace_all(const session_t *list, size_t n)
{
    /* "On your computer" survives a reconnect for a session still in the same state (device-side
     * flag) */
    struct back_s {
        uint32_t sid;
        sess_state_t st;
        uint8_t why;
    } back[MODEL_MAX_SESSIONS];
    int nb = 0;
    for (int i = 0; i < MODEL_MAX_SESSIONS; i++)
        if (s_used[i] && s_sessions[i].back)
            back[nb++] =
                (struct back_s){s_sessions[i].sid, s_sessions[i].state, s_sessions[i].back_why};
    memset(s_used, 0, sizeof s_used);
    for (size_t i = 0; i < n && i < MODEL_MAX_SESSIONS; i++) {
        s_sessions[i] = list[i];
        for (int b = 0; b < nb; b++)
            if (back[b].sid == list[i].sid && back[b].st == list[i].state) {
                s_sessions[i].back = true;
                s_sessions[i].back_why = back[b].why;
            }
        steps_seed(&s_sessions[i].steps, list[i].n, (step_state_t)list[i].step,
                   list[i].summary[0] ? list[i].summary : list[i].tool);
        s_used[i] = true;
    }
    for (int a = 0; a < MODEL_MAX_ATTENTION; a++)
        if (s_att_used[a] && find_slot(s_att[a].sid) < 0) s_att_used[a] = false;
    s_rev++;
    dbg_event("model", "snapshot", "\"sessions\":%u",
              (unsigned)(n > MODEL_MAX_SESSIONS ? MODEL_MAX_SESSIONS : n));
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

const session_t *model_at(uint8_t slot)
{ return (slot < MODEL_MAX_SESSIONS && s_used[slot]) ? &s_sessions[slot] : NULL; }

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

/* FNV-1a 32 over what the user reads of a request: a restarted daemon may reuse a rid for another
 * request (issue 20261109-1173). */
static uint32_t att_hash(const attention_t *a)
{
    uint32_t h = 2166136261u;
    for (const char *p = a->title; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    h = (h ^ 0u) * 16777619u;
    for (const char *p = a->text; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return h ^ (uint32_t)a->kind;
}

/* A request dropped by the last welcome comes back: take what the watch knew of it. */
static const att_known_t *known_take(const attention_t *a)
{
    uint32_t h = att_hash(a);
    for (int i = 0; i < s_n_known; i++) {
        if (s_known[i].rid == a->rid && s_known[i].sid == a->sid && s_known[i].hash == h) {
            static att_known_t k;
            k = s_known[i];
            s_known[i] = s_known[--s_n_known]; /* once */
            return &k;
        }
    }
    return NULL;
}

bool model_attention_add(const attention_t *a)
{
    int slot = -1;
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i] && s_att[i].rid == a->rid) slot = i; /* same request: replace */
    }
    for (int i = 0; i < MODEL_MAX_ATTENTION && slot < 0; i++)
        if (!s_att_used[i]) slot = i;
    if (slot < 0) slot = 0; /* queue full: oldest is least likely to still be live */
    bool had = s_att_used[slot] && s_att[slot].rid == a->rid;
    /* only these, not a copy: attention_t is ~0.5 KB of stack */
    bool sent = s_att[slot].answer_sent, skipped = s_att[slot].skipped;
    uint8_t answer = s_att[slot].answer, answer_opt = s_att[slot].answer_opt;
    uint32_t arrived_ms = s_att[slot].arrived_ms;
    const att_known_t *k = had ? NULL : known_take(a);
    if (k) { /* re-sent after a welcome */
        sent = k->sent;
        answer = k->answer;
        answer_opt = k->answer_opt;
        skipped = k->skipped;
    }
    s_att[slot] = *a;
    s_att[slot].n_qs = 0; /* the questions follow with model_attention_set_questions() */
    if (had || k) {       /* a re-sent request keeps the answer already given */
        s_att[slot].answer_sent = sent;
        s_att[slot].answer = answer;
        s_att[slot].answer_opt = answer_opt;
        s_att[slot].skipped = skipped;
    }
    if (had) s_att[slot].arrived_ms = arrived_ms;
    s_att_used[slot] = true;
    int ss = find_slot(a->sid);
    if (ss >= 0 && !had && !k) {
        s_sessions[ss].back = false; /* a new request is on the watch again */
    }
    s_rev++;
    dbg_event("model", "attention", "\"rid\":%u,\"sid\":%u,\"kind\":%d,\"new\":%s",
              (unsigned)a->rid, (unsigned)a->sid, (int)a->kind, had || k ? "false" : "true");
    return !had && !k;
}

void model_attention_set_questions(uint32_t rid, const ask_q_t *qs, int n)
{
    if (n > MODEL_MAX_QS) n = MODEL_MAX_QS;
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (!s_att_used[i] || s_att[i].rid != rid) continue;
        for (int k = 0; k < n; k++) s_att_qs[i][k] = qs[k];
        s_att[i].n_qs = (uint8_t)(n < 0 ? 0 : n);
        s_rev++;
        dbg_event("model", "questions", "\"rid\":%u,\"n\":%d", (unsigned)rid, n);
        return;
    }
}

const ask_q_t *model_attention_question(uint32_t rid, int i)
{
    for (int k = 0; k < MODEL_MAX_ATTENTION; k++)
        if (s_att_used[k] && s_att[k].rid == rid)
            return i >= 0 && i < s_att[k].n_qs ? &s_att_qs[k][i] : NULL;
    return NULL;
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
    s_n_known = 0;
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i]) {
            const attention_t *a = &s_att[i];
            s_known[s_n_known++] = (att_known_t){.rid = a->rid,
                                                 .sid = a->sid,
                                                 .hash = att_hash(a),
                                                 .sent = a->answer_sent,
                                                 .skipped = a->skipped,
                                                 .answer = a->answer,
                                                 .answer_opt = a->answer_opt};
        }
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

const attention_t *model_attention_at(int i)
{ return i >= 0 && i < MODEL_MAX_ATTENTION && s_att_used[i] ? &s_att[i] : NULL; }

void model_attention_skip(uint32_t rid)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (s_att_used[i] && s_att[i].rid == rid && !s_att[i].skipped) {
            s_att[i].skipped = true;
            dbg_event("model", "attention_skip", "\"rid\":%u", (unsigned)rid);
        }
    }
}

const attention_t *model_attention_next(uint32_t now_ms, uint32_t except_rid)
{
    const attention_t *best = NULL;
    uint32_t best_left = 0;
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        const attention_t *a = &s_att[i];
        if (!s_att_used[i] || a->rid == except_rid || a->answer_sent || a->skipped) continue;
        if (a->kind != ATT_APPROVAL && a->kind != ATT_QUESTION) continue;
        uint32_t left = model_attention_left_ms(a, now_ms);
        if (left == 0) continue; /* its window is over: it goes back to the computer */
        if (!best || left < best_left || (left == best_left && a->rid < best->rid)) {
            best = a;
            best_left = left;
        }
    }
    return best;
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

/* The session of a request that went back to the computer says so (UI: "On your computer"). */
static void mark_back(uint32_t sid, uint8_t why)
{
    if (!model_find(sid)) return;
    session_t d = {.sid = sid, .back = true, .back_why = why};
    model_upsert(&d, SF_BACK);
}

bool model_attention_went_back(uint32_t rid, uint8_t why)
{
    const attention_t *a = model_attention_find(rid);
    if (!a) return false;
    uint32_t sid = a->sid;
    bool answered = a->answer_sent;
    model_attention_clear(rid);
    /* an answer already sent says "Waiting for your computer" instead */
    if (!answered) mark_back(sid, why);
    dbg_event("model", "attention_back", "\"rid\":%u,\"sid\":%u,\"why\":%d", (unsigned)rid,
              (unsigned)sid, (int)why);
    return true;
}

uint32_t model_attention_left_ms(const attention_t *a, uint32_t now_ms)
{
    if (!a || !a->deadline_ms) return UINT32_MAX;
    int32_t left = (int32_t)(a->deadline_ms - now_ms);
    return left > 0 ? (uint32_t)left : 0;
}

void model_attention_expire_ms(uint32_t now_ms)
{
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        if (!s_att_used[i] || !s_att[i].deadline_ms) continue;
        if ((int32_t)(now_ms - s_att[i].deadline_ms) > MODEL_WINDOW_GRACE_MS) {
            dbg_event("model", "attention_window_over", "\"rid\":%u", (unsigned)s_att[i].rid);
            model_attention_went_back(s_att[i].rid, BACK_EXPIRED);
        }
    }
}

static uint8_t s_peer_minor;
void model_set_peer_minor(uint8_t minor) { s_peer_minor = minor; }
uint8_t model_peer_minor(void) { return s_peer_minor; }

void model_attention_expire(uint32_t now_unix)
{
    if (!now_unix) return;
    for (int i = 0; i < MODEL_MAX_ATTENTION; i++) {
        /* a request with an answer window ends by that (model_attention_expire_ms), to the
         * millisecond */
        if (s_att_used[i] && s_att[i].exp && !s_att[i].deadline_ms && now_unix > s_att[i].exp) {
            dbg_event("model", "attention_expired", "\"rid\":%u", (unsigned)s_att[i].rid);
            if (s_att[i].kind == ATT_APPROVAL || s_att[i].kind == ATT_QUESTION) {
                model_attention_went_back(s_att[i].rid, BACK_EXPIRED); /* the terminal asks now */
            } else {
                s_att_used[i] = false;
                s_rev++;
            }
        }
    }
}

settings_t *model_settings(void) { return &s_settings; }

void model_touch(void) { s_rev++; }

void model_settings_changed(void)
{
    s_rev++;
    dbg_event("model", "settings", "\"privacy\":%d,\"alert\":%d,\"quiet_until\":%u",
              (int)s_settings.privacy, (int)s_settings.alert, (unsigned)s_settings.quiet_until);
}

/* now_unix 0 = clock unknown (hal_rtc_get_unix): a timed quiet can never be judged, so it is
 * off. */
bool model_quiet_active(uint32_t now_unix)
{ return now_unix != 0 && s_settings.quiet_until != 0 && now_unix < s_settings.quiet_until; }
