//! Accuracy and timing evaluation on synthetic input (milestone M1 of the swipe keyboard).

use crate::decode::Decoder;
use crate::synth::{synth_stroke, synth_taps, Rng, SynthParams};
use std::collections::HashMap;
use std::time::Instant;

/// Result of evaluating one word set.
#[derive(Debug, Default)]
pub struct Report {
    pub n: usize,
    pub top1: usize,
    pub top4: usize,
    pub mean_ms: f64,
    pub max_ms: f64,
    /// (wanted, top-1 got) with counts, for the misses.
    pub confusions: HashMap<(String, String), usize>,
    /// (wanted letter, letter of the top-1 word at the same position) for top-1 misses with
    /// the same length: which keys get mixed up.
    pub letter_swaps: HashMap<(char, char), usize>,
}

impl Report {
    pub fn top1_pct(&self) -> f64 {
        100.0 * self.top1 as f64 / self.n.max(1) as f64
    }
    pub fn top4_pct(&self) -> f64 {
        100.0 * self.top4 as f64 / self.n.max(1) as f64
    }
    pub fn worst_swaps(&self, k: usize) -> Vec<((char, char), usize)> {
        let mut v: Vec<_> = self.letter_swaps.iter().map(|(a, &b)| (*a, b)).collect();
        v.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(&b.0)));
        v.truncate(k);
        v
    }
    pub fn worst(&self, k: usize) -> Vec<(&(String, String), usize)> {
        let mut v: Vec<_> = self.confusions.iter().map(|(a, &b)| (a, b)).collect();
        v.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(b.0)));
        v.truncate(k);
        v
    }
}

fn word_seed(seed: u64, w: &str) -> u64 {
    w.bytes().fold(seed ^ 0xCBF2_9CE4_8422_2325, |h, b| {
        (h ^ u64::from(b)).wrapping_mul(0x0100_0000_01B3)
    })
}

fn run(
    dec: &Decoder,
    words: &[&str],
    seed: u64,
    gen: impl Fn(&Decoder, &str, &mut Rng) -> Option<Vec<crate::Point>>,
    decode: impl Fn(&Decoder, &[crate::Point]) -> Vec<crate::Candidate>,
) -> Report {
    let mut r = Report::default();
    let mut total_ms = 0.0;
    for &w in words {
        let mut rng = Rng::new(word_seed(seed, w));
        let Some(input) = gen(dec, w, &mut rng) else {
            continue;
        };
        let t0 = Instant::now();
        let c = decode(dec, &input);
        let ms = t0.elapsed().as_secs_f64() * 1000.0;
        total_ms += ms;
        r.max_ms = r.max_ms.max(ms);
        r.n += 1;
        if c.first().is_some_and(|c| c.word == w) {
            r.top1 += 1;
        } else {
            let got = c.first().map(|c| c.word.clone()).unwrap_or_default();
            if got.len() == w.len() {
                for (a, b) in w.chars().zip(got.chars()).filter(|(a, b)| a != b) {
                    *r.letter_swaps.entry((a, b)).or_insert(0) += 1;
                }
            }
            *r.confusions.entry((w.to_string(), got)).or_insert(0) += 1;
        }
        if c.iter().take(4).any(|c| c.word == w) {
            r.top4 += 1;
        }
    }
    r.mean_ms = total_ms / r.n.max(1) as f64;
    r
}

/// Swipe every word once with the noise model `sp`. Words with a swipe path shorter than the
/// tap slop (one-letter words, doubled letters) are skipped: they are typed with taps.
pub fn eval_strokes(dec: &Decoder, words: &[&str], seed: u64, sp: &SynthParams) -> Report {
    run(
        dec,
        words,
        seed,
        |d, w, rng| {
            let s = synth_stroke(d.layout(), w, rng, sp)?;
            (!Decoder::is_tap(&s)).then_some(s)
        },
        |d, s| d.decode_stroke(s, 4),
    )
}

/// Tap every letter of every word once.
pub fn eval_taps(dec: &Decoder, words: &[&str], seed: u64, sp: &SynthParams) -> Report {
    run(
        dec,
        words,
        seed,
        |d, w, rng| synth_taps(d.layout(), w, rng, sp),
        |d, s| d.decode_taps(s, 4),
    )
}
