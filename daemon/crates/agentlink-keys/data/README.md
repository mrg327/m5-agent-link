# Lexicon data

`english.txt` is the embedded base word list (about 39k lowercase words, one per line, most
frequent first, ~340 KB). `dev.txt` is a short developer vocabulary (git, commit, cargo, ...)
that gets the prior of rank 150. `head.txt` is the hand-written order of the ~500 most common
English words. All of it is loaded with `include_str!`; nothing is fetched at run time.

## Source and licence

The words come from **SCOWL** (Spell Checker Oriented Word Lists, 2020.12.07, Kevin Atkinson,
<http://wordlist.aspell.net/>). SCOWL's licence (its `Copyright` file) lets the lists, scripts
and output be used, modified, distributed and sold for any purpose without fee, provided the
copyright notice is kept; its sources are public domain (Moby Words II, the UK English Wordlist
with Frequency Classification) or similarly permissive. That is compatible with MIT/Apache-2.0.

> The collective work is Copyright 2000-2018 by Kevin Atkinson as well as any of the
> copyrights mentioned below. Permission to use, copy, modify, distribute and sell these word
> lists, the associated scripts, the output created from the scripts, and its documentation
> for any purpose is hereby granted without fee, provided that the above copyright notice
> appears in all copies and that both that copyright notice and this permission notice appear
> in supporting documentation. Kevin Atkinson makes no representations about the suitability
> of this array for any purpose. It is provided "as is" without express or implied warranty.

Used: `english-words` and `american-words` at size levels 10, 20 and 35, plain `a-z` only,
no proper names, abbreviations or variants.

## Why SCOWL and not a frequency list

Real frequency lists were rejected for licence reasons: `wordfreq` data is CC-BY-SA,
Norvig's `count_1w` and `google-10000-english` derive from the Google Web 1T / LDC corpus
(LDC licence, explicitly not for commercial use), OpenSubtitles-based lists are CC-BY-SA.

SCOWL has no frequencies, only size levels (10 ~ the commonest words, 35 ~ ordinary
vocabulary). The rank used for the prior is therefore an estimate (`build_lexicon.py`):

1. `head.txt` (the commonest words, ordered by hand from general knowledge of English),
2. then SCOWL level 10, 20, 35 in turn; inside a level shorter words first (short words are
   in general more frequent),
3. an inflected form (`-s -ed -ing -er -ly ...`) is moved behind its base word.

The prior is a Zipf law on this rank (`-ln(rank + 10)`). Consequences: top-N "most frequent
words" in the evaluation are this estimated order, and real usage will differ from it. The
context vocabulary and the user's recent words (see `Decoder::set_context`) correct most of
that in practice. If a permissive real frequency list turns up, replace `english.txt` (same
format) and the code needs no change.

Regenerate with `python3 build_lexicon.py /path/to/scowl-2020.12.07`.
