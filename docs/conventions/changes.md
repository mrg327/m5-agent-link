---
status: accepted
owner: conventions
issue: 20261122-db53
last_reviewed: 2026-11-22
---

# Changes: issues, branches, commits, merges

How a change gets from an idea to `main`. Issue 20261122-db53 (phase 0). The rules borrow Google's
review practice ([small CLs](https://google.github.io/eng-practices/review/developer/small-cls.html),
[CL descriptions](https://google.github.io/eng-practices/review/developer/cl-descriptions.html)) and
the Linux kernel's commit format
([submitting patches](https://docs.kernel.org/process/submitting-patches.html)). We do **not** use
Conventional Commits: we have no automated releases for its `feat`/`fix` types to drive.

Each rule ends with how it is checked: `checked: <tool>`, `review`, or `planned: phase N`
([README](README.md#how-a-rule-becomes-enforced)).

## 1. Issue first

The tracker is `issues/`, driven by `scripts/issue`. [issues/README.md](../../issues/README.md) is the
source of truth for filing, claiming, logging and closing; this page does not restate it.

- **CHG-1. Every change has an issue, claimed before you edit.** The claim commit, pushed, is the lock
  that stops two agents doing the same work. Trivial typo fixes may skip it. `review`
- **CHG-2. Close with evidence.** The close message names the commit and the checks that passed, as
  issues/README.md §Protocol step 5 says. `review`
- **CHG-3. Out-of-scope findings become new issues**, not a wider change. `review`
- **CHG-4. No private data in issues or commits.** The repo is public: no hostnames, usernames, home
  paths, IPs, emails or tokens. Use the `AGENT_HOST` label (`linux-dev`, `mac-work`). `review`

## 2. Branches

- **CHG-5. One branch per effort**, named for the effort in lowercase with dashes (`ux-profile`,
  `multilink`, `glyphs-voice`). A one-commit fix may go straight onto `main`. `review`
- **CHG-6. Pull before you start and before you merge:** `git pull --rebase`. Several machines push
  to the same `main`. `review`
- **CHG-7. Merge into `main` locally with `--no-ff`**, so the branch stays visible as one unit in
  `git log --graph`. There is no required review; [CODEOWNERS](structure.md) is advisory. `review`

## 3. Small, whole changes

- **CHG-8. One logical change per commit.** Google calls about 100 lines normal and about 1000
  "usually too large". Split a large effort into commits that each build and pass. `review`
- **CHG-9. Refactors and behaviour changes go in separate commits.** A reviewer can then read the
  refactor as "no behaviour change" and check only that. `review`
- **CHG-10. Reformatting goes in its own commit,** listed in `.git-blame-ignore-revs` so `git blame`
  skips it. `planned: phase 1`
- **CHG-11. Every commit builds and passes `scripts/check` for its areas,** so `git bisect` works.
  Run `scripts/check --changed` before each commit. `checked: scripts/check` (run by hand today; CI
  calls it from `planned: phase 1`)
- **CHG-12. Tests go in the same change as the code** they test. A bug fix adds a test that fails
  without the fix (a ctest, a simulator script, a cargo test, a vector, or a bench task). `review`
- **CHG-13. Docs go in the same change** when it changes how something is built, tested, used, or
  what it does on the wire or on screen: the design doc, the component README, `PROTOCOL.md`,
  the Atlas. See [docs.md](docs.md). `review`

## 4. Generated files

Some committed files are produced by a tool from other files. They must match their source in every
commit, or the next agent's check fails on a change it did not make.

- **CHG-14. Regenerate and commit generated files in the same commit as their source.** `checked:`
  the commands below, run by `scripts/check` for the area

| Source changed | Regenerate | Commit |
|---|---|---|
| firmware C read by the Atlas (`nav.c`, screens, gestures, palette) | `python3 atlas/tools/extract.py` | `atlas/data/facts.json` |
| firmware UI that the Atlas watch runs | `atlas/wasm/build.sh` | `atlas/vendor/watch.*` |
| `design/mote/mote.toml` or `gen.py` (the Mote's look) | `python3 design/mote/gen.py` | the files it lists (`design/mote/README.md`) |
| `protocol/agentlink.cddl` or the vector generator | `uv run protocol/tools/gen_vectors.py` | `protocol/vectors/*.json` |

The verify side of each is `python3 atlas/tools/extract.py --check`,
`python3 design/mote/gen.py --check` and `git diff --exit-code protocol/vectors` (see [issues/README.md](../../issues/README.md#checks-to-run-before-closing)).
Marking generated files with a header and `linguist-generated` is in [structure.md](structure.md).
`planned: phase 2`

- **CHG-15. Never hand-edit a generated file.** Change the source or the generator. `review`
  (`checked:` by the regenerate-and-diff above)

## 5. Commit messages

### Format

```text
area: summary in the imperative (issue 20261122-db53)
                                                            <- blank line
Why the change is needed: the problem, what the user saw, what was
measured. Then what was done, briefly, and anything a reviewer must
know (limits, follow-ups, what was not tested).
                                                            <- blank line
Co-Authored-By: <agent and model> <noreply address>
```

- **CHG-16. Subject is `area: summary (issue ID)`.** `area` is lowercase, from the list below. The
  issue ID is the full `YYYYMMDD-xxxx`; several go as `(issues 20261122-761f, 20261122-f724)`.
  Commits that only edit the tracker use `issue:` and may name short IDs (`issue: claim db53`).
  `checked: scripts/check commits` (scripts/check.d/commit_msg.py); the check accepts the ID in the
  subject or the body
- **CHG-17. Imperative mood:** "add", "fix", "drop", as if completing "this commit will …". Not
  "added", "adds" or "adding". `review`; `checked: scripts/check commits`
  (scripts/check.d/commit_msg.py) refuses a short list of leading words (`added`, `fixes`,
  `updating`, …)
- **CHG-18. Subject at most 72 characters,** with no trailing full stop. Move detail to the body.
  `checked: scripts/check commits` (scripts/check.d/commit_msg.py)
- **CHG-19. The subject stands alone.** "fix bug", "wip", "phase 1", "updates" say nothing in
  `git log --oneline`; Google lists them as bad examples. `checked: scripts/check commits`
  (scripts/check.d/commit_msg.py, a short deny list)
- **CHG-20. A blank line, then a body for any non-trivial change.** The body says *why* first, then
  *what*; the diff already shows *how*. Quantify claims ("463 s -> 33 s"). Keep it self-contained:
  a reader should not need the chat or the issue to follow it. `review`
- **CHG-21. Wrap the body at 72 columns.** Long URLs, paths and pasted output may run over.
  `checked: scripts/check commits` (scripts/check.d/commit_msg.py; lines with a URL, lines indented
  4 spaces and fenced code are exempt)
- **CHG-22. Trailers go last, one per line, after a blank line.** `checked: scripts/check commits`
  (scripts/check.d/commit_msg.py)

Good:

```text
power: retry a refused deep sleep (issue 20261109-a1b2)

When the power chip refuses deep sleep we never asked again, so a
watch left on the desk stayed awake until its battery ran out. Retry
every 30 s while the screen is off, and log each refusal.
```

Bad:

```text
Fixed power stuff and also updated the docs and some UI tweaks
```

(No area, past tense, three changes in one, no issue, no body.)

### Areas

`area` names the component, or a module inside it, that the change is about. Use the narrowest one
that covers the whole change. These are the prefixes in use, from the last 409 commits (all of
history at 2026-11-22):

| Area | Uses | Area | Uses |
|---|---|---|---|
| `issue` | 103 | `bench` | 4 |
| `issues` (use `issue`) | 33 | `agentlinkd` | 4 |
| `daemon` | 25 | `jobs`, `keys`, `protocol` | 4 each |
| `ui` | 23 | `sim` | 3 |
| `docs` | 22 | `power`, `gauges`, `core` | 3 each |
| `firmware` | 18 | `ota`, `nav`, `ble`, `trackpad`, `tools/board`, `agentlink` | 2 each |
| `atlas` | 13 | `ask`, `build`, `display`, `hook`, `hooks`, `perf`, `pointer`, `proto`, `queue`, `remote`, `sound`, `ui_ota`, `ui_voice` | 1 each |
| `watch` | 9 | `firmware/sim/atlas`, `agentlink-hook/cli` | 1 each |
| `voice`, `board` | 5 each | | |

- **CHG-23. Pick from these areas.** Top-level components: `firmware`, `sim`, `daemon`, `protocol`,
  `atlas`, `bench`, `board`, `tools`, `scripts`, `docs`, `ci`, `issue`. Inside a component, a module
  or crate name is fine when the change stays in it: `ui`, `nav`, `power`, `ble`, `ota`, `voice`,
  `jobs`, `keys`, `core` (firmware); `agentlinkd`, `agentlink`, `hook` (daemon). Spell each one
  one way: `issue` not `issues`, `protocol` not `proto`, `sim` not `simulator`. `review`;
  `checked: scripts/check commits` (scripts/check.d/commit_msg.py) fails on `issues`, `proto`,
  `simulator` and `hooks`, and warns on an area not in this list
- **CHG-24. A change across two areas** names both, comma-separated: `docs, protocol: …`. Three or
  more usually means the commit should be split. `review`
- **CHG-25. A protocol version bump** goes in the summary, not the area:
  `protocol: 0.21, fw_begin.patch for delta updates (issue 20261113-ef2c)`, not `proto 0.21: …`.
  `review`

### Trailers

- **CHG-26. Credit AI help with `Co-Authored-By:`** naming the agent and model, e.g.
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Git treats the key case-insensitively;
  write it `Co-Authored-By`. A human runs every agent and is responsible for what it commits, as in
  the [Linux AI policy](https://docs.kernel.org/process/coding-assistants.html). An agent never adds
  `Signed-off-by:`. `checked: scripts/check commits` (scripts/check.d/commit_msg.py: spelling
  always; the hook also requires the trailer and refuses `Signed-off-by:` when `AGENT_NAME` is set)
- **CHG-27. Other trailers we use:** `Claude-Session:` (the session link the Claude Code harness
  adds), `Fixes: <12-char sha> ("subject")` when a commit repairs an earlier one, and
  `Co-Authored-By:` for a second human. No `Link:` or `Closes:` to private URLs. `review`

### Where we are (baseline, 2026-11-22)

Measured over all 409 commits (`git log --format=%s -n 500`; the repo has fewer than 500):

| Measure | Count | Share |
|---|---|---|
| Subject matches `^[a-z0-9/_-]+: ` | 317 | 78% |
| Subject at most 72 characters | 266 | 65% |
| Both (the CHG-16 shape and CHG-18 length) | 211 | 52% |
| Work commits (not `issue:` or merges) naming an issue ID | 142 of 249 | 57% |
| `Co-Authored-By:` trailer present | 381 | 93% |
| Commits with a body that has a line over 72 columns | 155 of 193 | 80% |

Most misses are older subjects that start with a capital ("Queued messages: …"), use a version as
the area ("proto 0.17: …"), or list areas with a comma (`docs, e2e:`, which CHG-24 now allows). Body
lines have mostly been wrapped near 100–120 columns; CHG-21 applies to new commits only. The
commit-message check will look at new commits only and never rewrite history.

## 6. Merges

- **CHG-28. Merge subject: `Merge branch 'x': summary (issues …)`**, naming what the branch adds
  and every issue it closes, e.g.
  `Merge branch 'bench': drive the watch like a finger, frame timeline and task report (issue 20261122-bb2e)`.
  Merge subjects may exceed 72 characters. `checked: scripts/check commits`
  (scripts/check.d/commit_msg.py)
- **CHG-29. Merging `main` into a branch** keeps git's default `Merge branch 'main' into x`; add a
  summary when it brings something the branch depends on. `review`
- **CHG-30. Close the branch's issues right after the merge** (`issue: close …`), with the merge
  commit as evidence. `review`

## 7. Before you commit

```sh
scripts/check --changed            # the checks for the areas you touched
git diff --stat                    # one logical change? generated files included?
scripts/issue log ID "…"           # what you did and what you checked
```

## 8. The commit-message check

`scripts/check.d/commit_msg.py` checks the rules of §5 and §6 marked `checked: scripts/check
commits`. It runs in two ways:

```sh
git config core.hooksPath scripts/hooks          # once per clone: the commit-msg hook
python3 scripts/check.d/commit_msg.py            # every commit on HEAD after the cutoff
python3 scripts/check.d/commit_msg.py --range main..HEAD   # one branch's commits
```

The hook (`scripts/hooks/commit-msg`) checks the message you are writing, before the commit exists;
if it fails, fix the message and retry with
`git commit -e -F "$(git rev-parse --git-dir)/COMMIT_EDITMSG"`. `fixup!` and `squash!` commits pass
the hook, since they are squashed away. Run without `--file`, the script checks every commit after
the cutoff in `scripts/check.d/baselines/commit_msg_since.txt`, the last commit before the check
existed, so older history is never checked and never rewritten. A merge is checked against CHG-28
only. CI needs full history for this (`fetch-depth: 0`).

What it checks:

1. subject matches `^[a-z0-9/_-]+(, [a-z0-9/_-]+)*: \S`, or is a merge (CHG-16, CHG-28);
2. an area is spelled one way (`issue` not `issues`); an area outside the §5 list warns (CHG-23);
3. subject is at most 72 characters (merges exempt) and does not end in `.` (CHG-18);
4. a work commit (not `issue:`, not a merge) names an issue ID `\d{8}-[0-9a-f]{4}` in the subject
   or the body; a merge named `Merge branch 'x': …` names one too (CHG-16, CHG-28);
5. the summary is not on a short deny list (`fix bug`, `wip`, `update`, `phase \d`) and does not
   start with `added`, `fixes`, `updating` and the like (CHG-19, CHG-17);
6. a blank line follows the subject, and body lines are at most 72 columns (URLs, indented lines
   and fenced code exempt) (CHG-20, CHG-21);
7. trailers come last, in one paragraph, and `Co-Authored-By` is spelled so (CHG-22, CHG-26);
8. in the hook, when `AGENT_NAME` is set: a `Co-Authored-By:` trailer is present and there is no
   `Signed-off-by:` (CHG-26).

Each failure prints `commit:line: CHG-n message — how to fix`, so an agent can amend and retry.
Of the 50 commits before the cutoff, 30 would have failed: 24 on subject length, 15 on body
wrapping, 7 on `Co-authored-by` spelling, 4 on the subject shape and 2 on merge subjects.
