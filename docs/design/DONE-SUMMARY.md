# Done summary: what the turn did, at a glance

> Status: built on branch `done-summary` (issue 20261112-d01e). Protocol 0.14. Simulator-verified; not run on the
> device or against a live Claude Code session.

A finished agent card said little more than that it finished. It now says what the turn did: how many files it
changed, how many lines it added and removed, whether the tests passed, and how long it took:

```
        ✓ Done  1m 34s
        ━━━━━━━───────            (context capsule, GAUGES.md)
   3 files · +120 –14 · ✓ passed · 4m 12s
```

Every part is optional. A part the daemon cannot stand behind is absent, never zero: no `0 files`, no
`tests not run`, no `+0 –0`. This is claimed for Claude Code only (Copilot's hook payloads differ and are not
tallied).

## 1. Data sources, and what each figure means and misses

All of it is tallied by the daemon (`agentlinkd/src/turn.rs`) from the hook payloads of the session
([hooks](https://code.claude.com/docs/en/hooks), checked 2026-11-12). The status line's cumulative
`cost.total_lines_added` / `total_lines_removed` are **not** used (see "Why not the status line"). The tally
starts at `UserPromptSubmit` and the summary is taken at `Stop` (or `StopFailure`, or a `Notification` that
enters done / error). Hook lines of Claude sub-agents carry the parent's `session_id`, so their edits and test
runs count toward the turn.

| Figure | Source | Means | Misses |
|---|---|---|---|
| **Files** (`fc`) | `PostToolUse` of `Edit`, `Write`, `MultiEdit`, `NotebookEdit`: distinct `tool_input.file_path` (`notebook_path`). A failed call (`PostToolUseFailure`) changed nothing and is not counted. | Distinct files the agent's edit tools changed this turn (a file edited twice counts once). Sent only when at least one was seen. | Files changed by shell commands (`sed -i`, `git checkout`, a formatter, a code generator, `mv`) and by anything outside the session. A turn that only used the shell shows no files at all, not `0 files`. More than 4096 distinct paths: the count is unknown. |
| **Lines** (`la`, `lr`) | Per edit call: `tool_response.structuredPatch` when present (exact `+` / `-` lines); else `Edit` / `MultiEdit`: a line diff of `old_string` against `new_string` (shortest edit script, so a changed line is one added and one removed); `Write` of a new file (`tool_response.type == "create"`): its lines, all added. Summed over the calls. | Gross lines added / removed across the calls. Two edits of one line count twice. | If any single call's lines cannot be told, the whole figure is unknown (and the files stay): `replace_all` edits (how many places is not in the payload), `Write` over an existing file without a patch (the old text is not in the payload), `NotebookEdit`, diffs over 4 M cells. Shell edits, as for files. |
| **Tests** (`tp`) | `PostToolUse` / `PostToolUseFailure` of `Bash` whose command is a recognised test command (§2). The **last** such run of the turn decides. | `true`: it exited 0. `false`: it exited with a code that means the tests failed. | Anything not recognised (a script, `cargo test` behind a pipe, a custom runner). A later recognised run whose result is unknown erases an earlier verdict: the line shows nothing rather than a stale one. "Not run" is not shown: the daemon cannot tell "no tests" from "tests the list does not know". |
| **Time** (`dur`) | The daemon's clock, from the prompt's hook line to the stop's. Whole seconds. | Wall-clock turn length, including time spent waiting for your approvals. | A session the daemon met mid-turn (its prompt was not seen) gets **no summary at all**: the files and lines would be incomplete. A turn that Claude Code never reports a stop for (interrupted with Esc) gets none. |

Claude Code's `tool_response` for `Bash` has no exit-code field: a command that exits non-zero arrives as
`PostToolUseFailure` with `error` starting `Exit code N`. So `PostToolUse` of a recognised test command means
exit 0, and the failure's `N` is read from the first line of `error`.

### Why not the status line

The status line's line counters are cumulative for the session and move only when it runs (after an assistant
message, debounced), so the difference between "at the prompt" and "at the stop" would need a reading at both
ends that the status line is not obliged to deliver; it also counts what Claude Code's own accounting counts,
which this daemon cannot inspect. The hook tally is exact for the tools it understands and says plainly what it
does not.

## 2. Test commands

`TEST_COMMANDS` in `turn.rs` is a table of word sequences; flags between the words are skipped, a trailing `*`
matches any suffix. Recognised: `cargo test`, `cargo nextest run`, `npm|pnpm|yarn|bun test` and
`run test*`, `npm t`, `pytest`, `py.test`, `python -m pytest|unittest`, `tox`, `go test`, `ctest`,
`make test|tests|check`, `ninja test`, `just test`, `mvn test|verify`, `gradle test`, `gradlew test`,
`dotnet test`, `jest`, `vitest run`, `phpunit`, `rspec`, `mix test`, `swift test`, `deno test`,
`flutter test`. Leading `VAR=x`, `time`, `nice`, `env`, `npx`, `uv run`, `poetry run` and similar wrappers are
skipped. `--no-run`, `--help`, `--list`, `--collect-only`, `--version`, `--dry-run` make it not a test run.
Add a row to teach the daemon another runner.

The exit status must be the tests' status, so the command line is read as a chain of `&&` only. A pipe, `;`,
`||`, a background `&`, a newline or a substitution makes it unrecognised. Success of the chain is a pass of
every test in it. A failure is a fail only when the test is the one failing: exactly one test command, preceded
only by `cd` / `export` / `set` / `source`, and nothing after it (`cargo build && cargo test` failing may be the
build); exit codes 126, 127 and 128 and up (not found, killed) are unknown. A run in the background or reported
as interrupted has no verdict. Pass / fail only: no counts are parsed from the output.

## 3. Reset and errors

The tally is per session and starts over at each `UserPromptSubmit`; the stored figures are cleared with it. A
turn that ends in `StopFailure` (state error) gets its summary from the same tally. A second stop without a new
prompt (a stop hook sent the agent back to work) sends the summary again, restated in full.

## 4. The watch: layout

On HOME the finished card (state `done` or `error`, not a job, not a request, not "On your computer") draws one
line at y 246 (centre), `FONT_BODY` (20 px) `COL_TEXT2`, tabular digits, centred, in the place of the stale
last-step text (`Bash · cargo test`) that the card showed before: that text names the step before the stop, not
what the turn amounted to. Everything else on the card stays: caption, subject, state row with the time since the
finish, and the context capsule (y 219).

Parts, in order: `3 files` (`1 file`), `+120 –14` (en dash, the font has no minus), `✓ tests passed` /
`✗ tests failed` (the existing Phosphor Fill check-circle / x-circle, 20 px), `4m 12s` (`<1s` under a second;
`1h 02m`), joined by ` · `. A part that is unknown is not drawn. **Failed tests alone are in the error colour**
(vermillion, the icon with it); everything else stays grey, so the one thing that wants attention is the only
thing coloured. Offline, the card is dimmed and the colour is dropped, as for the state word.

**Width.** The line may be 372 px wide: the ring's pips sit at r 200 and the line at y 246 reaches 13 px
below the centre, so it keeps about 12 px from them. When it is wider it gives way in this order:

1. `tests passed` becomes `passed` (`tests failed` becomes `failed`); the icon and the colour say the rest;
2. the lines (`+120 –14`) go;
3. the time goes;
4. the files go.

The tests are never dropped: they are the only part that can need you. The separator follows whichever part is
first. Measured with the real font in the simulator: `3 files · +120 –14 · ✓ passed · 4m 12s` fits (288 px);
`1234 files · +123456 –98765 · ✓ failed · 11h 06m` does not, and shows `1234 files · ✗ failed · 11h 06m`.

**Activity** (no new screen, no new action). A finished session's footer (y 322, `FONT_CAPTION`, 352 px) shows
the same line in `COL_TEXT2` in place of the `Step n · time` footer, which is shown only while it works. It is
one colour: a failure is in the words. At the caption size the lines usually give way first.

Terminal jobs keep their own finished / failed card (JOBS.md §5): the summary is never drawn for them.

## 5. Protocol 0.14

```cddl
session.30 fc:  uint   ; files the turn changed (>= 1); absent = not known
session.31 la:  uint   ; lines added, sent with lr
session.32 lr:  uint   ; lines removed
session.33 tp:  bool   ; the turn's last recognised test run: true passed, false failed
session.34 dur: uint   ; the turn's length in seconds, prompt to stop
```

Additive; keys numbered from 30 so that it merges with the other 0.13 work. Sent only to devices whose `hello`
says minor >= 14, with the delta that enters done / error (and in the snapshot for a session that is). They are
counts and seconds, so privacy `meta` does not withhold them; no file name is sent at all (the tally keeps the
names in memory, to count distinct ones, and drops them with the turn).

Rules the watch applies: a delta that changes `st` clears the summary; a delta carrying any of keys 30-34
replaces the whole summary (an absent key is unknown, not "unchanged"); a delta with none leaves it. So the
card never shows last turn's figures next to this turn's. An old watch with a new daemon never sees the keys; a
new watch with an old daemon never gets them and the card is as before.

Vectors: `session_done_summary`, `session_done_summary_partial`.

## 6. Checks

- `cargo test --workspace` in `daemon/`: `turn.rs` (test-command recognition and its refusals, exit-code
  verdicts, chains, interrupted and background runs, edits and lines, structured patches, what stays unknown,
  distinct files, reset at the next prompt, no file name in the summary); `hook_source.rs` (a whole turn through
  the registry, the stored summary in the snapshot, the next prompt clears it, a turn met mid-turn, an error
  turn, Copilot untouched, a second stop with less known); `device.rs` (0.14 gating, privacy meta keeps it).
- `daemon/tools/summary_e2e.py`: the shim fed a whole turn, a scratch daemon, a fake 0.14 and a fake 0.13 device.
- Simulator: `done_summary_model` (unit: parts, words, drop order, model clear / replace) and the scripts
  `sim_done_all`, `sim_done_failed`, `sim_done_time_only`, `sim_done_long`, `sim_done_wire` (the shared
  vectors), `sim_done_activity`. The script command is
  `done SID FILES|- ADDED/REMOVED|- pass|fail|- SECS|-` (after the session's `session SID done ...`).
- Not run: the device, a live Claude Code session (the `structuredPatch` field of `tool_response` is not
  documented; the tally uses it when present and falls back on the documented inputs).
