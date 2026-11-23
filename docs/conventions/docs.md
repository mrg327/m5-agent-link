# Docs: where they live, how they are written, how they are checked

How Markdown in this repository is organised and written. Issue 20261122-db53 (phase 0). We treat docs
as code, as Google does ([SWE at Google ch. 10](https://abseil.io/resources/swe-book/html/ch10.html)):
each doc has an owner and a status, is reviewed with the change that makes it true, and has one
canonical place. "Documents without owners become stale."

Each rule ends with how it is checked: `checked: <tool>`, `review`, or `planned: phase N`
([README](README.md#how-a-rule-becomes-enforced)).

## 1. Where docs live

| Path | Holds | Kind |
|---|---|---|
| `README.md` | What the project is, the repository map, principles, setup | entry point |
| `AGENTS.md` | What an agent must do and run here; a short map, not a manual | agent guide |
| `docs/design/` | One design doc per feature or subsystem (`DESIGN.md`, `PROTOCOL.md`, `FIRMWARE.md`, …) | design |
| `docs/design/glyphs/` | Glyph specs for each screen | design |
| `docs/conventions/` | These pages: how code, docs and changes are written | reference |
| `docs/research/` | Background research that informed a design | explanation |
| `docs/guide/` | How to use the watch | how-to |
| `docs/github/`, `docs/arduino/`, `docs/esp_idf/`, `docs/uiflow2/`, `docs/tools/` | Vendor docs mirrored by `uv run scripts/fetch_docs.py` | vendored |
| `<component>/README.md` | How to build, test and run that component | reference |
| `atlas/CONTRACT.md` | The Atlas module contract and `facts.json` schema | reference |
| `issues/` | The work log ([issues/README.md](../../issues/README.md)) | tracker |

- **DOC-1. One canonical doc per topic.** Link to it; do not copy its content. If two docs disagree,
  fix the one that is not canonical to point at the other. `review`
- **DOC-2. Every new top-level directory appears in the `README.md` repository map.** `review`
  (`planned: phase 2`, the structure check)
- **DOC-3. Every component has a `README.md`** saying what it is, how to build and test it, and
  where its design doc is. Keep it short; the design doc holds the reasoning. `planned: phase 2`
- **DOC-4. Vendored docs are not edited by hand.** Re-run `scripts/fetch_docs.py`. They are exempt
  from the Markdown and link rules below. `review`

### Component READMEs today (2026-11-22)

Have one: the root, `firmware/`, `firmware/components/core/`, `simulator/`, `daemon/`, `protocol/`,
`docs/`, `issues/`, `tools/board/`, `tools/linux/bcm4350c0-le-fix/`, `atlas/wasm/` (`atlas/` has
`CONTRACT.md` instead).

Missing (the DOC-3 baseline): `firmware/components/agentlink_hal/`,
`firmware/components/agentlink_proto/`, `firmware/main/`, `firmware/boards/`, each of the six crates
under `daemon/crates/`, `daemon/integrations/pi/`, `atlas/`, `bench/`, `tools/`, `scripts/`,
`docs/design/`.

## 2. Design docs

A design doc is written before or alongside the code for anything with a trade-off: a new screen,
a protocol message, a new link or driver. A bug fix or a small tweak does not need one; its issue is
enough.

- **DOC-5. Start a new design doc from [templates/design-doc.md](templates/design-doc.md).** File
  name: upper case with dashes, like the others (`MULTI-LINK.md`). `review`
- **DOC-6. Update the design doc in the same change as the code** that changes what it describes
  ([changes.md](changes.md) CHG-13). A design doc is updated until the feature ships, then kept
  true. `review`
- **DOC-7. A superseded doc says so at the top** and links its replacement, and the replacement
  names what it supersedes (as `UI-V2.md` does for `UI-RING.md`). Do not delete it; history is
  useful. `review`

### Front matter

Design docs will carry YAML front matter, so an index and a freshness report can be generated from
them:

```yaml
---
status: draft            # draft | accepted | implemented | superseded
owner: claude-code       # who answers questions about it: an AGENT_NAME or a person's handle
issue: 20261122-db53     # the issue that tracks the work; a list if several
last_reviewed: 2026-11-22
superseded_by: UI-V3.md  # only when status is superseded
---
```

- **DOC-8. Every file in `docs/design/` and `docs/conventions/` has this front matter.**
  `planned: phase 2` (a front-matter check in `scripts/check docs`)
- **DOC-9. `status` is one of `draft`, `accepted`, `implemented`, `superseded`.** `superseded`
  needs `superseded_by`, and its target must exist. `planned: phase 2`
- **DOC-10. Bump `last_reviewed` whenever you check a doc against the code,** not only when you
  edit it. A scheduled report lists `implemented` docs not reviewed for 90 days, as a tracker issue,
  not a failing build. `planned: phase 4`
- **DOC-11. `docs/INDEX.md` is generated from the front matter** and committed; the check
  regenerates it and diffs. `planned: phase 2`

Today none of the 29 design docs has front matter. 18 put a `Status:` line and an issue ID in their
first lines, in free text ("Status: built on branch `next` (issue 20261112-a9d5)"). Phase 2 moves
those into the fields above; until then, keep writing the `Status:` line.

## 3. Decision records

The project already records decisions inside design docs, dated and attributed:

- `docs/design/BENCH.md`: "Decisions (2026-11-22): no camera; the first tasks are …"
- `docs/design/MULTI-LINK.md`: "**Decided (user, 2026-11-14): two is enough.**"
- `docs/design/UI-V2.md` §9 "Decided (user answers, 2026-11-07)", numbered so later text can cite
  "decision 27".
- `docs/design/UI-RING.md` §6 "Decisions", `docs/design/BOARD.md` "Decided (2026-11-09)".

Keep doing that for decisions about one feature. A separate record is for the decisions that cut
across features, or that today live only in chat or in one agent's private memory.

- **DOC-12. Record a feature decision in its design doc** as `Decided (who, YYYY-MM-DD): …` or
  under a `## Decisions (YYYY-MM-DD)` heading. Say what was chosen, the runner-up and why, in one or
  two lines. `review`
- **DOC-13. Record a cross-cutting decision as an ADR** in `docs/decisions/NNNN-imperative-title.md`
  (`0001-use-glyphs-over-words.md`), in Nygard form: Context, Decision, Consequences
  ([MADR](https://github.com/adr/madr) adds options with pros and cons, which is welcome). Write one
  when a future agent would otherwise ask "why is it like this?": the glyph tenet, the patched
  `hci_uart` on Linux, the DRAM budget, the Linux-style commits. `planned: phase 2` (the directory
  and the first records)
- **DOC-14. Decisions are not rewritten.** To change one, add a new dated line or a new ADR that
  supersedes it, and mark the old one superseded. `review`
- **DOC-15. A gotcha that every agent needs goes in the repo,** not in private agent memory: in the
  component README, a nested `AGENTS.md`, or an ADR. ("The simulator cannot catch DRAM limits; read
  the boot log after a flash" is one.) `planned: phase 4` (nested `AGENTS.md`)

## 4. Writing style

Write for a busy reader who may be an agent with no context. We follow the
[Google developer documentation style guide](https://developers.google.com/style) where this page is
silent.

- **DOC-16. Short, plain sentences.** One idea each. Prefer the active voice and the present tense:
  "The daemon sends …", not "It will be sent by the daemon …". `review` (`planned: phase 4`, Vale
  at suggestion level)
- **DOC-17. Lead with the point.** First the answer or the rule, then the reason, then the detail.
  `review`
- **DOC-18. Be concrete.** Name the file, function, command, number and date. "Fast" becomes
  "33 s"; "recently" becomes "2026-11-14". `review`
- **DOC-19. Rules as ID'd bullets** (`DOC-n`, `CHG-n`, `C-n`) that say what to do, why in a clause,
  and how it is checked, as on these pages. `review`
- **DOC-20. Cite upstream instead of restating it.** Link the guide and write down only our deltas.
  `review`
- **DOC-21. Glyphs over words, for UI docs too.** A screen is described by a sketch, a mock render or
  a glyph table before prose, and a new screen spec starts from glyphs, colours and shapes (the
  project tenet: `README.md` Principles 4, [AGENTS.md](../../AGENTS.md),
  [UI-V3.md](../design/UI-V3.md) §1.7). Any text a spec puts on the watch is listed and justified.
  `review` (`planned: phase 4`, an advisory check on long literal strings in screen files)
- **DOC-22. No private data.** No hostnames, usernames, home paths (write `~/esp/esp-idf`, not a
  full path), IPs, emails or tokens. The repo is public. `review`

## 5. Markdown

- **DOC-23. One H1 (`# Title`) per file, on the first line** (after front matter, if any). Issue
  files are exempt: their title is the `title:` front-matter field. `checked: scripts/check docs`
  (markdownlint MD025, MD041)
- **DOC-24. Headings go down one level at a time** and are sentence case. `checked: scripts/check
  docs` (markdownlint MD001; sentence case: `review`)
- **DOC-25. Links inside the repo are relative** (`[PROTOCOL.md](PROTOCOL.md)`,
  `[issues](../../issues/README.md)`), never `/home/...`, a `github.com/.../blob/main/...` URL or an
  absolute path, so they work in a clone, on GitHub and offline. `checked: scripts/check docs`
  (`scripts/check.d/links.py` fails on an absolute path; a `github.com/.../blob/` URL to this repo:
  `review`)
- **DOC-26. Every relative link resolves,** including `#anchors` (GitHub's heading slugs).
  `checked: scripts/check docs` (`scripts/check.d/links.py`; a link to a gitignored file, such as a
  PDF `scripts/fetch_docs.py` downloads, is skipped; external links are checked weekly, not per
  change, `planned: phase 4`)
- **DOC-27. Fenced code blocks name their language** (`sh`, `c`, `rust`, `text`; a diagram or a
  screen sketch is `text`). `checked: scripts/check docs` (markdownlint MD040)
- **DOC-28. Line length is not enforced** (markdownlint MD013 off). Wrap prose at about 100 columns
  in new text so diffs stay readable; never re-wrap a paragraph you are not changing. `review`
  (MD013 is off in `.markdownlint-cli2.jsonc`)
- **DOC-29. Tables for comparisons and lookups, lists for steps.** Keep cells short; a cell that
  needs a paragraph belongs in the text. A `|` inside a code span in a table is written `\|`, or
  GitHub splits the cell. `review` (the cell count: `checked: scripts/check docs`, markdownlint
  MD056)
- **DOC-30. Spelling is checked** in docs and code alike. A real word the checker does not know (a
  field name like `nd`, a colour name like "vermillion") goes in `[default.extend-words]` of
  `_typos.toml` with a comment saying what it is; anything else is fixed. `checked: scripts/check
  docs` (typos)
- **DOC-31. Everything else markdownlint checks by default holds** (blank lines around headings,
  lists, fences and tables; one list marker style per file; no trailing spaces; no stray HTML, so
  write `\<fw>` or `` `<fw>` `` for a placeholder). `checked: scripts/check docs` (markdownlint)

### What `scripts/check docs` runs (phase 1)

`scripts/check.d/docs.sh` runs three tools and fails if any of them reports a line, or if `uvx` or
`npx` is missing (CI installs both; a skip would not be deterministic). 3.3 s on the Linux dev
laptop.

| Tool | Version (pinned in `docs.sh`) | Config | Scope |
|---|---|---|---|
| `scripts/check.d/links.py` | Python 3.11 standard library | in the script | first-party `.md` |
| [typos](https://github.com/crate-ci/typos) via `uvx typos@1.52.0` | 1.52.0 | `_typos.toml` | every file except vendored, generated, binary and word lists |
| [markdownlint-cli2](https://github.com/DavidAnson/markdownlint-cli2) via `npx --yes markdownlint-cli2@0.24.0` | 0.24.0 (markdownlint 0.42.0) | `.markdownlint-cli2.jsonc` | first-party `.md`, except issue files and `CLAUDE.md` imports |

Vendored docs (DOC-4) are excluded from all three: `docs/StopWatch.md`, `docs/arduino/`,
`docs/esp_idf/`, `docs/github/`, `docs/guide/`, `docs/tools/`, `docs/uiflow2/`, the list
`scripts/fetch_docs.py` writes. Issue files (`issues/2*.md`) get the link and spelling checks but
not markdownlint: their front matter and log are written by `scripts/issue` (the log nests its
environment list four spaces deep) and their bodies are notes from many agents.

markdownlint runs its default rule set with these off, each for a reason:

| Rule | Off because |
|---|---|
| MD013 line length | DOC-28 |
| MD060 table column style | Tables are written compact (`\|---\|`, cells unpadded); padding them churns every edit (1018 hits) |
| MD034 bare URLs | GitHub autolinks them; the research docs cite their sources that way (183 hits) |
| MD038 spaces in code spans | The spaces are the point in `` `# ` ``, `` `^Merge ` ``, `` `> ` `` (16 hits) |
| MD036 emphasis as heading | A bold label line ("**macOS permissions**") is house style (6 hits) |
| MD024 duplicate headings | On, `siblings_only`: each section can have its own "Build" |
| MD029 ordered list numbers | Off in `UI-V2.md` only (an inline comment): its decisions are numbered across lists so later text can cite "decision 27" |

## 6. Where we are (baseline, 2026-11-22)

Measured over the 186 tracked `.md` files (`git ls-files '*.md'`) with a small script: strip code,
find `# ` headings, follow every relative `[text](path)` link.

| Measure | Count |
|---|---|
| Tracked Markdown files | 186: 103 in `issues/`, 67 in `docs/`, 16 elsewhere |
| Files with front matter | 102, all in `issues/` (none in `docs/`) |
| Files without a `# ` title | 103: 101 issue files (exempt; they use `title:`), `CLAUDE.md` (a one-line `@AGENTS.md` import, exempt), `docs/github/M5PM1.md` (a byte-order mark before the `#`; vendored) |
| Files with more than one H1 | 1 (`docs/github/M5Unified.md`, vendored) |
| Design docs (`docs/design/**.md`) | 29; 18 with a free-text `Status:` line, 0 with front matter |
| Relative links | 161 |
| Broken relative links | 57: 53 in vendored `docs/github/` mirrors, 3 in `docs/README.md`, 1 in `AGENTS.md` |

The 53 vendored breaks are upstream READMEs whose links point into their own repositories; DOC-4
exempts them, and the link check will exclude `docs/github/`. The baseline items outside it, and the
first of the vendored ones:

1. `AGENTS.md` → `docs/conventions/changes.md` (resolved by this change, which adds the page)
2. `docs/README.md` → `StopWatch_product_page.pdf` (fetched by `scripts/fetch_docs.py`, gitignored)
3. `docs/README.md` → `hardware/` (fetched, gitignored)
4. `docs/README.md` → `datasheets/` (fetched, gitignored)
5. `docs/github/M5GFX.md` → `docs/UnitRCA.md`
6. `docs/github/M5GFX.md` → `docs/README.md`
7. `docs/github/M5GFX.md` → `LICENSE`
8. `docs/github/M5GFX.md` → `src/lgfx/utility/lgfx_tjpgd.c`
9. `docs/github/M5IOE1.md` → `LICENSE`
10. `docs/github/M5PM1.md` → `README_FUNCTION_EN.md`
11. `docs/github/M5StopWatch-PowerDemo.md` → `tests/l0_shutdown/README.md` (and 17 more `tests/…`)
12. `docs/github/M5StopWatch-PowerDemo.md` → `THIRD_PARTY_NOTICES.md`
13. `docs/github/M5Unified.md` → `examples/Basic/HowToUse/HowToUse.ino` (and 8 more `examples/…`)
14. `docs/github/hermes-gadget-sdk_STOPWATCH.md` → `server/README.md`
15. `docs/github/hermes-gadget-sdk_STOPWATCH.md` → `docs/hardware-validation.md`

Items 2–4, decided in phase 1: the link check skips a target that is gitignored (`git check-ignore`),
which covers item 2; `hardware/` and `datasheets/` hold only gitignored PDFs, so the directories do
not exist in a clone and `docs/README.md` now names them as "fetched, not committed" without a link.
`docs/github/M5PM1.md`'s byte-order mark stays (vendored); the link check reads files as
`utf-8-sig`.

### Phase 1 (2026-11-22, issue 20261123-2436)

Measured over first-party docs (vendored mirrors excluded), before and after this change:

| Check | Before | After |
|---|---|---|
| Broken relative links and anchors (`links.py`) | 2 (`docs/README.md` → `hardware/`, `datasheets/`) | 0 |
| typos, whole tree, no config | 297 hits, none a real typo in first-party `.md` (hashes, field names, test strings, vendored files) | 0 with `_typos.toml` |
| markdownlint, default rules minus MD013, non-issue files | 1712 | 0 |

markdownlint before, by rule: MD060 1018, MD034 183, MD032 152, MD022 150, MD040 61, MD031 61,
MD029 26, MD038 16, MD004 14, MD058 13, MD036 6, MD056 4, MD033 3, MD041 2, MD009 2, MD012 1.
Fixed in the `.md` files (39 files, +332 −89 lines, nearly all inserted blank lines): MD022, MD031,
MD032, MD058 (blank lines), MD040 (59 fences marked `text`, 2 `sh`), MD004 (`ui-animation.md`),
MD009, MD012, MD041 (`UI-V2.md`'s superseded note moved below its H1), MD056 (`|` escaped in
table code spans in `atlas/CONTRACT.md` and `daemon/README.md`, which GitHub rendered as extra
columns) and MD033 (`<fw>` and `Promise<number>`, which GitHub dropped as HTML). The rest are off
or scoped, as listed in §5.

## 7. Checks

| Check | Rules | When |
|---|---|---|
| `scripts/check docs` (`scripts/check.d/docs.sh`): `links.py`, typos, markdownlint-cli2 on own docs (§5) | DOC-23 – DOC-27, DOC-30, DOC-31 | `checked` (phase 1) |
| Front matter, `superseded_by` targets, `docs/INDEX.md` regenerate-and-diff, README per component | DOC-3, DOC-8, DOC-9, DOC-11 | `planned: phase 2` |
| Weekly: external links, docs past `last_reviewed`, docs naming paths that no longer exist; files one tracker issue | DOC-10, DOC-26 | `planned: phase 4` |
| Vale (Google style) on `docs/design/` at suggestion level | DOC-16 | `planned: phase 4` |

`links.py` prints `path:line: DOC-n message — how to fix`; typos and markdownlint print
`path:line:col` with their own rule names (the rule table in §5 maps them to DOC rules).
