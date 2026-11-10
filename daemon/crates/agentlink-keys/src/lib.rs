//! Gesture-typing decoder for the watch's round keyboard (SHARK2, Kristensson & Zhai 2004).
//!
//! The watch sends key geometry once and then touch strokes; this crate turns them into ranked
//! word candidates. Pure Rust, no dependencies, no I/O.
//!
//! ```text
//! Layout::new(keys) | Layout::from_packed(bytes) | Layout::stopwatch_qwerty()
//!     layout.key_at(x, y) -> Option<&Key>
//! unpack_points(bytes) -> Result<Vec<Point>, Error>      // 6 B/pt LE: i16 x, i16 y, u16 t_ms
//! Layout::from_packed: 10 B/key LE: u16 cp, i16 x, i16 y, u16 w, u16 h
//! Lexicon::english() -> Lexicon                          // embedded list + developer words
//! Context { text: Vec<String>, prev_word: Option<String> }
//! Decoder::new(layout, lexicon) -> Decoder               // precomputes templates (once)
//! Decoder::set_context(&mut self, &Context)              // cheap; hundreds of tokens
//! Decoder::decode_stroke(&self, &[Point], n) -> Vec<Candidate>
//! Decoder::decode_taps(&self, &[Point], n) -> Vec<Candidate>
//! Decoder::is_tap(&[Point]) -> bool                      // extent < 18 px
//! Candidate { word: String, score: f32 }                 // log score, higher is better
//! ```
//!
//! Output words are lowercase, except context identifiers, which keep their case.
//! Timestamps are not used by the decoder (arc-length resampling makes it speed independent).
//! `Lexicon::with_context` of the plan is folded into `Decoder::set_context`, which keeps the
//! base templates and overlays the context vocabulary.
//!
//! See [`Decoder::set_context`] for the context rules and [`Decoder::decode_taps`] for the tap
//! policy. [`eval`] and [`synth`] hold the synthetic evaluation harness.

mod context;
mod decode;
pub mod eval;
mod layout;
mod lexicon;
pub mod synth;

pub use context::Context;
pub use decode::{Candidate, Decoder, Params, TAP_SLOP_PX};
pub use layout::{extent, integrate_dt, pack_points, unpack_points, Error, Key, Layout, Point};
pub use lexicon::{dev_words, rank_prior, Lexicon};
