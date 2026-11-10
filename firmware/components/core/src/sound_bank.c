/* The sound bank (format and synth: sound_bank.h). Melodies are original; MIDI note names in
 * the comments. Add a candidate by adding its steps and one snd_bank line; add a moment by
 * adding a snd_roles line. Keep cues short, echo tail included: done under 1.5 s, input under 150 ms. */
#include "sound_bank.h"

#include <string.h>

#define N_OF(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
/* S(wave, note, slide, vol, ms, decay) */
#define S(w, n, sl, v, ms, d) {SND_##w, n, sl, v, ms, d}
/* T(steps): one track of a sound */
#define T(a) {a, N_OF(a)}

const snd_role_t snd_roles[] = {
    {"done", "A session you armed with Notify when done (or are watching) finishes", "done-ff", 1, 0},
    {"input", "An agent asks a question or needs your approval", "input-ff", 3, 90},
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

/* ── input ── */
static const snd_step_t input_blip[] = {S(SQ50, 81, 0, 10, 40, 1)};                            /* A5 */
static const snd_step_t input_tick[] = {S(SQ25, 88, 0, 8, 22, 1)};                             /* E6 */
static const snd_step_t input_confirm[] = {S(SQ25, 84, 0, 10, 45, 0), S(SQ25, 91, 0, 10, 90, 1)}; /* C6 G6 */
static const snd_step_t input_pop[] = {S(SQ50, 72, 96, 10, 70, 1)};                            /* C5 -> C7 */
static const snd_step_t input_chk[] = {S(NOISE, 96, 0, 9, 30, 1)};                             /* noise */
static const snd_step_t input_jump[] = {S(SQ12, 76, 88, 10, 110, 1)};                          /* E5 -> E6 */
static const snd_step_t input_soft[] = {S(SINE, 88, 0, 12, 25, 1)};                             /* E6, modern menu */
static const snd_step_t input_pew[] = {S(SQ50, 96, 64, 9, 80, 1)};                              /* sci-fi laser */
static const snd_step_t input_bubble[] = {S(SINE, 64, 88, 13, 60, 1)};                          /* water pop */
static const snd_step_t input_bell[] = {S(BELL, 93, 0, 10, 60, 1)};                             /* A6, with echo */
static const snd_step_t input_lock_a[] = {S(NOISE, 96, 0, 8, 15, 1)};                           /* click... */
static const snd_step_t input_lock_b[] = {S(TRI, 0, 0, 0, 12, 0), S(TRI, 91, 0, 13, 55, 1)};    /* ...then G6 */
static const snd_step_t input_swish[] = {S(NOISE, 96, 88, 5, 40, 0), S(NOISE, 88, 76, 8, 60, 1)}; /* sword */

/* ── homages ──
 * Original cues in the style of classic games: their sound chips and gestures, not their
 * audio or melodies. Nothing here is transcribed from a game; the one borrowed tune is
 * Korobeiniki, a public-domain 19th-century Russian folk song. */
static const snd_step_t hom_poke_lead[] = {S(SQ25, 79, 0, 9, 70, 0), S(SQ25, 84, 0, 9, 70, 0), /* G5 C6 B5 C6 E6 */
                                           S(SQ25, 83, 0, 9, 70, 0), S(SQ25, 84, 0, 9, 70, 0),
                                           S(SQ25, 88, 0, 9, 300, 1)};
static const snd_step_t hom_poke_harm[] = {S(SQ50, 76, 0, 5, 70, 0), S(SQ50, 79, 0, 5, 70, 0),
                                           S(SQ50, 79, 0, 5, 70, 0), S(SQ50, 79, 0, 5, 70, 0),
                                           S(SQ50, 84, 0, 5, 300, 1)};
static const snd_step_t hom_poke_bass[] = {S(TRI, 48, 0, 10, 140, 0), S(TRI, 55, 0, 10, 140, 0), S(TRI, 48, 0, 10, 300, 1)};
static const snd_step_t hom_zelda[] = {S(SQ50, 74, 0, 9, 70, 0), S(SQ50, 78, 0, 9, 70, 0), /* D5 F#5 A5 C#6 D6 . A6 */
                                       S(SQ50, 81, 0, 9, 70, 0), S(SQ50, 85, 0, 9, 70, 0),
                                       S(SQ50, 86, 0, 9, 70, 0), S(SQ50, 93, 0, 9, 300, 1)};
static const snd_step_t hom_contra_boom[] = {S(NOISE, 36, 24, 13, 500, 1)};
static const snd_step_t hom_contra_bugle[] = {S(SQ50, 0, 0, 0, 150, 0), S(SQ50, 67, 0, 8, 60, 0), /* G4 C5 E5 G5 */
                                              S(SQ50, 72, 0, 8, 60, 0), S(SQ50, 76, 0, 8, 60, 0),
                                              S(SQ50, 79, 0, 8, 300, 1)};
static const snd_step_t hom_sf_hit[] = {S(NOISE, 60, 40, 14, 80, 1), S(TRI, 0, 0, 0, 40, 0), S(TRI, 48, 0, 9, 400, 1)};
static const snd_step_t hom_sf_hi[] = {S(SAW, 0, 0, 0, 120, 0), S(SAW, 79, 0, 6, 100, 0), S(SAW, 84, 0, 6, 400, 1)};
static const snd_step_t hom_sf_lo[] = {S(SAW, 0, 0, 0, 120, 0), S(SAW, 76, 0, 5, 100, 0), S(SAW, 79, 0, 5, 400, 1)};
static const snd_step_t hom_mega_lead[] = {S(SQ25, 69, 0, 9, 45, 0), S(SQ25, 72, 0, 9, 45, 0), /* rising triplets */
                                           S(SQ25, 76, 0, 9, 45, 0), S(SQ25, 71, 0, 9, 45, 0),
                                           S(SQ25, 74, 0, 9, 45, 0), S(SQ25, 78, 0, 9, 45, 0),
                                           S(SQ25, 72, 0, 9, 45, 0), S(SQ25, 76, 0, 9, 45, 0),
                                           S(SQ25, 79, 0, 9, 45, 0), S(SQ25, 84, 0, 9, 300, 1)};
static const snd_step_t hom_mega_bass[] = {S(TRI, 45, 0, 10, 135, 0), S(TRI, 47, 0, 10, 135, 0),
                                           S(TRI, 48, 0, 10, 135, 0), S(TRI, 48, 0, 10, 300, 1)};
static const snd_step_t hom_metroid_bell[] = {S(BELL, 74, 0, 9, 200, 1), S(BELL, 77, 0, 9, 200, 1), S(BELL, 81, 0, 9, 500, 1)};
static const snd_step_t hom_metroid_drone[] = {S(SINE, 50, 0, 8, 900, 1)};
static const snd_step_t hom_ff_harp[] = {S(FM, 60, 0, 8, 35, 1), S(FM, 64, 0, 8, 35, 1), S(FM, 67, 0, 8, 35, 1), /* Cmaj9 */
                                         S(FM, 71, 0, 8, 35, 1), S(FM, 74, 0, 8, 35, 1), S(FM, 76, 0, 8, 35, 1),
                                         S(FM, 79, 0, 8, 35, 1), S(FM, 83, 0, 8, 35, 1), S(FM, 84, 0, 8, 500, 1)};
static const snd_step_t hom_ff_brass[] = {S(SAW, 0, 0, 0, 280, 0), S(SAW, 79, 0, 5, 500, 1)};
static const snd_step_t hom_ff_bass[] = {S(TRI, 0, 0, 0, 280, 0), S(TRI, 48, 0, 9, 500, 1)};
static const snd_step_t hom_sonic[] = {S(FM, 84, 0, 10, 40, 0), S(FM, 91, 0, 10, 40, 0), /* C6 G6 C7 E7 */
                                       S(FM, 96, 0, 10, 40, 0), S(FM, 100, 0, 10, 300, 1)};
static const snd_step_t hom_pac[] = {S(TRI, 72, 84, 13, 60, 0), S(TRI, 84, 72, 13, 60, 0), /* wavetable sweeps */
                                     S(TRI, 76, 88, 13, 60, 0), S(TRI, 88, 76, 13, 60, 0),
                                     S(TRI, 79, 91, 13, 60, 0), S(TRI, 91, 96, 13, 300, 1)};
static const snd_step_t hom_tetris_lead[] = {S(SQ25, 76, 0, 9, 150, 0), S(SQ25, 71, 0, 9, 75, 0), /* Korobeiniki: E B C D C B A */
                                             S(SQ25, 72, 0, 9, 75, 0),  S(SQ25, 74, 0, 9, 150, 0),
                                             S(SQ25, 72, 0, 9, 75, 0),  S(SQ25, 71, 0, 9, 75, 0),
                                             S(SQ25, 69, 0, 9, 300, 1)};
static const snd_step_t hom_tetris_bass[] = {S(TRI, 52, 0, 10, 450, 0), S(TRI, 57, 0, 10, 450, 1)};

static const snd_step_t hom_poke_sel[] = {S(SQ25, 96, 0, 8, 15, 0), S(SQ25, 91, 0, 8, 25, 1)};
static const snd_step_t hom_zelda_swish[] = {S(NOISE, 100, 84, 9, 80, 1)};
static const snd_step_t hom_zelda_ring[] = {S(SQ12, 84, 72, 6, 60, 1)};
static const snd_step_t hom_contra_shot_n[] = {S(NOISE, 72, 48, 13, 70, 1)};
static const snd_step_t hom_contra_shot_t[] = {S(SQ50, 72, 55, 6, 40, 1)};
static const snd_step_t hom_sf_punch_n[] = {S(NOISE, 55, 40, 15, 90, 1)};
static const snd_step_t hom_sf_punch_t[] = {S(SQ50, 60, 43, 8, 60, 1)};
static const snd_step_t hom_mega_sel[] = {S(SQ25, 96, 84, 10, 30, 0), S(SQ25, 91, 79, 10, 40, 1)};
static const snd_step_t hom_metroid_tri[] = {S(TRI, 96, 72, 13, 100, 1)};
static const snd_step_t hom_metroid_sq[] = {S(SQ12, 84, 60, 5, 100, 1)};
static const snd_step_t hom_ff_click[] = {S(NOISE, 108, 0, 6, 10, 1)};
static const snd_step_t hom_ff_tone[] = {S(SINE, 98, 0, 10, 35, 1)};
static const snd_step_t hom_sonic_sel[] = {S(FM, 60, 84, 11, 130, 1)};
static const snd_step_t hom_pac_sel[] = {S(TRI, 67, 79, 13, 50, 0), S(TRI, 79, 67, 13, 50, 1)};
static const snd_step_t hom_tetris_sel[] = {S(SQ25, 79, 0, 9, 20, 1), S(SQ25, 86, 0, 9, 25, 1)};

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
    {"input-blip", "input", "Blip", {T(input_blip)}, 0, 0},
    {"input-tick", "input", "Tick", {T(input_tick)}, 0, 0},
    {"input-confirm", "input", "Confirm", {T(input_confirm)}, 0, 0},
    {"input-pop", "input", "Pop", {T(input_pop)}, 0, 0},
    {"input-chk", "input", "Chk", {T(input_chk)}, 0, 0},
    {"input-jump", "input", "Jump", {T(input_jump)}, 0, 0},
    {"input-soft", "input", "Soft tap", {T(input_soft)}, 0, 0},
    {"input-pew", "input", "Pew", {T(input_pew)}, 0, 0},
    {"input-bubble", "input", "Bubble", {T(input_bubble)}, 0, 0},
    {"input-bell", "input", "Bell", {T(input_bell)}, 25, 30},
    {"input-lock", "input", "Lock-on", {T(input_lock_a), T(input_lock_b)}, 0, 0},
    {"input-swish", "input", "Swish", {T(input_swish)}, 0, 0},
    {"done-pokemon", "done", "Healed (Pokémon style)", {T(hom_poke_lead), T(hom_poke_harm), T(hom_poke_bass)}, 0, 0},
    {"done-zelda", "done", "Discovery (Zelda style)", {T(hom_zelda)}, 120, 35},
    {"done-contra", "done", "Stage cleared (Contra style)", {T(hom_contra_boom), T(hom_contra_bugle)}, 0, 0},
    {"done-sf2", "done", "Round won (Street Fighter style)", {T(hom_sf_hit), T(hom_sf_hi), T(hom_sf_lo)}, 90, 20},
    {"done-megaman", "done", "Weapon get (Mega Man style)", {T(hom_mega_lead), T(hom_mega_bass)}, 0, 0},
    {"done-metroid", "done", "Item found (Metroid style)", {T(hom_metroid_bell), T(hom_metroid_drone)}, 180, 40},
    {"done-ff", "done", "Victory (Final Fantasy style)", {T(hom_ff_harp), T(hom_ff_brass), T(hom_ff_bass)}, 140, 30},
    {"done-sonic", "done", "Rings (Sonic style)", {T(hom_sonic)}, 70, 30},
    {"done-pacman", "done", "Maze clear (Pac-Man style)", {T(hom_pac)}, 0, 0},
    {"done-tetris", "done", "Korobeiniki (Tetris style)", {T(hom_tetris_lead), T(hom_tetris_bass)}, 0, 0},
    {"input-pokemon", "input", "Menu (Pokémon style)", {T(hom_poke_sel)}, 0, 0},
    {"input-zelda", "input", "Sword (Zelda style)", {T(hom_zelda_swish), T(hom_zelda_ring)}, 0, 0},
    {"input-contra", "input", "Shot (Contra style)", {T(hom_contra_shot_n), T(hom_contra_shot_t)}, 0, 0},
    {"input-sf2", "input", "Hit (Street Fighter style)", {T(hom_sf_punch_n), T(hom_sf_punch_t)}, 0, 0},
    {"input-megaman", "input", "Buster (Mega Man style)", {T(hom_mega_sel)}, 0, 0},
    {"input-metroid", "input", "Beam (Metroid style)", {T(hom_metroid_tri), T(hom_metroid_sq)}, 0, 0},
    {"input-ff", "input", "Cursor (Final Fantasy style)", {T(hom_ff_click), T(hom_ff_tone)}, 0, 0},
    {"input-sonic", "input", "Spin (Sonic style)", {T(hom_sonic_sel)}, 0, 0},
    {"input-pacman", "input", "Wakka (Pac-Man style)", {T(hom_pac_sel)}, 0, 0},
    {"input-tetris", "input", "Rotate (Tetris style)", {T(hom_tetris_sel)}, 0, 0},
};
const unsigned snd_bank_count = sizeof snd_bank / sizeof snd_bank[0];

const snd_role_t *snd_role(const char *id) {
    for (unsigned r = 0; r < snd_role_count; r++)
        if (strcmp(snd_roles[r].id, id) == 0) return &snd_roles[r];
    return NULL;
}

const snd_sound_t *snd_for(const char *role) {
    const snd_role_t *r = snd_role(role);
    for (unsigned i = 0; r && i < snd_bank_count; i++)
        if (strcmp(snd_bank[i].id, r->pick) == 0) return &snd_bank[i];
    return NULL;
}
