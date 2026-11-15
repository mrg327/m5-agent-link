/* Voice dictation, device side (issue 0ea9). See voice.h. */
#include "voice.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "hal.h"
#include "link.h"
#include "model.h"

static const int16_t kStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t kIndexAdj[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static uint8_t nibble(int sample, int *pred, int *index)
{
    int step = kStep[*index], diff = sample - *pred;
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    int delta = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 2; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 1; delta += step; }
    *pred += (code & 8) ? -delta : delta;
    if (*pred > 32767) *pred = 32767;
    if (*pred < -32768) *pred = -32768;
    *index += kIndexAdj[code];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;
    return code;
}

int voice_adpcm_block(const int16_t *pcm, int n, uint8_t *out, int *index_io)
{
    if (n < 1) return 0;
    if (n > VOICE_BLOCK_SAMPLES) n = VOICE_BLOCK_SAMPLES;
    int pred = pcm[0], index = *index_io;
    out[0] = (uint8_t)(pred & 0xff);
    out[1] = (uint8_t)((pred >> 8) & 0xff);
    out[2] = (uint8_t)index;
    out[3] = 0;
    int len = 4;
    for (int i = 1; i < n; i += 2) {
        uint8_t lo = nibble(pcm[i], &pred, &index);
        uint8_t hi = i + 1 < n ? nibble(pcm[i + 1], &pred, &index) : 0;
        out[len++] = (uint8_t)(lo | hi << 4);
    }
    *index_io = index;
    return len;
}

/* The recording waits here, encoded, for the link. The microphone makes 8 kB/s and a BLE central on a long
 * interval takes less (48.75 ms to a Linux machine: under 2 kB/s). Sending straight from the microphone loop
 * then never returned: the watch froze and recorded past VOICE_MAX_MS. So the microphone only fills this
 * queue, a tick sends from it for VOICE_SEND_BUDGET_US, and a stop drains it before the end is sent.
 * In PSRAM: a minute of blocks, and the second the microphone's own buffer holds. */
#define VOICE_Q_BLOCKS ((VOICE_MAX_MS + 1500) * (HAL_MIC_RATE / 1000) / VOICE_BLOCK_SAMPLES + 2)
#define VOICE_SEND_BUDGET_US 20000
MODEL_EXT_BSS static uint8_t s_q[VOICE_Q_BLOCKS][VOICE_BLOCK_BYTES];
MODEL_EXT_BSS static uint8_t s_q_len[VOICE_Q_BLOCKS];
static uint32_t s_q_n;   /* blocks encoded; s_seq of them are sent */
static bool s_draining;  /* stopped, with blocks still to send: the end follows the last one */
static bool s_bulk;      /* the link was asked for its short interval */
static hal_link_kind_t s_port;

static voice_state_t s_state;
static uint32_t s_aid, s_sid, s_seq, s_t0, s_t_end, s_wait_t0;
static int16_t s_pcm[VOICE_BLOCK_SAMPLES];
static int s_have, s_index;
static int s_level;
static int64_t s_energy;
static int s_energy_n;
MODEL_EXT_BSS static char s_text[VOICE_TEXT_MAX];
static const char *s_err = "";

voice_state_t voice_state(void) { return s_state; }
uint32_t voice_aid(void) { return s_aid; }
uint32_t voice_sid(void) { return s_sid; }
int voice_level(void) { return s_level; }
const char *voice_text(void) { return s_text; }
const char *voice_error(void) { return s_err; }
uint32_t voice_elapsed_ms(uint32_t now_ms) { return (s_state == VOICE_REC ? now_ms : s_t_end) - s_t0; }

/* A recording is the one time the watch sends in bulk: ask the link for its shortest interval meanwhile. */
static void bulk(bool on)
{
    if (on == s_bulk) return;
    if (on) s_port = link_active_kind();
    s_bulk = on;
    hal_link_bulk(s_port, on);
}

static void fail(const char *why)
{
    hal_mic_stop();
    bulk(false);
    s_draining = false;
    s_state = VOICE_FAILED;
    s_err = why;
    dbg_event("voice", "failed", "\"aid\":%u,\"why\":\"%s\"", (unsigned)s_aid, why);
}

static void flush_block(void)
{
    if (s_have < 1) return;
    if (s_q_n < VOICE_Q_BLOCKS) { /* full only past VOICE_MAX_MS, if a tick came very late: the rest is dropped */
        s_q_len[s_q_n] = (uint8_t)voice_adpcm_block(s_pcm, s_have, s_q[s_q_n], &s_index);
        s_q_n++;
    }
    s_have = 0;
}

/* Sends queued blocks for one tick's budget. A send waits for the link (back-pressure), so one block can
 * take longer than the budget on a slow link; the loop still returns after it. */
static void send_queued(void)
{
    uint64_t t0 = hal_time_us();
    while (s_seq < s_q_n) {
        if (!link_send_audio(s_aid, s_seq, s_q[s_seq], s_q_len[s_seq])) {
            fail("Offline");
            return;
        }
        s_seq++;
        if (hal_time_us() - t0 >= VOICE_SEND_BUDGET_US) break;
    }
}

/* After a stop: the rest of the queue, then the end. The daemon's time to transcribe counts from the end. */
static void drain(uint32_t now_ms)
{
    send_queued();
    if (s_state != VOICE_WAIT || s_seq < s_q_n) return;
    s_draining = false;
    bulk(false);
    if (!link_send_audio_end(s_aid)) {
        fail("Offline");
        return;
    }
    s_wait_t0 = now_ms;
}

bool voice_start(uint32_t sid, uint32_t now_ms)
{
    if (s_state == VOICE_REC) return true;
    s_err = "";
    s_aid = s_aid + 1 + (now_ms & 0xff); /* differs across boots: the daemon keys recordings by aid */
    s_sid = sid;
    s_seq = 0;
    s_q_n = 0;
    s_draining = false;
    s_have = 0;
    s_index = 0;
    s_level = 0;
    s_energy = 0;
    s_energy_n = 0;
    s_text[0] = 0;
    if (!hal_board_info()->has_mic || !hal_mic_start()) {
        s_err = "No microphone";
        s_state = VOICE_FAILED;
        char msg[96];
        snprintf(msg, sizeof msg, "mic start failed: %s", hal_board_info()->has_mic ? hal_mic_error() : "no mic");
        link_send_log(0, msg);
        return false;
    }
    if (!link_send_audio_start(s_aid, sid)) {
        hal_mic_stop();
        s_err = "Offline";
        s_state = VOICE_FAILED;
        return false;
    }
    s_t0 = now_ms;
    s_state = VOICE_REC;
    bulk(true);
    dbg_event("voice", "start", "\"aid\":%u,\"sid\":%u", (unsigned)s_aid, (unsigned)sid);
    return true;
}

static void pull(void)
{
    int16_t buf[160];
    size_t n;
    while (s_state == VOICE_REC && (n = hal_mic_read(buf, sizeof buf / sizeof *buf)) > 0) {
        for (size_t i = 0; i < n; i++) {
            s_energy += (int32_t)buf[i] * buf[i];
            if (++s_energy_n == HAL_MIC_RATE / 10) { /* 100 ms window */
                int64_t mean = s_energy / s_energy_n;
                int rms = 0;
                while ((int64_t)(rms + 1) * (rms + 1) <= mean) rms += (rms < 64 ? 1 : rms / 16); /* rough sqrt */
                /* ~ -50 dBFS .. -10 dBFS onto 0..100 */
                int lvl = rms <= 100 ? 0 : rms >= 10000 ? 100 : (rms - 100) * 100 / 9900;
                s_level = lvl;
                s_energy = 0;
                s_energy_n = 0;
            }
            s_pcm[s_have++] = buf[i];
            if (s_have == VOICE_BLOCK_SAMPLES) flush_block();
        }
    }
}

void voice_stop(uint32_t now_ms)
{
    if (s_state != VOICE_REC) return;
    pull();
    hal_mic_stop();
    flush_block();
    s_t_end = now_ms;
    s_state = VOICE_WAIT;
    s_draining = true;
    s_wait_t0 = now_ms;
    s_level = 0;
    drain(now_ms); /* a link that kept up has nothing queued: the end goes now */
    if (s_state != VOICE_WAIT) return; /* a block or the end failed */
    dbg_event("voice", "stop", "\"aid\":%u,\"ms\":%u,\"blocks\":%u,\"queued\":%u", (unsigned)s_aid,
              (unsigned)(now_ms - s_t0), (unsigned)s_q_n, (unsigned)(s_q_n - s_seq));
}

void voice_cancel(void)
{
    if (s_state == VOICE_REC) hal_mic_stop();
    if (s_state == VOICE_REC || (s_state == VOICE_WAIT && s_draining))
        link_send_audio_end(s_aid); /* the daemon transcribes what it got, and nothing ever sends it */
    bulk(false);
    s_draining = false;
    if (s_state != VOICE_IDLE) dbg_event("voice", "cancel", "\"aid\":%u", (unsigned)s_aid);
    s_state = VOICE_IDLE;
    s_text[0] = 0;
}

void voice_tick(uint32_t now_ms)
{
    if (s_state == VOICE_REC) {
        pull();
        send_queued();
        if (s_state == VOICE_REC && now_ms - s_t0 >= VOICE_MAX_MS) voice_stop(now_ms);
    } else if (s_state == VOICE_WAIT && s_draining) {
        drain(now_ms);
    } else if (s_state == VOICE_WAIT && now_ms - s_wait_t0 >= VOICE_WAIT_MS) {
        fail("No transcript");
    }
}

void voice_on_transcript(uint32_t aid, const char *text)
{
    if (s_state != VOICE_WAIT || aid != s_aid) return; /* an old or abandoned recording */
    if (!text || !text[0]) {
        fail("Nothing heard");
        return;
    }
    snprintf(s_text, sizeof s_text, "%s", text);
    s_state = VOICE_TEXT;
    dbg_event("voice", "transcript", "\"aid\":%u,\"chars\":%u", (unsigned)aid, (unsigned)strlen(s_text));
}
