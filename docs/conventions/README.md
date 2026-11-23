---
status: accepted
owner: conventions
issue: 20261122-db53
last_reviewed: 2026-11-22
---

# Conventions

How code, docs and changes are written in this repository, and how each rule is checked. Issue
20261122-db53 (phase 0). The research behind these choices compared Google, Microsoft, Linux, Zephyr,
ESP-IDF, BARR-C, MISRA and CERT; the short version is: follow each ecosystem's own dominant standard
for style, and borrow the large organisations' *system* for keeping it (one check command, rules that
map to checks, baselines that only shrink).

Run the checks before every commit:

```sh
scripts/check              # every area
scripts/check --changed    # only the areas your branch touches
scripts/check --list       # what each area runs
```

## Pages

| Page | Covers |
|---|---|
| [c.md](c.md) | C in `firmware/` and `simulator/`: ESP-IDF style, naming, modules, errors, safety rules, Doxygen |
| [rust.md](rust.md) | The `daemon/` workspace: API Guidelines, crate layering, errors, panics, lints, docs |
| [python.md](python.md) | Python tools: Google style, Ruff, mypy, `uv run` scripts |
| [js.md](js.md) | JavaScript: ESLint, JSDoc, `tsc --checkJs`, Prettier |
| [protocol.md](protocol.md) | CDDL names, versions, message numbers, how each language spells a protocol name |
| [structure.md](structure.md) | Directory layout, dependency rules between components, where new code goes, generated files |
| [changes.md](changes.md) | Issues, branches, commit messages, merges |
| [docs.md](docs.md) | Design docs, front matter, decision records, Markdown rules |
| [tooling.md](tooling.md) | What we measured with clang-format, clang-tidy, `idf.py clang-check` and Doxygen |
| [glossary.md](glossary.md) | The project's words and how code spells them |
| [templates/design-doc.md](templates/design-doc.md) | Start a new design doc from this |

## Decisions (2026-11-22)

- C: 100 columns, `char *p`, `/* */` comments only, functions capped at 60 lines. One-line functions
  stay; a body without braces only on one line (`if (!p) return;`); `_fn` and `_fn_t` both allowed.
- C errors: `esp_err_t`-style codes in every module, including those that also build in the simulator.
- Rust: no `unwrap()` or `expect()` outside tests; an exception carries `#[expect(..., reason = "…")]`.
- Commits: Linux-style `area: summary (issue ID)` subjects, not Conventional Commits.
- JS formatting: Prettier. CODEOWNERS: advisory, not required for merges.

## How a rule becomes enforced

Every rule ends with how it is checked: `checked: <tool>` (a check fails today), `review` (a person or
agent checks it in review), or `planned: phase N`. A new check lands in this order:

1. The rule is written here, with its check named.
2. The check runs on changed lines only, and its existing violations are counted in a baseline.
3. The baseline may only go down. When it reaches zero the check blocks everywhere.

Reformatting is one commit per language, listed in `.git-blame-ignore-revs` so `git blame` skips it.

## Phases

| Phase | Lands |
|---|---|
| 0 | These pages, `.editorconfig`, `scripts/check`, the check list in `AGENTS.md` |
| 1 | Formatter configs and one reformat per language (clang-format, rustfmt, Ruff, Prettier), markdownlint, typos, offline link check; CI calls `scripts/check` |
| 2 | Generated-code markers, the structure check (include and crate rules), `ARCHITECTURE.md`, CODEOWNERS, doc front matter |
| 3 | Linters on changed lines with baselines: clang-tidy, the clippy table, Ruff's wider set and mypy, ESLint with `checkJs` |
| 4 | Doc coverage (Doxygen, `missing_docs`, Ruff `D`, JSDoc), the commit-message check, per-component `AGENTS.md` |
