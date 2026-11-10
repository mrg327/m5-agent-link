//! Context vocabulary: words and identifiers from the conversation, screen and repository.

use std::collections::{HashMap, HashSet};

/// What the daemon knows about what the user is likely to type.
#[derive(Clone, Debug, Default)]
pub struct Context {
    /// Free text: the agent's question, terminal screen, file names, recent user words.
    pub text: Vec<String>,
    /// The word typed just before the one being decoded, for the bigram boost.
    pub prev_word: Option<String>,
}

const MAX_TOKEN: usize = 48;
const MAX_IDENTS: usize = 3000;

#[derive(Debug, Default)]
pub(crate) struct Parsed {
    /// Plain lowercase words (including the alphabetic parts of identifiers) with counts.
    pub words: HashMap<String, u32>,
    /// Whole identifiers (`check_dram.py`, `--output-on-failure`, `CamelCase`), case kept.
    pub idents: HashMap<String, u32>,
    /// Words that directly follow the given word in the context text.
    pub next: HashMap<String, HashSet<String>>,
}

fn is_ident(tok: &str) -> bool {
    let mut prev_lower = false;
    for c in tok.chars() {
        if !c.is_alphabetic() {
            return true;
        }
        if c.is_uppercase() && prev_lower {
            return true;
        }
        prev_lower = c.is_lowercase();
    }
    false
}

/// Alphabetic runs of a token, split at separators and at CamelCase boundaries, lowercased.
fn parts(tok: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut cur = String::new();
    let mut prev_lower = false;
    for c in tok.chars() {
        if !c.is_alphabetic() {
            if !cur.is_empty() {
                out.push(std::mem::take(&mut cur));
            }
            prev_lower = false;
            continue;
        }
        if c.is_uppercase() && prev_lower && !cur.is_empty() {
            out.push(std::mem::take(&mut cur));
        }
        prev_lower = c.is_lowercase();
        cur.extend(c.to_lowercase());
    }
    if !cur.is_empty() {
        out.push(cur);
    }
    out
}

pub(crate) fn parts_of(tok: &str) -> Vec<String> {
    parts(tok)
}

fn trim_token(raw: &str) -> &str {
    let t = raw.trim_matches(|c: char| "\"'`()[]{}<>,;:!?*".contains(c));
    t.trim_end_matches(['.', '-'])
        .trim_start_matches(['.', '/'].as_slice())
        .trim_end_matches('/')
}

impl Parsed {
    pub fn parse(ctx: &Context) -> Parsed {
        let mut p = Parsed::default();
        let mut prev: Option<String> = None;
        for text in &ctx.text {
            for raw in text.split_whitespace() {
                let tok = trim_token(raw);
                if tok.is_empty() || tok.chars().count() > MAX_TOKEN {
                    continue;
                }
                let ident = is_ident(tok);
                if ident && (p.idents.len() < MAX_IDENTS || p.idents.contains_key(tok)) {
                    *p.idents.entry(tok.to_string()).or_insert(0) += 1;
                }
                for w in parts(tok) {
                    if w.chars().count() < 2 && !(w == "a" || w == "i") {
                        continue;
                    }
                    if let Some(pv) = prev.take() {
                        p.next.entry(pv).or_default().insert(w.clone());
                    }
                    *p.words.entry(w.clone()).or_insert(0) += 1;
                    prev = Some(w);
                }
            }
            prev = None;
        }
        p
    }
}
