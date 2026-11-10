/* The sound bank (format and synth: sound_bank.h). Melodies are original; MIDI note names in
 * the comments. Add a candidate by adding its steps and one snd_bank line; add a moment by
 * adding a snd_roles line. Keep cues short, echo tail included: done under 1.5 s, select under 150 ms. */
#include "sound_bank.h"

#define N_OF(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
/* S(wave, note, slide, vol, ms, decay) */
#define S(w, n, sl, v, ms, d) {SND_##w, n, sl, v, ms, d}
/* T(steps): one track of a sound */
#define T(a) {a, N_OF(a)}

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
/* Fanfare: brassy saw stab, G-G-C over E-E-G and a C bass, in a hall */
static const snd_step_t done_fanfare_a[] = {S(SAW, 67, 0, 6, 90, 0), S(SAW, 0, 0, 0, 20, 0),
                                            S(SAW, 67, 0, 6, 90, 0), S(SAW, 72, 0, 6, 480, 1)};
static const snd_step_t done_fanfare_b[] = {S(SAW, 64, 0, 5, 90, 0), S(SAW, 0, 0, 0, 20, 0),
                                            S(SAW, 64, 0, 5, 90, 0), S(SAW, 67, 0, 5, 480, 1)};
static const snd_step_t done_fanfare_c[] = {S(TRI, 48, 0, 8, 90, 0), S(TRI, 0, 0, 0, 20, 0),
                                            S(TRI, 48, 0, 8, 90, 0), S(TRI, 48, 0, 8, 480, 1)};
/* Achievement: console-style two-note chime, E6 B6 */
static const snd_step_t done_achieve[] = {S(BELL, 88, 0, 11, 120, 1), S(BELL, 95, 0, 11, 500, 1)};
/* Quest complete: electric piano IV -> I (F/A -> E/G over F -> C) */
static const snd_step_t done_quest_a[] = {S(FM, 81, 0, 6, 220, 1), S(FM, 79, 0, 6, 500, 1)};
static const snd_step_t done_quest_b[] = {S(FM, 77, 0, 6, 220, 1), S(FM, 76, 0, 6, 500, 1)};
static const snd_step_t done_quest_c[] = {S(TRI, 53, 0, 8, 220, 1), S(TRI, 48, 0, 8, 500, 1)};
/* Sparkle: magic twinkle cascade, G7 D7 E7 B6 C7 G6 A6 E6 .. C7 */
static const snd_step_t done_sparkle[] = {S(SINE, 103, 0, 11, 45, 1), S(SINE, 98, 0, 11, 45, 1),
                                          S(SINE, 100, 0, 11, 45, 1), S(SINE, 95, 0, 11, 45, 1),
                                          S(SINE, 96, 0, 11, 45, 1),  S(SINE, 91, 0, 11, 45, 1),
                                          S(SINE, 93, 0, 11, 45, 1),  S(SINE, 88, 0, 11, 45, 1),
                                          S(SINE, 96, 0, 11, 220, 1)};
/* K.O.: boss goes down in a rumble, then a rising sting */
static const snd_step_t done_ko_a[] = {S(NOISE, 40, 28, 12, 600, 1)};
static const snd_step_t done_ko_b[] = {S(SQ50, 0, 0, 0, 250, 0), S(SQ50, 72, 84, 8, 120, 0),
                                       S(SQ50, 84, 0, 8, 300, 1)};
/* Level up: RPG scale run C5..B5 to a C6 hit, with a walking bass */
static const snd_step_t done_levelup_a[] = {S(SQ25, 72, 0, 9, 40, 0), S(SQ25, 74, 0, 9, 40, 0),
                                            S(SQ25, 76, 0, 9, 40, 0), S(SQ25, 77, 0, 9, 40, 0),
                                            S(SQ25, 79, 0, 9, 40, 0), S(SQ25, 81, 0, 9, 40, 0),
                                            S(SQ25, 83, 0, 9, 40, 0), S(SQ25, 84, 0, 9, 90, 0),
                                            S(SQ25, 0, 0, 0, 30, 0),  S(SQ25, 84, 0, 9, 350, 1)};
static const snd_step_t done_levelup_b[] = {S(TRI, 60, 0, 12, 280, 0), S(TRI, 55, 0, 12, 120, 0),
                                            S(TRI, 48, 0, 12, 350, 1)};

/* ── select ── */
static const snd_step_t select_blip[] = {S(SQ50, 81, 0, 10, 40, 1)};                            /* A5 */
static const snd_step_t select_tick[] = {S(SQ25, 88, 0, 8, 22, 1)};                             /* E6 */
static const snd_step_t select_confirm[] = {S(SQ25, 84, 0, 10, 45, 0), S(SQ25, 91, 0, 10, 90, 1)}; /* C6 G6 */
static const snd_step_t select_pop[] = {S(SQ50, 72, 96, 10, 70, 1)};                            /* C5 -> C7 */
static const snd_step_t select_chk[] = {S(NOISE, 96, 0, 9, 30, 1)};                             /* noise */
static const snd_step_t select_jump[] = {S(SQ12, 76, 88, 10, 110, 1)};                          /* E5 -> E6 */
static const snd_step_t select_soft[] = {S(SINE, 88, 0, 12, 25, 1)};                             /* E6, modern menu */
static const snd_step_t select_pew[] = {S(SQ50, 96, 64, 9, 80, 1)};                              /* sci-fi laser */
static const snd_step_t select_bubble[] = {S(SINE, 64, 88, 13, 60, 1)};                          /* water pop */
static const snd_step_t select_bell[] = {S(BELL, 93, 0, 10, 60, 1)};                             /* A6, with echo */
static const snd_step_t select_lock_a[] = {S(NOISE, 96, 0, 8, 15, 1)};                           /* click... */
static const snd_step_t select_lock_b[] = {S(TRI, 0, 0, 0, 12, 0), S(TRI, 91, 0, 13, 55, 1)};    /* ...then G6 */
static const snd_step_t select_swish[] = {S(NOISE, 96, 88, 5, 40, 0), S(NOISE, 88, 76, 8, 60, 1)}; /* sword */

const snd_sound_t snd_bank[] = {
    {"done-coin", "done", "Coin", {T(done_coin)}, 0, 0},
    {"done-clear", "done", "Stage clear", {T(done_clear)}, 0, 0},
    {"done-powerup", "done", "Power-up", {T(done_powerup)}, 0, 0},
    {"done-treasure", "done", "Treasure", {T(done_treasure)}, 0, 0},
    {"done-bonus", "done", "Bonus", {T(done_bonus)}, 0, 0},
    {"done-warp", "done", "Warp", {T(done_warp)}, 0, 0},
    {"done-fanfare", "done", "Fanfare", {T(done_fanfare_a), T(done_fanfare_b), T(done_fanfare_c)}, 110, 25},
    {"done-achieve", "done", "Achievement", {T(done_achieve)}, 140, 35},
    {"done-quest", "done", "Quest complete", {T(done_quest_a), T(done_quest_b), T(done_quest_c)}, 160, 30},
    {"done-sparkle", "done", "Sparkle", {T(done_sparkle)}, 90, 45},
    {"done-ko", "done", "K.O.", {T(done_ko_a), T(done_ko_b)}, 0, 0},
    {"done-levelup", "done", "Level up", {T(done_levelup_a), T(done_levelup_b)}, 0, 0},
    {"select-blip", "select", "Blip", {T(select_blip)}, 0, 0},
    {"select-tick", "select", "Tick", {T(select_tick)}, 0, 0},
    {"select-confirm", "select", "Confirm", {T(select_confirm)}, 0, 0},
    {"select-pop", "select", "Pop", {T(select_pop)}, 0, 0},
    {"select-chk", "select", "Chk", {T(select_chk)}, 0, 0},
    {"select-jump", "select", "Jump", {T(select_jump)}, 0, 0},
    {"select-soft", "select", "Soft tap", {T(select_soft)}, 0, 0},
    {"select-pew", "select", "Pew", {T(select_pew)}, 0, 0},
    {"select-bubble", "select", "Bubble", {T(select_bubble)}, 0, 0},
    {"select-bell", "select", "Bell", {T(select_bell)}, 25, 30},
    {"select-lock", "select", "Lock-on", {T(select_lock_a), T(select_lock_b)}, 0, 0},
    {"select-swish", "select", "Swish", {T(select_swish)}, 0, 0},
};
const unsigned snd_bank_count = sizeof snd_bank / sizeof snd_bank[0];
