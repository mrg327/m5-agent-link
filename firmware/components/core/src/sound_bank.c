/* The sound bank (format and synth: sound_bank.h). Melodies are original; MIDI note names in
 * the comments. Add a candidate by adding its steps and one snd_bank line; add a moment by
 * adding a snd_roles line. Keep cues short: done under 1 s, select under 150 ms. */
#include "sound_bank.h"

#define N_OF(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
/* S(wave, note, slide, vol, ms, decay) */
#define S(w, n, sl, v, ms, d) {SND_##w, n, sl, v, ms, d}

const snd_role_t snd_roles[] = {
    {"done", "A session you armed with Notify when done (or are watching) finishes"},
    {"select", "You commit an item on the wheel"},
};
const unsigned snd_role_count = sizeof snd_roles / sizeof snd_roles[0];

/* ── done ── */
static const snd_step_t done_coin[] = {/* B5 E6 */
                                       S(SQ25, 83, 0, 12, 75, 0), S(SQ25, 88, 0, 12, 420, 1)};
static const snd_step_t done_clear[] = {/* C5 E5 G5 C6 . G5 C6 */
                                        S(SQ50, 72, 0, 10, 70, 0), S(SQ50, 76, 0, 10, 70, 0),
                                        S(SQ50, 79, 0, 10, 70, 0), S(SQ50, 84, 0, 10, 90, 0),
                                        S(SQ50, 0, 0, 0, 50, 0),   S(SQ50, 79, 0, 10, 70, 0),
                                        S(SQ50, 84, 0, 10, 380, 1)};
static const snd_step_t done_powerup[] = {/* G4 B4 D5 G5 B5 D6 G6, B5 D6 G6 */
                                          S(SQ12, 67, 0, 11, 35, 0), S(SQ12, 71, 0, 11, 35, 0),
                                          S(SQ12, 74, 0, 11, 35, 0), S(SQ12, 79, 0, 11, 35, 0),
                                          S(SQ12, 83, 0, 11, 35, 0), S(SQ12, 86, 0, 11, 35, 0),
                                          S(SQ12, 91, 0, 11, 35, 0), S(SQ12, 83, 0, 11, 35, 0),
                                          S(SQ12, 86, 0, 11, 35, 0), S(SQ12, 91, 0, 11, 300, 1)};
static const snd_step_t done_treasure[] = {/* D5 F#5 A5 D6 */
                                           S(SQ50, 74, 0, 10, 100, 0), S(SQ50, 0, 0, 0, 20, 0),
                                           S(SQ50, 78, 0, 10, 100, 0), S(SQ50, 0, 0, 0, 20, 0),
                                           S(SQ50, 81, 0, 10, 100, 0), S(SQ50, 0, 0, 0, 20, 0),
                                           S(SQ50, 86, 0, 10, 450, 1)};
static const snd_step_t done_bonus[] = {/* C6 A5 C6 F6 . F6 A6, triangle */
                                        S(TRI, 84, 0, 15, 60, 0),  S(TRI, 81, 0, 15, 60, 0),
                                        S(TRI, 84, 0, 15, 60, 0),  S(TRI, 89, 0, 15, 120, 0),
                                        S(TRI, 0, 0, 0, 30, 0),    S(TRI, 89, 0, 15, 60, 0),
                                        S(TRI, 93, 0, 15, 400, 1)};
static const snd_step_t done_warp[] = {/* C5 gliding up to C7, then C7 */
                                       S(SQ50, 72, 96, 10, 260, 0), S(SQ50, 96, 0, 10, 220, 1)};

/* ── select ── */
static const snd_step_t select_blip[] = {S(SQ50, 81, 0, 10, 40, 1)};                            /* A5 */
static const snd_step_t select_tick[] = {S(SQ25, 88, 0, 8, 22, 1)};                             /* E6 */
static const snd_step_t select_confirm[] = {S(SQ25, 84, 0, 10, 45, 0), S(SQ25, 91, 0, 10, 90, 1)}; /* C6 G6 */
static const snd_step_t select_pop[] = {S(SQ50, 72, 96, 10, 70, 1)};                            /* C5 -> C7 */
static const snd_step_t select_chk[] = {S(NOISE, 96, 0, 9, 30, 1)};                             /* noise */
static const snd_step_t select_jump[] = {S(SQ12, 76, 88, 10, 110, 1)};                          /* E5 -> E6 */

const snd_sound_t snd_bank[] = {
    {"done-coin", "done", "Coin", done_coin, N_OF(done_coin)},
    {"done-clear", "done", "Stage clear", done_clear, N_OF(done_clear)},
    {"done-powerup", "done", "Power-up", done_powerup, N_OF(done_powerup)},
    {"done-treasure", "done", "Treasure", done_treasure, N_OF(done_treasure)},
    {"done-bonus", "done", "Bonus", done_bonus, N_OF(done_bonus)},
    {"done-warp", "done", "Warp", done_warp, N_OF(done_warp)},
    {"select-blip", "select", "Blip", select_blip, N_OF(select_blip)},
    {"select-tick", "select", "Tick", select_tick, N_OF(select_tick)},
    {"select-confirm", "select", "Confirm", select_confirm, N_OF(select_confirm)},
    {"select-pop", "select", "Pop", select_pop, N_OF(select_pop)},
    {"select-chk", "select", "Chk", select_chk, N_OF(select_chk)},
    {"select-jump", "select", "Jump", select_jump, N_OF(select_jump)},
};
const unsigned snd_bank_count = sizeof snd_bank / sizeof snd_bank[0];
