# Presence and keep-awake: buzz when you are away, stay awake while work runs

> Status: built on branch `next` (issue 20261112-fe75). Protocol 0.12. Not yet run on the device or against a
> live Claude Code session. Linux/X11 and KDE are exercised on a dev machine; macOS code is written from the
> documented tool output and untested.

Two gaps from the market research (gap rows 5 and 7). The watch buzzed the same whether you were typing in that
very terminal or in another room, and the permission hook held the terminal's own prompt for the whole
[answer window](ANSWER-WINDOW.md) even with you sitting in front of it. And a computer that goes to sleep
while an agent or job runs is the silent failure of stepping away.

## 1. What the user gets

| You are... | The permission hook holds the terminal prompt | The watch buzzes for |
|---|---|---|
| at the keyboard, that session's terminal focused | 2 s (the card still appears; the terminal asks right after) | nothing: no buzz, glow or reminders for done / error / job finished, approvals and questions |
| at the keyboard, in another window | 10 s | everything, as before |
| away, screen locked, or the daemon cannot tell | the full window (60 s by default) | everything, as before |

The cards are always on the watch and can be answered as ever. `agentlink status` shows what the daemon
believes:

```
presence: present (via freedesktop-screensaver, idle 3s of 120s)
  #2 terminal focused: yes (via x11)
keep awake: held (setting ac, work running, on AC power, via systemd-inhibit)
```

## 2. The table

`presence::cell(event, here)` in `daemon/crates/agentlinkd/src/presence.rs` is the only place these are
decided; every cell has a test. `here` is `Focused` (present and the session's terminal is the focused window),
`AtKeyboard` (present, anything else, including "cannot tell which window") or `Away` (away, locked, unknown,
or presence switched off).

| Event | Focused | AtKeyboard | Away |
|---|---|---|---|
| **Answer window** (`window::answer_window`) | `Short(2 s)` | `Short(10 s)` | `Full` |
| **Done / error / job finished** | `Hush` | `Buzz` | `Buzz` |
| **Needs approval / has a question** (card always shown) | `Hush` | `Buzz` | `Buzz` |

`Short(d)` is `min(d, default window)`; `Full` is the configured window (default 60 s). `Hush` means the
daemon tells the watch not to buzz, glow or remind for that one event; `Buzz` means it says nothing and the
watch behaves as it always has.

Around the table:

- **Presence never changes what can be approved, never answers anything and never hides a card.** It acts on
  two things: how long the hook holds the terminal prompt, and one flag that suppresses a buzz. The Focused
  window is 2 s, not 0, so the card is still offered (a window of 0 means the watch is not offered the request
  at all, [ANSWER-WINDOW.md](ANSWER-WINDOW.md) section 3). It expires on its own: `clear expired`, the terminal
  asks.
- **An explicit window setting wins.** `~/.agentlink/answer_window` / `AGENTLINK_ANSWER_WINDOW_S`, or a hook's
  own `AGENTLINK_WAIT_MS`, is never shortened by presence (a `0` still means never hold). The default (no
  setting) is what presence shortens. Hook-timeout and no-device caps still apply to a short window.
- **Quiet / mute (alert mode) / snooze / per-session Notify keep their meaning.** Hush only ever removes a
  buzz, and the watch applies it on top of its own rules, so the stricter wins. One deliberate exception on
  the daemon side: a session armed with Notify (the user asked to be told when it finishes) is never hushed,
  because the delta that enters done clears `ntf` and the daemon sees it. This is the owner's call; the other
  reading is that presence outranks Notify.
- **Unknown behaves exactly as before.** No source, a source that errors, a source that times out: `Away`.
  Wayland without kitty remote control, macOS focus, Windows: the user may be known present but the focus is
  unknown, which is `AtKeyboard` (10 s window, buzz as usual).
- **Privacy.** Window titles and process names are never read for the rules or the trace. The focused window's
  X11 id and pid are compared with the ids the hook captured and dropped. The trace records
  `{here, presence, source, focus, hush}` on the request's `window` / `notice` entry, nothing else.

## 3. Presence sources

Read at most once every 3 s (a decision asks for the cached reading), only when a permission request or a
finish needs it, and `agentlink status`. No timers, no polling when nothing happens. Each probe is a small
process run without a shell under a 3 s timeout (`focus::run`); no new dependencies.

**Is the user at the keyboard** (`State`: present, away, locked, unknown), first source that answers:

| OS | Locked | Idle time |
|---|---|---|
| Linux | `busctl --user call org.freedesktop.ScreenSaver ... GetActive` (KDE, freedesktop), `org.gnome.ScreenSaver`, then logind `LockedHint` | `org.freedesktop.ScreenSaver.GetSessionIdleTime` (ms; KDE), GNOME's `org.gnome.Mutter.IdleMonitor.GetIdletime`; logind `IdleHint=yes` counts as away, `no` proves nothing (a desktop that never sets it leaves it at `no`) |
| macOS | `ioreg -n Root -d1 -l -w0`, `CGSSessionScreenIsLocked` | `ioreg -c IOHIDSystem`, `HIDIdleTime` (ns) |
| elsewhere | unknown | unknown |

Idle at most 120 s (setting) is present; more is away. Locked wins over idle.

**Is that session's terminal the focused window** (`Focus`: yes, no, unknown):

1. kitty with remote control: `kitty @ ls --match` for the session's window: the OS window has the system's
   focus, the tab is active and the window is the focused one (works on Wayland too).
2. X11 (`DISPLAY` set, not a Wayland session): `xdotool getactivewindow` is the hook's `WINDOWID`, or its pid
   is `KITTY_PID` or one of the pids in the hook's process chain.
3. Otherwise unknown. A tmux pane's own focus is not checked (the outer terminal is).

This machine (KDE Plasma, X11, kitty, systemd): `GetActive` and `GetSessionIdleTime` on the freedesktop
screen saver, logind `LockedHint` and `xdotool` all answer; the screen saver one decides. Verified live:
locked is reported as locked; the idle time counts in milliseconds; the X11 focus path says yes for the
focused terminal's window id and for its pid, and no for another pid. The present-at-the-keyboard branch could
not be exercised for real, because the screen was locked while this was built: it is tested with an injected
source.

## 4. Settings and the test source

| Setting | Where | Values |
|---|---|---|
| Presence rules | `~/.agentlink/presence` or `AGENTLINK_PRESENCE` | `on` (default) / `off`, optionally followed by the idle limit in seconds (`on 300`, 10-3600, default 120). Read on every question. Off = everything as before presence existed |
| Keep awake | `~/.agentlink/keep_awake` or `AGENTLINK_KEEP_AWAKE` | `ac` (default), `always`, `off` |
| Answer window | as in ANSWER-WINDOW.md | an explicit value is never shortened |

**Test only:** `AGENTLINK_PRESENCE_FILE` names a file the daemon reads instead of asking the system, every
time, e.g. `state=present focus=yes power=battery` (`state` present|away|locked|unknown, `focus` yes|no|unknown,
`power` ac|battery). `daemon/tools/presence_e2e.py` drives it. Do not set it outside tests.

## 5. Keep the computer awake

While any agent session is **working or waiting on the user** (states `work` and `input`; a running job is
`work`), the daemon holds an idle-sleep inhibitor, subject to the setting:

| Setting | Holds it when work runs... |
|---|---|
| `off` | never |
| `ac` (default) | on AC power. A machine with no battery counts as on AC; an unknown power source counts as AC |
| `always` | on battery too |

- **Linux:** `systemd-inhibit --what=idle --mode=block --who=agentlink` around a small shell loop that runs
  while the daemon's pid exists. `idle` only: it does not block lid-close suspend, the power key or screen
  locking (those are the `sleep` and `handle-*` locks, which are not taken). **macOS:** `caffeinate -i -w
  <daemon pid>` (idle sleep only; untested). **Elsewhere:** reported `unsupported`.
- **Released** when nothing runs (re-checked on every registry update and every 5 s), when the setting turns
  off or the machine goes on battery under `ac`, and when the daemon exits (Ctrl-C, SIGTERM, which the daemon
  now handles). The child is its own process group and is ended as a group. A daemon that is killed outright
  (SIGKILL, power loss) loses the lock within 2 s: the holder notices the pid is gone.
- A session stuck in `work` for more than 12 hours stops counting (an agent that died without a last hook must
  not hold the machine awake for ever).
- **Status:** the `keep awake:` line of `agentlink status` (also `keep_awake` in `status --json`):
  `idle`, `held`, `failed` (with the reason; retried every 5 s) or `unsupported`.

Limit, unverified: logind's `idle` lock stops logind's own idle action. A desktop that suspends on its own
timers (KDE's PowerDevil, GNOME's settings daemon) may not honour it. `kde-inhibit --power` was tried here and
registered nothing with PowerDevil, so it was not used. If the owner finds the machine still sleeps, the next
step is the desktop's own `org.freedesktop.PowerManagement.Inhibit` over a persistent D-Bus connection.

## 6. Protocol 0.12

```cddl
session.26   hush: bool   ; only in the delta that enters done / error; never stored
attention.12 hush: bool   ; no buzz, glow or reminders for this request
```

Sent only to devices whose hello says minor >= 12, and only when true (the daemon never sends `false`; absent
means "buzz as your own rules say"). An old watch with a new daemon sees neither key and buzzes as before; a new
watch with an old daemon never gets one. The watch holds the arrival buzz, the glow and the reminders for a
request with `hush` (event `ui hush`, like `quiet` and `snoozed`), and the finished buzz for a delta with
`hush`; everything else (card, ASK, rows, holds, the answer window ring) is unchanged. No new screens, rows or
actions. Vectors `attention_hush`, `session_hush`; Atlas decoder and `facts.json` updated.

## 7. Safety

The v2 invariants are unchanged and presence cannot touch them: it never produces a decision, only a shorter
hold of the terminal prompt (which falls back to the terminal asking, always the safe side) and a flag that
removes a buzz. A wrong "focused" costs a missed buzz for a card that is still on the watch; a wrong "away"
costs a buzz. Tests: the table (every cell), the window decision (explicit setting wins), the hush gating
(finished, Notify armed, proto < 0.12), presence-source parsing, the inhibitor life cycle with a fake
inhibitor (take, hold, release on battery, setting off, work done, drop, refusal, a child that died, no tool),
the real child process group; simulator `sim_presence_hush_ask`, `_nag`, `_done` (no `op` 3/4 may leave, no
glow, the unhushed request still buzzes); end to end `daemon/tools/presence_e2e.py`.

## 8. Not in this version

- Focus on Wayland without kitty, on macOS and on Windows, and a tmux pane's own focus.
- Windows presence and keep-awake (unsupported).
- Telling the daemon when the watch's own Quiet is on, or the watch being on the wrist (the watch's motion
  sensor could say "not worn" and widen the window).
- A desktop-native idle inhibitor (see the limit in section 5).
