# Answer window and full text: more time to answer, and all of a long request

> Status: built on branch `next` (issue 20261112-a9d5). Protocol 0.11. Not yet run on the device or against a
> live Claude Code session.

Two gaps on the approval path. The hook held Claude Code's permission prompt for 15 seconds, and then the
terminal prompt appeared and the card vanished from the watch: too short for someone who has walked away, and
the watch gave no sign of how long was left. And a request whose text the daemon had cut answered "Too long to
approve here", because the watch only ever had the first 160 characters.

## 1. What the user gets

| Where | What |
|---|---|
| ASK, and the request's card on the deck | A thin ring just inside the index ring that shortens toward 12 o'clock as the time to answer on the watch runs out. Grey; white in the last 10 s. No text. |
| When the window ends | ASK shows `On your computer` with `Time ran out here` / `Answer it there` for 3 s, then the deck; the card says `On your computer` · `Answer it there` until the session moves on. Nothing vanishes silently. |
| Open on PC (button A, or the row) | The request goes back to the terminal at once: its prompt appears now, not at the end of the window. The card says `On your computer` · `Answer it there`. |
| A long request | `Read all` opens the reader, which fetches the full text and pages through it as it arrives (`3 of 12+`, then `3 of 20`). Approve can be held once the last page of the whole text has been shown. |
| Reminders | The three reminder buzzes spread over the window (60 s: 15, 30, 45 s, as before; 5 min: 75, 150, 225 s), none in its last 5 s. |

## 2. What Claude Code does (checked 2026-11-11)

From [hooks](https://code.claude.com/docs/en/hooks) and [hooks guide](https://code.claude.com/docs/en/hooks-guide):

- The broker uses `PermissionRequest`: it "runs when Claude Code is about to ask you for permission to use a
  tool", and the transcript shows "Allowed by PermissionRequest hook" *where the dialog would have appeared*.
  `PreToolUse` on `AskUserQuestion` is the other blocking hook (the answer goes in `updatedInput.answers`).
- "By default, hooks block Claude's execution until they complete." The dialog appears only after the hook
  returns without a decision. A hook that "exits 2 without a `decision` object leaves the permission flow
  unchanged"; exiting 0 with no output does the same.
- Timeout: "Defaults: 600 for `command`" (seconds), set per hook with `timeout`. A timed-out hook is cancelled
  and its output discarded, so the dialog then appears.
- "Async hooks can't block or control Claude's behavior: response fields like `decision`, `permissionDecision`
  ... have no effect." So a background hook cannot answer.
- The only place the docs describe hooks and another answerer live at once is "For a call that reaches a
  `--permission-prompt-tool` or the Agent SDK's `canUseTool` callback, the hooks run alongside your host, and
  whichever decides first applies." That is the non-interactive and SDK path, not the terminal dialog.

**So there is no supported way for the terminal dialog and the watch to be live at the same time in an
interactive session.** While the hook holds, the user at the keyboard sees no prompt. The design keeps
"silence means the terminal asks" (the watch can never make a session less safe; a timeout never approves or
denies) and makes the window a setting.

## 3. The window

- **One decision point.** `window::answer_window(&Ask)` in `daemon/crates/agentlinkd/src/window.rs` decides the
  window for each request, from the event, the agent's working directory, whether the hook is remote, the hook's
  own pinned wait and the number of linked devices. Presence detection (short at the keyboard, long when away)
  belongs here and nowhere else; this build has none.
- **Setting.** `AGENTLINK_ANSWER_WINDOW_S` in the daemon's environment, else the first number in
  `~/.agentlink/answer_window` (seconds; `#` comments allowed; read per request, so an edit applies to the next
  request without a restart). Clamped to 5-300 s; `0` means never hold (the terminal asks at once and the watch
  is not offered the request). `AGENTLINK_WAIT_MS` set in a hook's own environment still wins for that hook
  (`pinned`), as before.
- **Default: 60 s.** The arrival buzz and the three 15-second reminders (UI-V2 §4.3) all fall inside it; it is
  four times the old window, long enough to reach the watch from the next room; and a user who is at the
  keyboard waits at most a minute, and not at all if they press A (below). Field practice runs from 60 s
  (ClaudeDeck, then the normal prompt) to 120 s (agentsd, then auto-deny) to hours; a longer default should
  wait for presence detection, which can make it long only when the user is away.
- **Never longer than the hook may live.** Claude Code kills a hook at its `timeout`. The daemon reads the
  `timeout` of the `agentlink-hook` entry for that event in the user's settings (`$CLAUDE_CONFIG_DIR` or
  `~/.claude/settings.json`) and the project's (`.claude/settings.json`, `.claude/settings.local.json` under the
  agent's cwd), takes the shortest (600 s when an entry has none) and keeps the window 3 s inside it. A remote
  hook's settings cannot be read; the keepalive below covers it. `agentlink hooks` now prints `timeout: 330`
  for the two blocking hooks (it printed 30, which would cut a 60 s window to 27 s).
- **Nobody to answer.** With no device linked the window is at most 10 s (a link that is just reconnecting).
- **The hook and the daemon.** A new hook sends `"await": {"wait_ms": 15000, "window": true, "pinned": false}`.
  The daemon answers `{"window_ms": N}` at once, then a blank line every second, then the decision line. The
  hook moves its deadline to N + 2 s when it hears the window; the keepalive lets the daemon notice a hook that
  Claude Code killed (the write fails) and clear its card (`expired`) within a second.
- **Compatibility.** An older hook (no `window`) gets only the decision line, and the window is never longer
  than its own `wait_ms`, because it stops listening then. An older daemon ignores the new keys and holds a new
  hook for its `wait_ms` (15 s) as before.

| hook \ daemon | older | 0.11 |
|---|---|---|
| older | 15 s, as before | min(setting, the hook's 15 s) |
| 0.11 | 15 s, as before | the setting (60 s), told to the hook |

### The way out at the keyboard

The hook blocks the terminal prompt, so no key in Claude Code can bring it forward (the docs say nothing of
interrupting a running `PermissionRequest` hook; not tried). The watch is the way out: **Open on PC** (button A
on ASK, or the row) now sends `focus` with the request's `rid`, and a 0.11 daemon hands the request back at once
(`clear` `handed_back`): the hook returns no decision and the prompt appears immediately. It answers nothing, so
it is a press, not a hold. A watch that is off or out of range leaves the user waiting for the window; the
presence build is the fix for that.

## 4. The full text (`detail`)

- **When.** A 0.11 daemon sends `len`, the full text's length in bytes, whenever the preview is not the whole
  request: cut at 160 characters, or reshaped (`tidy` collapses line breaks and runs of spaces). A request
  without `len` from a 0.11 daemon is whole; from an older daemon the ellipsis rule of UI-V3 §4.3a still applies.
- **What.** The full text as the reader shows it (`broker::detail_text`): line breaks kept, other control
  characters in caret notation (`^[` for Esc, `^I` for a tab), every character outside the watch's glyph set
  and the invisible or look-alike ones inside it (no-break space, soft hyphen, C1 controls) spelled `\u{…}`,
  trailing spaces and line breaks dropped. Nothing else is collapsed, dropped or elided. It is the tool input
  the agent sent (the command, the path, the URL, or the input JSON): never a description a model wrote.
- **Cap: 4096 bytes.** About twenty pages of the reader (seven lines of about 30 characters). The watch keeps one
  text at a time in a 4 KiB buffer in PSRAM (`MODEL_EXT_BSS`, `detail.c`), so internal RAM grows by a few dozen
  bytes; LVGL lays out and pages a 4 KiB label without trouble; and a request longer than twenty watch pages is
  one to read on the PC. Past the cap the watch does not ask, the daemon refuses (`denied`), and Approve stays
  `Too long to approve here`; it can still be read (the preview) and denied. `DETAIL_CAP` in `broker.rs` and
  `detail.h` must match.
- **Transfer.** `cmd detail {sid, rid, arg: offset}` → `detail {rid, sid, text, off, total}` chunks of at most
  1024 bytes cut on UTF-8 boundaries, in order, then `result ok`. A 4 KiB text is four chunks (about 17 BLE
  fragments). The watch asks when ASK opens on a request that has one, so the text is usually there before the
  reader is.
- **Bound to the request.** The watch keeps a chunk only if it names the rid it asked for, its session, the
  offset it has reached and the total the request announced. A chunk for another rid or out of order is dropped;
  a different total or a NUL byte fails the transfer closed. A new request starts over. The daemon serves only an
  open request: a settled, expired or unknown rid is `stale`.
- **Link loss.** A transfer with no chunk for 3 s asks again from the offset it reached (4 tries, then failed);
  after a reconnect the daemon re-sends the open request and the transfer resumes. A request that does not come
  back within 6 s is forgotten.
- **Privacy.** Under privacy `meta` the daemon refuses `detail` (`denied`) and the watch does not ask; `len` is
  still sent (it is a number, and it keeps Approve locked).
- **The lock** (UI-V3 §4.3a). An item that grants permission can be held only once the reader has shown the last
  page of the *whole* text for this request: a text that was never cut, or a cut one whose full text has
  arrived. While it arrives, the last page reads `n of m · loading` and B stays on it. A cut text the watch can
  never have in full (an older daemon, meta, past the cap, a refused or failed transfer) can be read and denied
  but approved only on the PC.

## 5. On the watch

- **The ring.** `ui_countdown.c`: centre line r 214.5, 3 px, round caps, `COL_TEXT2` (white from 10 s left),
  centred on 12 o'clock with both ends retreating toward it. That band is free on every screen: content and the
  needs-you pips stay inside r 210, the legends' farthest corners sit at r 212, the index ring starts at r 218.
  The hold progress draws over the index ring itself, so the two never meet. It moves in whole degrees and
  invalidates only the two ends that moved. Shown on ASK and on the request's card on the deck, hidden once an
  answer is out.
- **Clock.** The daemon sends `left` (ms left when it sends the message) and `win` (the whole window); the watch
  sets its deadline from its own millisecond clock on arrival, so a reconnect (the daemon re-sends what is left
  now) or a difference between the two clocks cannot skew it. Transit only makes the watch's deadline later than
  the daemon's, never earlier, and the daemon decides.
- **When it ends.** The daemon's `clear` `expired` (or `handed_back`) marks the session `back` and removes the
  request. With the link down the watch ends the request itself 1.5 s after its deadline. Once the window is
  over a hold cannot start, a running hold is cancelled, and anything that still arrives at the daemon is
  `stale`. The `back` mark goes when a new request arrives or the session changes state, and survives a
  reconnect for a session still in the same state.
- **Words.** `On your computer` (`FONT_TITLE` on ASK, the state word on the card, `ICON_COMPUTER`),
  `Time ran out here` / `Answer it there` (`FONT_CAPTION`). No new actions, no new gestures.

## 6. Protocol 0.11

```cddl
attention.9  left: uint    ; ms left in the answer window when sent          ┐ only to devices
attention.10 win:  uint    ; the whole window in ms                          │ whose hello says
attention.11 len:  uint    ; the full text's length in bytes, when `text` is cut ┘ proto >= 0.11
detail = { ? 0 rid, ? 1 sid, 2 text, ? 3 off, ? 4 total }   ; a chunk of <= 1024 bytes
cmd_op_detail = 7          ; {sid, rid, arg: offset}: detail chunks, then a result
cmd_op_focus = 0           ; with a rid (0.11): also hand that request back to the terminal
clear_why_handed_back = 4  ; older devices get cancelled (3)
```

The daemon sends keys 9-11 and `handed_back` only to devices whose hello says minor >= 11, and answers `detail`
from older devices with `unsupported`; `focus` with a rid from an older device only focuses. A 0.11 watch on an
older daemon sees no window (no ring; the old unix `exp` still ends the card), no `len` (the ellipsis rule), and
an Open on PC that only focuses. Vectors: `attention_window`, `cmd_detail`, `detail_chunk`, `cmd_focus_handback`,
`clear_handed_back`, `clear_expired`.

## 7. Safety

The v2 invariants are unchanged: a tap never approves, denies, stops or sends; granting needs a hold that starts
on that item's own row, bound to the request id and armed 300 ms after the card appears; an expired or replaced
request can never be answered. New here: a timeout or a dead hook is always "no decision"; the hand-back answers
nothing; the reader never shows another request's text; the lock releases only on the whole text. Tests that try
to break them:

- daemon (`broker.rs`, `device.rs`, `hook_source.rs`): approve after expiry, after the hand-back and after a dead
  hook is `stale`; detail for another rid, a settled rid, a mid-character offset; an approve right behind a
  transfer; proto and privacy gating; a reconnect counts down from what is left; an older hook never held past
  its wait; the window rule and the settings timeout (`window.rs`).
- simulator: `detail_model` (unit), `sim_window_countdown`, `sim_window_expired`, `sim_window_offline_expiry`,
  `sim_window_handback`, `sim_window_old_daemon`, `sim_window_nag`, `sim_window_nag_short`,
  `sim_detail_read_approve`, `sim_detail_bound_to_rid`, `sim_detail_wrong_total`, `sim_detail_resume`,
  `sim_detail_too_long`, `sim_detail_privacy_meta`. Their ctest rules fail on any approve (`"op":3`) where none
  may leave, and on any `detail` request (`"op":7`) past the cap or under meta.
- end to end: `daemon/tools/answer_window_e2e.py` (the real hook and daemon, fake 0.11 and 0.10 devices).

## 8. Not in this version

- Presence detection (short at the keyboard, long when away) and keep-awake: later builds; the seam is
  `answer_window`.
- A way out at the keyboard without the watch.
- Full text for the deck card's two-line preview (the card shows the preview; the reader is on ASK).
- The device itself, Windows (the keepalive over the named pipe is untested), a remote hook through ssh, and a
  live Claude Code session.
