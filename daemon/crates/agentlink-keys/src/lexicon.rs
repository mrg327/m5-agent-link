//! The word list: a base English list ranked by estimated frequency, plus developer words.

/// Rank-ordered base list (see `data/README.md` for source and licence).
const ENGLISH: &str = include_str!("../data/english.txt");
/// Developer vocabulary, boosted to the prior of rank [`DEV_RANK`].
const DEV: &str = include_str!("../data/dev.txt");
const DEV_RANK: usize = 150;

/// Log prior (nats) of the word at `rank` (0 = most frequent): a Zipf law.
pub fn rank_prior(rank: usize) -> f32 {
    -((rank as f32) + 10.0).ln()
}

#[derive(Clone, Debug)]
pub(crate) struct LexEntry {
    pub word: String,
    pub prior: f32,
}

/// Words with log priors. Order is rank order for the base list; developer words that are not
/// in the base list come last.
#[derive(Clone, Debug, Default)]
pub struct Lexicon {
    pub(crate) entries: Vec<LexEntry>,
}

impl Lexicon {
    /// The embedded English list (about 39k lowercase words) with developer words boosted.
    pub fn english() -> Self {
        let mut lex = Lexicon::from_ranked(ENGLISH.split_whitespace());
        let dev_prior = rank_prior(DEV_RANK);
        let mut index: std::collections::HashMap<String, usize> = lex
            .entries
            .iter()
            .enumerate()
            .map(|(i, e)| (e.word.clone(), i))
            .collect();
        for w in DEV.split_whitespace() {
            match index.get(w) {
                Some(&i) => lex.entries[i].prior = lex.entries[i].prior.max(dev_prior),
                None => {
                    index.insert(w.to_string(), lex.entries.len());
                    lex.push(w, dev_prior);
                }
            }
        }
        lex
    }

    /// A lexicon from words listed most frequent first; priors follow [`rank_prior`].
    pub fn from_ranked<I, S>(words: I) -> Self
    where
        I: IntoIterator<Item = S>,
        S: AsRef<str>,
    {
        let mut lex = Lexicon::default();
        for (rank, w) in words.into_iter().enumerate() {
            lex.push(w.as_ref(), rank_prior(rank));
        }
        lex
    }

    /// Add a word with an explicit log prior, or raise the prior of an existing one.
    pub fn boost(&mut self, word: &str, prior: f32) {
        let w = word.to_lowercase();
        if let Some(e) = self.entries.iter_mut().find(|e| e.word == w) {
            e.prior = e.prior.max(prior);
        } else {
            self.entries.push(LexEntry { word: w, prior });
        }
    }

    fn push(&mut self, word: &str, prior: f32) {
        self.entries.push(LexEntry {
            word: word.to_lowercase(),
            prior,
        });
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Words in rank order (developer-only words last).
    pub fn words(&self) -> impl Iterator<Item = &str> {
        self.entries.iter().map(|e| e.word.as_str())
    }

    /// The `n` most frequent base words.
    pub fn top_words(&self, n: usize) -> Vec<&str> {
        self.words().take(n).collect()
    }
}

/// The developer words (for evaluation).
pub fn dev_words() -> Vec<&'static str> {
    let mut seen = std::collections::HashSet::new();
    DEV.split_whitespace().filter(|w| seen.insert(*w)).collect()
}
