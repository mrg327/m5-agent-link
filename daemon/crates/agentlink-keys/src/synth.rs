//! Synthetic strokes and taps for the evaluation harness (deterministic given the seed).

use crate::layout::{Layout, Point};

/// SplitMix64 with a Box-Muller Gaussian.
pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Self {
        Rng(seed.wrapping_mul(0x9E37_79B9_7F4A_7C15) ^ 0xD1B5_4A32_D192_ED03)
    }

    pub fn next_u64(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// Uniform in [0, 1).
    pub fn uniform(&mut self) -> f32 {
        (self.next_u64() >> 40) as f32 / (1u64 << 24) as f32
    }

    pub fn range(&mut self, lo: f32, hi: f32) -> f32 {
        lo + (hi - lo) * self.uniform()
    }

    pub fn gauss(&mut self) -> f32 {
        let u1 = self.uniform().max(1e-7);
        let u2 = self.uniform();
        (-2.0 * u1.ln()).sqrt() * (std::f32::consts::TAU * u2).cos()
    }
}

/// Noise model of the synthetic finger.
#[derive(Clone, Debug)]
pub struct SynthParams {
    /// Per-key Gaussian offset of the path, in key widths.
    pub key_sigma: f32,
    /// Per-tap Gaussian offset, in key widths.
    pub tap_sigma: f32,
    /// Speed range of a swipe, px/s.
    pub speed: (f32, f32),
    /// Uniform per-axis jitter of every sample, px.
    pub jitter: f32,
    /// Fraction of each segment cut at every corner, applied twice.
    pub corner_cut: f32,
    /// Touch sampling interval, ms.
    pub dt_ms: f32,
}

impl Default for SynthParams {
    fn default() -> Self {
        SynthParams {
            key_sigma: 0.25,
            tap_sigma: 0.3,
            speed: (250.0, 400.0),
            jitter: 3.0,
            corner_cut: 0.2,
            dt_ms: 33.0,
        }
    }
}

fn centres_of(layout: &Layout, word: &str) -> Option<Vec<[f32; 2]>> {
    word.chars()
        .map(|c| {
            let c = c.to_ascii_lowercase();
            layout
                .keys()
                .iter()
                .find(|k| k.glyph() == Some(c))
                .map(|k| k.centre())
        })
        .collect()
}

fn cut_corners(p: &[[f32; 2]], f: f32) -> Vec<[f32; 2]> {
    if p.len() < 3 {
        return p.to_vec();
    }
    let mut out = vec![p[0]];
    for i in 0..p.len() - 1 {
        let (a, b) = (p[i], p[i + 1]);
        if i > 0 {
            out.push([a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f]);
        }
        if i + 1 < p.len() - 1 {
            out.push([b[0] + (a[0] - b[0]) * f, b[1] + (a[1] - b[1]) * f]);
        }
    }
    out.push(p[p.len() - 1]);
    out
}

fn to_i16(v: f32) -> i16 {
    v.round().clamp(-32000.0, 32000.0) as i16
}

/// A swipe over `word` (repeated letters collapse). `None` if a letter is not on the layout.
pub fn synth_stroke(
    layout: &Layout,
    word: &str,
    rng: &mut Rng,
    sp: &SynthParams,
) -> Option<Vec<Point>> {
    let kw = layout.key_width();
    let mut way: Vec<[f32; 2]> = Vec::new();
    for c in centres_of(layout, word)? {
        if way.last() != Some(&c) {
            way.push(c);
        }
    }
    for w in &mut way {
        w[0] += rng.gauss() * sp.key_sigma * kw;
        w[1] += rng.gauss() * sp.key_sigma * kw;
    }
    let path = cut_corners(&cut_corners(&way, sp.corner_cut), sp.corner_cut);
    let speed = rng.range(sp.speed.0, sp.speed.1);
    let mut cum = vec![0.0f32];
    for w in path.windows(2) {
        let d = ((w[1][0] - w[0][0]).powi(2) + (w[1][1] - w[0][1]).powi(2)).sqrt();
        cum.push(cum.last().unwrap() + d);
    }
    let total = *cum.last().unwrap();
    let at = |s: f32| -> [f32; 2] {
        let s = s.clamp(0.0, total);
        let j = cum
            .partition_point(|&c| c <= s)
            .clamp(1, path.len().max(2) - 1);
        if path.len() < 2 {
            return path[0];
        }
        let seg = (cum[j] - cum[j - 1]).max(1e-6);
        let f = (s - cum[j - 1]) / seg;
        [
            path[j - 1][0] + (path[j][0] - path[j - 1][0]) * f,
            path[j - 1][1] + (path[j][1] - path[j - 1][1]) * f,
        ]
    };
    let duration_ms = total / speed * 1000.0;
    let mut pts = Vec::new();
    let mut t = 0.0f32;
    loop {
        let last = t >= duration_ms;
        let p = at(speed * t.min(duration_ms) / 1000.0);
        pts.push(Point {
            x: to_i16(p[0] + rng.range(-sp.jitter, sp.jitter)),
            y: to_i16(p[1] + rng.range(-sp.jitter, sp.jitter)),
            t_ms: t.min(duration_ms) as u16,
        });
        if last {
            break;
        }
        t += sp.dt_ms;
    }
    Some(pts)
}

/// One tap per letter of `word`, with Gaussian noise.
pub fn synth_taps(
    layout: &Layout,
    word: &str,
    rng: &mut Rng,
    sp: &SynthParams,
) -> Option<Vec<Point>> {
    let kw = layout.key_width();
    let mut t = 0u16;
    let taps = centres_of(layout, word)?
        .into_iter()
        .map(|c| {
            t = t.saturating_add(300);
            Point {
                x: to_i16(c[0] + rng.gauss() * sp.tap_sigma * kw),
                y: to_i16(c[1] + rng.gauss() * sp.tap_sigma * kw),
                t_ms: t,
            }
        })
        .collect();
    Some(taps)
}
