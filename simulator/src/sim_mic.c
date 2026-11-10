/* Simulated microphone (voice dictation, issue 0ea9): plays a 16 kHz mono 16-bit WAV loaded by the
 * script's `mic FILE.wav` at real time, then silence. Without a file it is silence throughout. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"

static int16_t *s_pcm;
static size_t s_n, s_pos;
static uint64_t s_t0_us;
static bool s_on;
static size_t s_given; /* samples handed out since start */

bool sim_mic_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[12];
    bool ok = fread(hdr, 1, 12, f) == 12 && !memcmp(hdr, "RIFF", 4) && !memcmp(hdr + 8, "WAVE", 4);
    free(s_pcm);
    s_pcm = NULL;
    s_n = 0;
    while (ok) { /* walk the chunks to fmt and data */
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        uint32_t len = ch[4] | ch[5] << 8 | ch[6] << 16 | (uint32_t)ch[7] << 24;
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fmt[16];
            if (len < 16 || fread(fmt, 1, 16, f) != 16) { ok = false; break; }
            unsigned chans = fmt[2] | fmt[3] << 8, rate = fmt[4] | fmt[5] << 8 | fmt[6] << 16, bits = fmt[14] | fmt[15] << 8;
            if (fmt[0] != 1 || chans != 1 || rate != HAL_MIC_RATE || bits != 16) { ok = false; break; }
            fseek(f, len - 16, SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            s_pcm = malloc(len);
            s_n = s_pcm ? fread(s_pcm, 2, len / 2, f) : 0;
            break;
        } else {
            fseek(f, len, SEEK_CUR);
        }
    }
    fclose(f);
    sim_log("mic", "\"loaded\":%s,\"samples\":%u", s_n ? "true" : "false", (unsigned)s_n);
    return s_n > 0;
}

bool hal_mic_start(void)
{
    s_on = true;
    s_pos = 0;
    s_given = 0;
    s_t0_us = hal_time_us();
    sim_log("mic", "\"on\":true");
    return true;
}

size_t hal_mic_read(int16_t *pcm, size_t max)
{
    if (!s_on) return 0;
    size_t due = (size_t)((hal_time_us() - s_t0_us) * HAL_MIC_RATE / 1000000u);
    size_t n = due > s_given ? due - s_given : 0;
    if (n > max) n = max;
    for (size_t i = 0; i < n; i++) pcm[i] = s_pos < s_n ? s_pcm[s_pos++] : 0;
    s_given += n;
    return n;
}

void hal_mic_stop(void)
{
    if (!s_on) return;
    s_on = false;
    sim_log("mic", "\"on\":false,\"samples\":%u", (unsigned)s_given);
}
