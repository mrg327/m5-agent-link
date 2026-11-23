# Agent guide

Several agents work on this repository at once, on different machines. Before you start, read
[`issues/README.md`](issues/README.md). It explains how to file, claim, log and close work with
`scripts/issue`, and which checks each area needs. In short:

```sh
export AGENT_NAME=<you> AGENT_HOST=<machine label>   # no hostnames or usernames: this repo is public
git pull --rebase && scripts/issue list
scripts/issue claim ID        # before you edit anything
scripts/issue log ID "…"      # as you go
scripts/issue close ID fixed "commit …; checks …"
```

The design docs are under `docs/design/` and the repository map is in `README.md`.

## Conventions and checks

How code, names, docs and commits are written is in [`docs/conventions/`](docs/conventions/README.md):
one page per language, plus structure, protocol names, changes and a glossary. Read the page for the
area you touch before you edit it.

Run the checks before you commit, and name them when you close an issue:

```sh
scripts/check --changed    # the areas your branch touches
scripts/check sim daemon   # or name areas: sim daemon protocol atlas firmware docs
scripts/check --list       # the exact command behind each area
```

`firmware` builds only where ESP-IDF is on `PATH`, so run it after `. ~/esp/esp-idf/export.sh` when
you touch firmware. A SKIP in the summary means that area did not run, not that it passed.

Commit subjects are `area: summary (issue ID)`, as in `git log` ([changes.md](docs/conventions/changes.md)).

**Project tenet: glyphs over words.** We should use the least amount of text possible and represent
information via an image or glyph whenever possible (`README.md` Principles 4, `docs/design/UI-V3.md`
§1.7). Apply it to every watch screen you add or change.
