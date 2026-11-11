# Jobs: terminal commands on the watch

> Status: built on branch `jobs` (issue 20261111-2e5f). Protocol 0.9.

The watch knew about agents only, because only agents have hooks. A build, a test run or a deploy that
the user starts by hand was invisible. A **job** is any command typed into a terminal that is still
running after ten seconds. It gets a card like an agent does: what runs, where, for how long, about how
much longer, and how it ended.

## 1. What the user gets

| Moment | On the watch |
|---|---|
| A command passes 10 s | A new segment on the ring. Card: `Terminal · <branch>`, the folder, `Running` with the elapsed time, the command line. |
| The same command has run before in that folder | A thin progress bar and `about 2m left` instead of the elapsed time (median of the last successful runs). |
| It exits 0 | One buzz. `Finished · in 3m 12s`. |
| It exits non-zero | The error buzz. `Failed · exit 101 · 42s`. |
| Any time | Button A brings the terminal forward (and dismisses a finished job). |
| While it runs | `Stop` (hold): Ctrl-C for that terminal. |
| After it ended | `Run again` (hold) types the same command into the same terminal; `Dismiss` removes the card. |

Nothing to configure beyond one line in the shell's rc file:

```sh
eval "$(agentlink shell-init zsh)"     # or bash
```

## 2. Rules

- **Threshold.** A job is shown once it has run `AGENTLINK_JOB_MIN_S` seconds (default 10). Shorter
  commands never reach a device.
- **One job per shell.** The session key is the shell (`shell:<pid>`). A new long command in the same
  shell takes the card over (same `sid`, `t0` reset); the card is removed when it is dismissed (`ack`),
  10 minutes after the job ended, or when the shell exits.
- **Never shown:** interactive programs (editors, pagers, `ssh`, `tmux`, REPLs started without
  arguments, `fg`, …; built-in list plus `~/.agentlink/jobs-ignore.txt`, one program name per line), and
  any command in a terminal that already hosts an agent session (the agent's own card covers it).
- **Interrupted by the user** (exit 130, SIGINT): the card is removed without a buzz. The user is at
  the keyboard, or pressed Stop and knows.
- **Estimate.** `eta` is the median run time of up to the last 5 successful runs of the same command
  line in the same directory, and only when there are at least 2. History lives in
  `~/.agentlink/job-history.json`, keyed by a hash: no command text is stored.
- **Privacy.** The command line travels in `sum` (already withheld under privacy `meta`) and the
  program name in `tool`. The trace and the audit log never hold the command line.

## 3. Protocol 0.9

```cddl
agent_shell = 3            ; session.ag for a job
session.19 eta: uint       ; expected total run time, seconds; 0 = unknown
session.20 exit: uint      ; exit status, sent with the delta that enters done / error
cmd_op_rerun = 17          ; run a finished job's command again (hold_ms >= 1000)
```

A job uses the session states it already has: `work` while it runs, `done` for exit 0, `error` otherwise.
`t0` is the command's start, `tst` its end, so the run time is `tst - t0`. `tool` is the program
(`cargo`), `sum` the command line (≤ 80 chars), `repo` and `br` as for agents.

The daemon sends shell sessions, `eta` and `exit` only to devices whose `hello` says proto ≥ 0.9: an
older decoder rejects the unknown agent value.

Commands on a shell session:

| Command | Effect | Refused |
|---|---|---|
| `focus` | The terminal comes forward (the usual focus drivers). | |
| `cancel` (hold ≥ 1000 ms) | SIGINT to the foreground process group of the shell's tty. | `stale` when nothing runs; `unsupported` on Windows |
| `rerun` (hold ≥ 1000 ms) | Types the job's command line and Enter into its terminal. | `denied` while that shell runs anything; `stale` when the job is unknown; `unsupported` without a typing driver (kitty today) |
| `ack` | Removes the card (`gone`). | |

## 4. Shell integration

`agentlink shell-init zsh|bash` prints a snippet that calls the hook shim in the background, so a
command is never delayed:

```
agentlink-hook shell start --pid $$ -- "<command line>"     # preexec
agentlink-hook shell end   --pid $$ --exit <status>          # precmd
agentlink-hook shell exit  --pid $$                          # the shell exits
```

The shim adds the working directory and the terminal identity it already collects for agents, and
writes one line to the hook socket. With no daemon it fails silently in a few milliseconds.

## 5. On the watch (UI-V3 §4.1b)

A job is a session like any other: same ring, same card, same gestures. Differences:

- Caption `Terminal` (plus the branch); state words `Running`, `Finished`, `Failed`.
- The time beside the state word is the elapsed run time while it runs, `about 2m left` when `eta` is
  known and not yet passed, `in 3m 12s` when it finished and `exit 101 · 42s` when it failed.
- With an `eta`, a capsule progress bar (160 × 4, the state colour over `COL_FILL2`) sits under the
  state row. Past the estimate it stays full and the elapsed time returns.
- The summary is the command line (the program name under privacy `meta`).
- A finished job always buzzes (unless snoozed or quiet): that is what it is for.
- Menu: `Stop` (hold) while it runs; `Run again` (hold) and `Dismiss` after. No Activity, Notify or
  Snooze rows.

## 6. Not in this version

- The last line of output on the finished card (needs a terminal reader that can tell output from the
  prompt).
- Jobs on remote hosts (`agentlink ssh`).
- fish and PowerShell snippets.
