# Gauges: plan usage and context on the watch

> Status: built on branch `next` (issue 20261112-2777). Protocol 0.10. Not yet run on the device.

The watch knew what each agent was doing but nothing about what it costs. Two numbers decide whether a
long run will finish: how much of the plan's rate limit is left, and how full the session's context window
is. This adds both, plus three readouts that ride on the same data: the model, the permission mode and the
pull request. All of it is readout only: nothing in this build switches a mode or a model.

## 1. What the user gets

| Where | What |
|---|---|
| STATUS | Two thin arcs inside the bezel: outer = the 5-hour window, inner = the week. Under a smaller clock, two rows: `5h 42%  resets in 2h 10m`, `Week 18%  resets in 3d 5h`. On pace to run out early: `5h 83%  runs out 14:45` in amber. At the limit: `5h 100%  limit until 16:00`. |
| Empty and ambient faces | One figure under the caption: `5h 42%` (amber from 80 %). |
| Session card | A 160 × 4 capsule under the state row: the context window's fill. Grey below 70 %, amber from 70 %, vermillion from 90 %. |
| AGENT menu | One line under the caption: `Opus 4.7 · Plan · PR 128 approved`. |
| Wrist | One 200 ms buzz when the 5-hour window first crosses 80 % in a window. |

Nothing to see is shown as nothing. A figure the watch does not have, or no longer trusts, is never a zero:
the faces and the card draw nothing, STATUS draws `5h –` over an empty track.

## 2. Rules

- **Source.** Claude Code's status line ([docs](https://code.claude.com/docs/en/statusline), checked
  2026-11-11). Field names as documented:
  `rate_limits.five_hour.used_percentage` / `.resets_at` (unix seconds), the same under `seven_day`;
  `context_window.used_percentage`; `model.display_name` (`model.id` as fallback); `pr.number`,
  `pr.review_state` (`approved`, `pending`, `changes_requested`, `draft`). The permission mode is not in the
  status line: it is `permission_mode` in the hook payloads ([hooks](https://code.claude.com/docs/en/hooks):
  `default`, `plan`, `acceptEdits`, `auto`, `dontAsk`, `bypassPermissions`).
- **Who has rate limits.** Only Pro, Max and Ultra subscribers, and only after a session's first response.
  Each window may be absent. So a session without figures never clears another session's: the limits are
  the account's. Only this machine's sessions feed them (a remote host may be another account).
- **Staleness.** The daemon stamps each reading (`usage.at`). The watch shows a window only while the clock is
  set, the window has not reset (`now < resets_at`) and the reading is less than an hour old
  (`GAUGES_STALE_S`). The daemon re-sends unchanged figures every 5 minutes while readings arrive, so an
  active user never sees the dash; an idle one sees it after an hour.
- **Pace.** "Runs out at" is the daemon's: the burn rate over the last 10-60 minutes of readings (the oldest
  reading in that span against the latest); with no such reading yet, the average since the window began
  (`resets_at - 5 h`), once the window is 15 minutes old. If 100 % comes before the reset at that rate, the
  time (to the minute) is sent as `h5o`; otherwise nothing. A projection that moves by less than 5 minutes is
  not re-sent. The weekly window works the same way over 1-24 hours, after 6 hours.
- **The buzz.** On a reading at or above 80 % that follows one below 80 % in the same window (`resets_at`
  within 10 minutes). Once per window. A watch that connects with the figure already over 80 % did not see it
  cross and does not buzz. Quiet mode swallows it (and it does not come back later); the alert mode decides as
  for every buzz (Silent: none; Sounds off: the haptic still plays, it never had a sound).
- **Context.** `used_percentage` rounded down. `null` (early in a session, after `/compact`) clears the
  capsule (`ctx: 255` on the wire); a session that never had a figure gets nothing sent.
- **Model.** The display name without a leading `Claude ` and without a bracketed note (`Opus 4.7 (1M context)`
  → `Opus 4.7`), at most 16 characters.
- **Mode.** Shown only when it is not the default (status by exception): `Plan`, `Accept edits`, `Auto`,
  `Don't ask`, `Bypass`.
- **Pull request.** `PR 128` plus the review state (`in review`, `approved`, `changes`, `draft`). A GitLab
  merge request arrives the same way (`pr.kind` = `mr`) and is shown as a PR. The status line carries no
  check (CI) state, so there is none on the watch.
- **Privacy.** Usage figures, context fill, model and mode are metadata and go to every 0.10 device. The pull
  request number says what a session works on: it is withheld under privacy `meta`, like the summary. Paths,
  costs and the transcript path never leave the shim (§4).

## 3. Protocol 0.10

```cddl
mt_usage = 23
usage = {
  0 => uint,      ; at    when the daemon last read these figures (unix)
  ? 1 => uint,    ; h5    5-hour window used, percent 0-100 (rounded down)
  ? 2 => uint,    ; h5r   5-hour window resets at (unix)
  ? 3 => uint,    ; h5o   runs out at the current pace (unix); absent = not before it resets
  ? 4 => uint,    ; wk    the week, percent
  ? 5 => uint,    ; wkr
  ? 6 => uint     ; wko
}
session.21 ctx: uint     ; context window used, percent; 255 = unknown
session.22 mdl: tstr     ; model short name, <= 16 chars
session.23 pm: perm_mode ; default 0, plan 1, accept_edits 2, auto 3, dont_ask 4, bypass 5
session.24 pr: uint      ; open pull request number; 0 = none (withheld under privacy meta)
session.25 prs: pr_state ; none 0, pending 1, approved 2, changes 3, draft 4
```

`usage` is a full replacement: an absent window is unknown. The daemon sends it after the snapshot when a
device connects (unless the last reading is stale), whenever the figures change, and again after a lag.
`usage` and keys 21-25 go only to devices whose `hello` says proto ≥ 0.10. Vectors: `usage_both`,
`usage_none`, `session_gauges`, `session_gauges_cleared`.

## 4. The status-line entry point

```
agentlink-hook statusline [-- <your own status-line command>]
```

Claude Code runs the status line after each assistant message (debounced 300 ms), on a permission-mode change
and when a rate-limit window resets. The shim reads the JSON on stdin, keeps only the fields of §2 plus
`session_id` and `cwd`, writes one line (`event: "statusline"`) to the hook socket and returns: no process
walk, a 250 ms cap on the socket write, nothing sent and nothing printed without a daemon. After `--` it runs
the given command through the shell with the same stdin and passes its stdout, stderr and exit status
through, so installing it keeps whatever status line the user had.

The daemon uses a status line only for a session the hooks already created (a status line alone never makes
a card) and never lets it answer or cancel a request.

`agentlink hooks statusline` prints the setting. It reads (never writes) `~/.claude/settings.json` and, when a
`statusLine.command` is already there, chains it:

```json
{ "statusLine": { "type": "command", "command": "agentlink-hook statusline -- '~/.claude/statusline.sh'" } }
```

`--chain '<command>'` names the command instead. Nothing is installed: merging the block is the user's step.

## 5. On the watch

- **STATUS** (UI-V3 §4.6b). Arcs of 240° through 12 o'clock, centre lines r 192 and 182, 4 px, round caps,
  `COL_FILL2` track. Fill white below 80 %, amber from 80 %, vermillion from 95 %: chrome, not a state colour,
  so the arcs never read as session segments of the ring. The ends sit at y 329, above the legends; r 192 + 2
  stays clear of the ring's pips (r 200-210). With a usage reading the clock is `FONT_TITLE` at y 84, the
  chips at y 120, the rows at y 152 and 180 (`FONT_BODY_STRONG` figure, `FONT_BODY` detail). Without any
  reading (not a subscriber, an older daemon) STATUS is exactly as before.
- **Faces.** `5h 42%` in `FONT_BODY` at y 304 on the empty deck (`COL_TEXT2`) and the ambient face (45 %
  white), amber from 80 %. It drifts with the face.
- **Card.** The capsule takes the job bar's row (y 219; a job has no context, so they never meet). It is
  hidden while the card shows a request (which starts at y 220), for a merged slot and for a job.
- **AGENT.** The readout in `FONT_BODY` `COL_TEXT2` at y 108, between the caption and the first row; not in
  the Reply list (its question owns the top) and not for a job.
- Nothing here is touchable: no new actions, no new gestures. Every v2 safety invariant is untouched.

## 6. Not in this version

- Switching the mode or the model from the watch.
- The PR's check (CI) state: not in the status-line data.
- The spend limit (`rate_limits.spend_limit`, behind a Claude apps gateway).
- Copilot CLI usage: it has no status-line equivalent.

## 7. Checks

- `cargo test --workspace` in `daemon/`: parsing of the documented payload, absent and null values, the
  short model name, review states, publish policy, reset windows, both pace methods, a new window
  (`usage.rs`); status lines only for known sessions, rate limits only from this machine, permission mode
  from the hooks (`hook_source.rs`); proto and privacy gating, usage on connect (`device.rs`); the shim's
  trimming and chaining (`agentlink-hook`); the printed setting and its quoting (`agentlink`).
- `daemon/tools/gauges_e2e.py`: the shim fed a status-line JSON, a scratch daemon, a fake 0.10 device and a
  fake 0.9 device.
- Simulator: `gauges_model` (unit), `sim_gauges_status`, `sim_gauges_card`, `sim_gauges_face`,
  `sim_gauges_buzz`, `sim_gauges_wire`.
- Not yet run anywhere: the device itself, Windows (the chain uses `cmd /C` there), and a real Claude Code
  subscription session end to end.
