#!/usr/bin/env python3
"""Rebuild data/english.txt from a SCOWL checkout.

usage: build_lexicon.py /path/to/scowl-2020.12.07

Output: one lowercase word per line, most frequent first (an estimated rank,
see README.md). Only words from the american/english "words" lists up to size
level 35 are used, plain a-z only.
"""
import sys
from pathlib import Path

LEVELS = [10, 20, 35]
TWO_LETTER = set(
    "am an as at be by do go he if in is it me my no of oh on or so to up us we ok ah eh ha ho hi".split()
)
MAX_LEN = 16

here = Path(__file__).parent
final = Path(sys.argv[1]) / "final"
head = (here / "head.txt").read_text().split()

SUFFIXES = [("ies", "y"), ("ied", "y"), ("ing", ""), ("ing", "e"), ("ers", ""), ("er", ""),
            ("es", ""), ("ed", ""), ("ed", "e"), ("ly", ""), ("s", ""), ("d", "")]


def demote_inflections(words):
    """An inflected form is rarer than its base: rank it after the base."""
    rank = {w: i for i, w in enumerate(words)}
    eff = {}
    for w, i in rank.items():
        r = float(i)
        for suf, rep in SUFFIXES:
            if w.endswith(suf) and len(w) - len(suf) >= 2:
                base = w[: len(w) - len(suf)] + rep
                if base in rank and rank[base] < i:
                    r = max(r, 1.5 * rank[base] + 50)
                    break
        eff[w] = r
    return sorted(words, key=lambda w: (eff[w], rank[w]))


out, seen = [], set()


def add(w):
    if w not in seen:
        seen.add(w)
        out.append(w)


for w in head:
    add(w)

for level in LEVELS:
    words = set()
    for variant in ("english", "american"):
        p = final / f"{variant}-words.{level}"
        for line in p.read_bytes().decode("latin-1").split("\n"):
            w = line.strip()
            if not (w.isascii() and w.isalpha() and w.islower()):
                continue
            if len(w) > MAX_LEN or (len(w) == 1 and w not in "ai"):
                continue
            if len(w) == 2 and w not in TWO_LETTER:
                continue
            words.add(w)
    # No frequency inside a SCOWL size level: shorter words first, as a rough proxy.
    for w in sorted(words, key=lambda x: (len(x), x)):
        add(w)

out = demote_inflections(out)
(here / "english.txt").write_text("\n".join(out) + "\n")
print(len(out), "words")
