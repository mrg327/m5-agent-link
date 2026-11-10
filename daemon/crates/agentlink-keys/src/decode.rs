//! SHARK2 stroke decoding and tap decoding.

use crate::context::{parts_of, Context, Parsed};
use crate::layout::{extent, Layout, Point};
use crate::lexicon::{rank_prior, Lexicon};
use std::collections::HashMap;

/// Points every stroke and template is resampled to.
const N: usize = 64;
/// Strokes whose bounding extent is below this many pixels are taps (firmware tap slop).
pub const TAP_SLOP_PX: i32 = 18;

/// A ranked word. `score` is an unnormalised log posterior: higher is better, only the
/// differences matter.
#[derive(Clone, Debug, PartialEq)]
pub struct Candidate {
    pub word: String,
    pub score: f32,
}

/// Tunable weights. Lengths are in key widths unless noted.
#[derive(Clone, Debug)]
pub struct Params {
    /// Std-dev of the location channel (mean point distance).
    pub sigma_loc: f32,
    /// Location error below this many key widths counts as zero (the finger only has to be
    /// somewhere on the key).
    pub loc_dead: f32,
    /// Std-dev of the start and end points (the finger aims there), key widths.
    pub sigma_end: f32,
    /// Std-dev of the order-free channel: mean distance from each template point to its nearest
    /// stroke point (key widths).
    pub sigma_near: f32,
    /// Std-dev of the shape channel (mean distance in units of the stroke extent).
    pub sigma_shape: f32,
    /// Weight of the log prior of a word.
    pub prior_weight: f32,
    /// Std-dev of a tap around the key centre.
    pub sigma_tap: f32,
    /// Log boost (nats) for words found in the context.
    pub ctx_boost: f32,
    /// Extra boost for a word that followed `prev_word` in the context.
    pub bigram_boost: f32,
    /// Bonus for a tapped literal string that is a known word.
    pub literal_bonus: f32,
}

impl Default for Params {
    fn default() -> Self {
        Params {
            sigma_loc: 0.5,
            loc_dead: 0.0,
            sigma_near: 0.15,
            sigma_end: 0.3,
            sigma_shape: 0.08,
            prior_weight: 0.7,
            sigma_tap: 0.45,
            ctx_boost: 4.0,
            bigram_boost: 2.0,
            literal_bonus: 3.0,
        }
    }
}

type P2 = [f32; 2];

fn dist(a: P2, b: P2) -> f32 {
    ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2)).sqrt()
}

fn path_len(p: &[P2]) -> f32 {
    p.windows(2).map(|w| dist(w[0], w[1])).sum()
}

/// Resample a polyline to `out.len()` equidistant points.
fn resample(p: &[P2], out: &mut [P2]) {
    let n = out.len();
    let total = path_len(p);
    if p.len() < 2 || total < 1e-3 {
        out.fill(p[0]);
        return;
    }
    let step = total / (n - 1) as f32;
    out[0] = p[0];
    let (mut j, mut seg_start) = (0usize, 0.0f32);
    for (i, o) in out.iter_mut().enumerate().skip(1) {
        let target = step * i as f32;
        loop {
            let seg = dist(p[j], p[j + 1]);
            if seg_start + seg >= target || j + 2 >= p.len() {
                let f = if seg > 0.0 {
                    ((target - seg_start) / seg).clamp(0.0, 1.0)
                } else {
                    0.0
                };
                *o = [
                    p[j][0] + (p[j + 1][0] - p[j][0]) * f,
                    p[j][1] + (p[j + 1][1] - p[j][1]) * f,
                ];
                break;
            }
            seg_start += seg;
            j += 1;
        }
    }
    out[n - 1] = p[p.len() - 1];
}

/// Centroid and scale (largest bounding side, at least `floor`) used by the shape channel.
fn centroid_scale(p: &[P2], floor: f32) -> (P2, f32) {
    let n = p.len() as f32;
    let (mut sx, mut sy) = (0.0, 0.0);
    let (mut x0, mut x1, mut y0, mut y1) = (f32::MAX, f32::MIN, f32::MAX, f32::MIN);
    for q in p {
        sx += q[0];
        sy += q[1];
        x0 = x0.min(q[0]);
        x1 = x1.max(q[0]);
        y0 = y0.min(q[1]);
        y1 = y1.max(q[1]);
    }
    ([sx / n, sy / n], (x1 - x0).max(y1 - y0).max(floor))
}

struct Entry {
    text: String,
    prior: f32,
    /// Key indices of every letter, repeats included (for taps).
    seq: Vec<u16>,
    start: P2,
    end: P2,
    len: f32,
    cen: P2,
    scale: f32,
    /// Context identifier offered whole; not matched by taps.
    ident: bool,
}

/// Templates of a set of words: one resampled ideal path per entry.
#[derive(Default)]
struct Bank {
    entries: Vec<Entry>,
    pts: Vec<P2>,
}

struct Keys {
    centres: Vec<P2>,
    char_key: HashMap<char, u16>,
    /// Key indices that take part in words.
    word_keys: Vec<u16>,
    kw: f32,
}

impl Bank {
    fn push(&mut self, k: &Keys, text: &str, prior: f32, ident: bool) {
        let seq: Vec<u16> = text
            .chars()
            .filter_map(|c| k.char_key.get(&c.to_ascii_lowercase()).copied())
            .collect();
        // Words with characters off the layout can't be typed here (identifiers keep only the
        // characters that are on it).
        let ok = if ident {
            seq.len() >= 2
        } else {
            !seq.is_empty() && seq.len() == text.chars().count()
        };
        if !ok {
            return;
        }
        let mut path: Vec<P2> = Vec::with_capacity(seq.len());
        let mut last = u16::MAX;
        for &s in &seq {
            if s != last {
                path.push(k.centres[s as usize]);
                last = s;
            }
        }
        let mut t = [[0.0; 2]; N];
        resample(&path, &mut t);
        let (cen, scale) = centroid_scale(&t, 2.0 * k.kw);
        self.entries.push(Entry {
            text: text.to_string(),
            prior,
            seq,
            start: t[0],
            end: t[N - 1],
            len: path_len(&path),
            cen,
            scale,
            ident,
        });
        self.pts.extend_from_slice(&t);
    }
}

/// An identifier from the context with the lowercase words it is made of.
struct Ident {
    text: String,
    parts: Vec<String>,
    count: u32,
}

/// The decoder: a layout, a lexicon and the current context.
pub struct Decoder {
    layout: Layout,
    keys: Keys,
    params: Params,
    base: Bank,
    base_index: HashMap<String, usize>,
    by_len: Vec<Vec<u32>>,
    base_boost: Vec<f32>,
    extra: Bank,
    extra_boost: Vec<f32>,
    idents: Vec<Ident>,
}

struct StrokeFeat {
    pts: [P2; N],
    nrm: [P2; N],
    start: P2,
    end: P2,
    len: f32,
}

impl Decoder {
    /// Precompute the templates of every lexicon word on `layout`.
    pub fn new(layout: Layout, lexicon: Lexicon) -> Self {
        Self::with_params(layout, lexicon, Params::default())
    }

    pub fn with_params(layout: Layout, lexicon: Lexicon, params: Params) -> Self {
        let mut centres = Vec::new();
        let mut char_key = HashMap::new();
        let mut word_keys = Vec::new();
        for (i, k) in layout.keys().iter().enumerate().take(u16::MAX as usize) {
            centres.push(k.centre());
            if let Some(c) = k.glyph() {
                char_key.entry(c).or_insert(i as u16);
                word_keys.push(i as u16);
            }
        }
        let kw = layout.key_width();
        let keys = Keys {
            centres,
            char_key,
            word_keys,
            kw,
        };
        let mut base = Bank::default();
        for e in &lexicon.entries {
            base.push(&keys, &e.word, e.prior, false);
        }
        let mut base_index = HashMap::new();
        let mut by_len: Vec<Vec<u32>> = Vec::new();
        for (i, e) in base.entries.iter().enumerate() {
            base_index.entry(e.text.clone()).or_insert(i);
            if by_len.len() <= e.seq.len() {
                by_len.resize(e.seq.len() + 1, Vec::new());
            }
            by_len[e.seq.len()].push(i as u32);
        }
        let base_boost = vec![0.0; base.entries.len()];
        Decoder {
            layout,
            keys,
            params,
            base,
            base_index,
            by_len,
            base_boost,
            extra: Bank::default(),
            extra_boost: Vec::new(),
            idents: Vec::new(),
        }
    }

    pub fn layout(&self) -> &Layout {
        &self.layout
    }

    /// Replace the context vocabulary.
    ///
    /// Words from the context get `ctx_boost` (plus a little for repeats). Context words that are
    /// not in the lexicon become extra entries with a mid-rank prior. Identifiers
    /// (`check_dram.py`, `--output-on-failure`, `CamelCase`) are also added whole: their
    /// template is the path through their letters on this layout (other characters are
    /// skipped), so the user can swipe the letter skeleton and get the full token back, case
    /// kept. Separately, when the best candidate is a part of a context identifier, that
    /// identifier is offered as an expansion at rank 3.
    pub fn set_context(&mut self, ctx: &Context) {
        let parsed = Parsed::parse(ctx);
        let p = &self.params;
        let prev = ctx.prev_word.as_ref().map(|w| w.to_lowercase());
        let followers = prev.as_ref().and_then(|w| parsed.next.get(w));
        let boost_of = |w: &str, count: u32| -> f32 {
            let mut b = p.ctx_boost + 0.5 * (count as f32).ln();
            if followers.is_some_and(|f| f.contains(w)) {
                b += p.bigram_boost;
            }
            b
        };
        self.base_boost.fill(0.0);
        self.extra = Bank::default();
        self.extra_boost.clear();
        self.idents.clear();
        let unknown_prior = rank_prior(3000);
        let mut words: Vec<(&String, &u32)> = parsed.words.iter().collect();
        words.sort();
        for (w, &count) in words {
            let b = boost_of(w, count);
            if let Some(&i) = self.base_index.get(w.as_str()) {
                self.base_boost[i] = b;
            } else {
                let before = self.extra.entries.len();
                self.extra.push(&self.keys, w, unknown_prior, false);
                if self.extra.entries.len() > before {
                    self.extra_boost.push(b);
                }
            }
        }
        let mut idents: Vec<(&String, &u32)> = parsed.idents.iter().collect();
        idents.sort();
        for (tok, &count) in idents {
            let before = self.extra.entries.len();
            self.extra.push(&self.keys, tok, unknown_prior, true);
            if self.extra.entries.len() > before {
                self.extra_boost.push(boost_of(tok, count) - 0.5);
                self.idents.push(Ident {
                    text: tok.clone(),
                    parts: parts_of(tok),
                    count,
                });
            }
        }
    }

    /// A stroke that stays within the tap slop is a tap.
    pub fn is_tap(pts: &[Point]) -> bool {
        pts.len() < 2 || extent(pts) < TAP_SLOP_PX
    }

    fn stroke_feat(&self, pts: &[Point]) -> StrokeFeat {
        let raw: Vec<P2> = pts
            .iter()
            .map(|p| [f32::from(p.x), f32::from(p.y)])
            .collect();
        let mut s = [[0.0; 2]; N];
        resample(&raw, &mut s);
        let (cen, scale) = centroid_scale(&s, 2.0 * self.keys.kw);
        let mut nrm = [[0.0; 2]; N];
        for (o, q) in nrm.iter_mut().zip(&s) {
            *o = [(q[0] - cen[0]) / scale, (q[1] - cen[1]) / scale];
        }
        StrokeFeat {
            pts: s,
            nrm,
            start: s[0],
            end: s[N - 1],
            len: path_len(&raw),
        }
    }

    fn score_bank(
        &self,
        bank: &Bank,
        boosts: &[f32],
        s: &StrokeFeat,
        out: &mut Vec<(f32, usize, bool)>,
        is_extra: bool,
    ) {
        let kw = self.keys.kw;
        let sl = self.params.sigma_loc * kw;
        let ss = self.params.sigma_shape;
        let r = 1.7 * kw;
        for (i, e) in bank.entries.iter().enumerate() {
            if dist(s.start, e.start) > r || dist(s.end, e.end) > r {
                continue;
            }
            if s.len < 0.5 * e.len - 0.5 * kw || s.len > 1.6 * e.len + kw {
                continue;
            }
            let t = &bank.pts[i * N..(i + 1) * N];
            let dead = self.params.loc_dead * kw;
            let loc: f32 = s
                .pts
                .iter()
                .zip(t)
                .map(|(a, b)| (dist(*a, *b) - dead).max(0.0))
                .sum::<f32>()
                / N as f32;
            if loc > 1.2 * kw {
                continue;
            }
            let shape: f32 = s
                .nrm
                .iter()
                .zip(t)
                .map(|(a, b)| {
                    dist(
                        *a,
                        [(b[0] - e.cen[0]) / e.scale, (b[1] - e.cen[1]) / e.scale],
                    )
                })
                .sum::<f32>()
                / N as f32;
            let se = self.params.sigma_end * kw;
            let ends = (dist(s.start, e.start) / se).powi(2) + (dist(s.end, e.end) / se).powi(2);
            let sn = self.params.sigma_near * kw;
            let near: f32 = t
                .iter()
                .map(|b| s.pts.iter().map(|a| dist(*a, *b)).fold(f32::MAX, f32::min))
                .sum::<f32>()
                / N as f32;
            let ll = -0.5 * (near / sn).powi(2)
                - 0.5 * (loc / sl).powi(2)
                - 0.5 * (shape / ss).powi(2)
                - 0.5 * ends;
            let total = ll + self.params.prior_weight * e.prior + boosts[i];
            out.push((total, i, is_extra));
        }
    }

    /// Rank words for one swipe. Strokes within the tap slop are decoded as a single tap.
    /// Returns up to `n` candidates, best first.
    pub fn decode_stroke(&self, pts: &[Point], n: usize) -> Vec<Candidate> {
        if pts.is_empty() || n == 0 {
            return Vec::new();
        }
        if Self::is_tap(pts) {
            let (sx, sy) = pts.iter().fold((0i32, 0i32), |a, p| {
                (a.0 + i32::from(p.x), a.1 + i32::from(p.y))
            });
            let c = Point {
                x: (sx / pts.len() as i32) as i16,
                y: (sy / pts.len() as i32) as i16,
                t_ms: 0,
            };
            return self.decode_taps(&[c], n);
        }
        let feat = self.stroke_feat(pts);
        let mut hits = Vec::new();
        self.score_bank(&self.base, &self.base_boost, &feat, &mut hits, false);
        self.score_bank(&self.extra, &self.extra_boost, &feat, &mut hits, true);
        let mut cands = self.finish(hits, n.max(8));
        self.expand(&mut cands, n);
        cands.truncate(n);
        cands
    }

    fn text_of(&self, i: usize, extra: bool) -> &str {
        if extra {
            &self.extra.entries[i].text
        } else {
            &self.base.entries[i].text
        }
    }

    fn finish(&self, mut hits: Vec<(f32, usize, bool)>, n: usize) -> Vec<Candidate> {
        hits.sort_by(|a, b| b.0.total_cmp(&a.0));
        let mut out: Vec<Candidate> = Vec::with_capacity(n);
        for (score, i, extra) in hits {
            let w = self.text_of(i, extra);
            if out.iter().all(|c| c.word != w) {
                out.push(Candidate {
                    word: w.to_string(),
                    score,
                });
                if out.len() == n {
                    break;
                }
            }
        }
        out
    }

    /// Offer the most frequent context identifier that contains the top word as a part.
    fn expand(&self, cands: &mut Vec<Candidate>, n: usize) {
        let Some(top) = cands.first() else { return };
        if n < 2 || self.idents.iter().any(|i| i.text == top.word) {
            return;
        }
        let best = self
            .idents
            .iter()
            .filter(|i| i.parts.contains(&top.word))
            .max_by_key(|i| i.count);
        if let Some(id) = best {
            if cands.iter().take(n).all(|c| c.word != id.text) {
                let at = 2.min(n - 1).min(cands.len());
                let score = cands[at.saturating_sub(1)].score - 1e-3;
                cands.retain(|c| c.word != id.text);
                cands.insert(
                    at,
                    Candidate {
                        word: id.text.clone(),
                        score,
                    },
                );
            }
        }
    }

    /// Words for a run of taps (all the taps since the last word): per-tap Gaussians over the
    /// letter keys times the word prior and context boost.
    ///
    /// Policy: the literal string of nearest keys is #0 when it is a known word (lexicon or
    /// context; it gets `literal_bonus` nats over corrections). Otherwise the best
    /// same-length correction is #0 and the literal is placed #1, so it is always in the
    /// list when `n >= 2`. With no correction at all the literal is #0.
    pub fn decode_taps(&self, taps: &[Point], n: usize) -> Vec<Candidate> {
        if taps.is_empty() || n == 0 || self.keys.word_keys.is_empty() {
            return Vec::new();
        }
        let sigma = self.params.sigma_tap * self.keys.kw;
        // log-likelihood of every word key for every tap
        let lp: Vec<Vec<f32>> = taps
            .iter()
            .map(|t| {
                let q = [f32::from(t.x), f32::from(t.y)];
                self.keys
                    .centres
                    .iter()
                    .map(|c| -0.5 * (dist(q, *c) / sigma).powi(2))
                    .collect()
            })
            .collect();
        let literal_keys: Vec<u16> = lp
            .iter()
            .map(|row| {
                *self
                    .keys
                    .word_keys
                    .iter()
                    .max_by(|a, b| row[**a as usize].total_cmp(&row[**b as usize]))
                    .unwrap_or(&0)
            })
            .collect();
        let literal: String = literal_keys
            .iter()
            .filter_map(|&k| self.layout.keys()[k as usize].glyph())
            .collect();
        let len = taps.len();
        let score_seq = |seq: &[u16]| -> f32 {
            seq.iter()
                .enumerate()
                .map(|(t, &k)| lp[t][k as usize])
                .sum()
        };
        let mut hits: Vec<(f32, usize, bool)> = Vec::new();
        if let Some(list) = self.by_len.get(len) {
            for &i in list {
                let e = &self.base.entries[i as usize];
                hits.push((
                    score_seq(&e.seq)
                        + self.params.prior_weight * e.prior
                        + self.base_boost[i as usize],
                    i as usize,
                    false,
                ));
            }
        }
        for (i, e) in self.extra.entries.iter().enumerate() {
            if !e.ident && e.seq.len() == len {
                hits.push((
                    score_seq(&e.seq) + self.params.prior_weight * e.prior + self.extra_boost[i],
                    i,
                    true,
                ));
            }
        }
        let known = hits.iter().any(|&(_, i, x)| self.text_of(i, x) == literal);
        if known {
            for h in &mut hits {
                if self.text_of(h.1, h.2) == literal {
                    h.0 += self.params.literal_bonus;
                }
            }
        }
        let mut cands = self.finish(hits, n.max(8));
        if !known {
            let score = score_seq(&literal_keys) + self.params.prior_weight * rank_prior(100_000);
            let at = usize::from(!cands.is_empty());
            cands.insert(
                at,
                Candidate {
                    word: literal,
                    score,
                },
            );
        }
        cands.truncate(n);
        cands
    }
}
