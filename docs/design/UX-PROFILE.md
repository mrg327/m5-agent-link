---
status: accepted
owner: ui
issue: 20261122-761f
last_reviewed: 2026-11-22
summary: 'The watch as the user meets it: speed, content, interruptions (2026-11-22)'
---

# UX profile: the watch as the user meets it

Issue 20261122-761f, 2026-11-22, main at 426be1f plus branch `ux-profile`. [UI-PROFILE.md](UI-PROFILE.md) audited
how v2 *looked*; this profile asks what the user *meets*: how fast the watch answers a finger, what the screens do
with real-world content, and what happens when events arrive while the user is in the middle of something.

Method, three parts:

1. **On the watch, in real time.** The bench ([BENCH.md](BENCH.md)) played 11 tasks as finger strokes on a
   StopWatch over USB, three runs each, exact and with the user model (`--human 1`), and recorded every frame,
   touch, gesture and command on the watch's clock. Tasks: approve, deny, answer, reply, switch, agent_back,
   watch, status, labs, busy (six agents, idle then a full turn of the rim), arrival (a request while browsing
   another agent's menu). Seven are new with this profile (`bench/tasks/`).
2. **Content in the simulator.** Every screen fed with edge-case data: 0 / 1 / 32 / 33 sessions, every state,
   strings at and past the model's limits, non-Latin scripts and emoji, empty fields, odd option and grant-mask
   combinations, long and one-word messages, jobs past their estimate.
3. **Event order in the simulator.** Requests, results, drops and link loss injected while the user is mid-task
   in each of the 12 modes, every back gesture from every mode (66 cases), and the glow / dark-display paths.

Every finding below was reproduced (a script plus a screenshot, log or timeline); the scripts are listed in the
issues. Severity: **A** hurts trust, safety or comprehension, or loses the user's work; **B** polish;
**C** nit.

## 1. Fixed on the way: a request turned the display off (f724)

The first watch run failed: a deny task's B press did nothing. The watch's log showed `display_off` 5 ms after
ASK opened, for **every one of the 8 requests that arrived on a lit display**. `on_attention()` stamps the
activity with a fresh clock while the link drains, `power_tick()` then runs with the step's earlier clock, and
the unsigned difference wrapped to 49 days idle. So a user looking at the watch when a request came saw the card
flash and the screen go black, and their next touch was spent on waking it. The simulator never showed it: its
link drain takes under a millisecond. Fixed with signed elapsed checks in `power.c`, with a unit test
(`simulator/tests/test_power.c`, fails before the fix).

It also exposed a bench flaw: a played stroke called `power_activity()` before `dispatch()` saw the press, so a
stroke skipped the first-touch-only-wakes rule a finger meets, which hid the bug. A stroke now wakes like a finger.

And it corrects the bench's first finding (issue 0145): the "touch read 52 ms late on ASK" was the dark display's
50 ms poll, not drawing. With the display on, the touch is read 11-13 ms after it lands, on every screen.

## 2. Speed: what the finger feels

Exact input, median of three runs (firmware b980ef1, USB):

| Task | Time ms | Touch read ms | Touch→frame ms (p50 / p95) | Frame ms (p50 / p95 / max) | Anim fps |
|---|---|---|---|---|---|
| approve | 1198 | 13 | 326 / 327 | 7.1 / 64 / 71 | 29 |
| deny | 1617 | 11 | 315 / 315 | 7.6 / 59 / 60 | 29 |
| answer | 54 | 13 | 80 / 130 | 48 / 55 / 55 | – |
| reply | 2582 | 11 / 51 | 88 / 148 | 37 / 75 / 75 | 16 |
| switch | 951 | 13 | 21 / 31 | 9.0 / 15 / 57 | 30 |
| agent_back | – | 13 | 92 / 303 | 6.1 / 9.5 / 81 | 30 |
| watch | – | 13 | 36 / 141 | 4.8 / 11 / 80 | 30 |
| status | – | 12 | 284 / 1024 | 57 / 107 / 107 | – |
| labs | – | 12 | 75 / 285 | 48 / 57 / 57 | 19 |
| busy | – | 12 | 93 / 123 | 20 / 28 / 33 | 11 |
| arrival | – | – | – | 75 / 83 / 83 | 13 |

(– for time: the task ends on a screen, not a command.) With the user model (`--human 1`: taps scattered σ 6 px,
reaction times with a 250 ms median) every task still passes; the times grow by the reactions (deny 1873 ms, reply
3331 ms median and 3861 ms worst, switch 1165 ms) and the frame figures stay the same. The 6 px scatter never
missed a target in these tasks: the discs and rows are large. What the timelines show:

| # | Sev | Finding | Evidence |
|---|---|---|---|
| P1 | B | **Every screen change is three full-screen frames of 70-110 ms** (217 k px each): opening AGENT, STATUS, LABS, a request taking over. The change takes about 250 ms at 13 fps, the slowest thing the user sees. It is `ui_motion_enter()`: the new screen's content rises 20 px over 160 ms (`UI_MOTION_MS`), which moves every pixel, so each step repaints the whole glass at ~0.35 µs a pixel and only three steps fit. A cut, or a rise of only the parts that change (the title, the row), would be one frame. | `timeline-agent_back`, `-status`, `-labs`, `-arrival` |
| P2 | B | **A swipe shows nothing while the finger moves.** Swipe down to STATUS, up to LABS, right to go back: no frame until the finger lifts, then P1. From the finger landing to the finished screen is ~450 ms. | same |
| P3 | B | **STATUS is the slowest screen**: its frames take 100-107 ms, twice any other full redraw, and a rim step lands ~300 ms after the finger. | `timeline-status` |
| P4 | B | **A hold shows nothing for ~320 ms.** By design (`NAV_HOLD_SHOW_MS` 250 ms, so a tap never flashes the arc), but the disc gives no press state either, so for a third of a second the user cannot tell the touch registered. A pressed-state change on the disc at touch-down, with the arc still at 250 ms, would answer at once without flashing an approval. | `timeline-approve`, `-deny` |
| P5 | B | The agent and reply menus draw at 16 fps (median frame 37 ms against the 33 ms period), as in 0145. | `timeline-reply` |
| P6 | C | Six working agents on HOME redraw ~8 times a second while nobody touches the watch (20 ms, 8 k px frames every 93-124 ms, uneven). Cheap per frame, but it is the steady-state cost on the wrist. | `timeline-busy` |

## 3. Interruptions: the user's work is not safe

UI-V3 §1 promises "Nothing is lost". These break it.

| # | Sev | Finding |
|---|---|---|
| E1 | A | **A request (or an error) on another session tears down whatever is open.** In KEYS the typed text is erased (`kb_end` discard), even mid hold-to-send; in VOICE the recording or ready transcript is discarded; READ, GRAPH, STATUS, LABS and TRACKPAD are closed. After answering, the user lands on HOME and the work is gone. Only "already in ASK" is exempt (`app.c` `on_attention`). A composition should not be interrupted: glow, buzz and badge instead, or at least keep the draft. |
| E2 | A | **A late failure reads as success.** Approve, see Approved, move on; a `result err` (stale, denied, timeout) then shows a 1.8 s glyph over *another* session's card, and the request silently becomes answerable again without being brought back (`on_result`). |
| E3 | A | **A request that arrives on a dark display is easy to miss.** It glows for 2 s, then the display goes dark; the next touch wakes HOME on the previously selected session, not the request. After a buzz, the wake should show what buzzed. |
| E4 | A | **A finger already down when ASK takes over carries into it**: its 600 ms long press is ASK's "back", so the request closes half a second after it appears, unseen. A mode change under a finger should swallow that contact, as a wake does. |
| E5 | B | After answering, a second pending request is neither opened nor selected; the only cue is a dot on the rim. |
| E6 | B | ASK shows no link state: offline, the user goes through the full 1.2 s hold, then gets a reject buzz and "Offline". KEYS offline gives only the buzz and clears the composition before the result. |
| E7 | B | The open session ends: WATCH says "Session ended", but AGENT, READ and KEYS drop to HOME silently (KEYS also erases). VOICE on a question answered elsewhere or expired closes with no word and loses the recording. |
| E8 | B | The same session's next request replaces "Approved" after ~0.55 s with an identical layout; a reassuring second hold approves the new one. Safe by the rules (bound to the rid, armed), but easy to misread; the new card needs to look new. |
| E9 | C | AGENT's selection jumps to the first row when the session changes state (Snooze becomes Activity under the thumb). |

## 4. Navigation: where back goes

| # | Sev | Finding |
|---|---|---|
| N1 | B | Back from READ, VOICE, KEYS, GRAPH or WATCH opened from AGENT lands on HOME, not AGENT; SCROLL→WATCH, TRACKPAD→LABS and ASK→WATCH do return. After ASK takes over, only WATCH and SCROLL are resumed; READ loses its page. |
| N2 | B | KEYS: B held and A+B erase unsent text at once, without a confirm; swipe right and long press, back everywhere else, do nothing. VOICE: a stray swipe right discards the recording. |
| N3 | C | A double (next request) works only on HOME, and lands on the card, not ASK; elsewhere it is swallowed with the first A. |
| N4 | C | A double tap on an AGENT row commits it, returns HOME, and the second tap reopens AGENT. |
| N5 | C | A long press near the top of STATUS / LABS does not go back, as it does in AGENT, READ and WATCH. |

No mode traps the user: every one leaves by B held and by A+B.

## 5. Content: what the screens do with real data

| # | Sev | Finding |
|---|---|---|
| C1 | A | **Anything outside Latin-1 draws as empty boxes**: Polish, Czech, Turkish letters, Greek, Cyrillic, CJK, Hangul, Hebrew, Arabic, emoji (a `git commit -m "✨ …"` included). On ASK the user is asked to hold-approve a command they cannot read. The fonts cover 0x20-0x7E and 0xA0-0xFF; the daemon replaces undrawable characters only in the last message (`lastmsg::drawable`), not in session, request, question or step text. |
| C2 | A | **Clipped text loses its ellipsis and can end in half a character.** The daemon clips by characters and appends "…" (3 bytes); the device's buffers are bytes and cut again without `utf8_drop_partial`. "Yes, don't ask again fo…" shows as "Yes, don't ask again fo": a cut grant option reads as a whole phrase. Stray lead bytes reach the model, the UI state and the debug log. The AGENT header readout clips on both sides: "PR #123456" shows as "12". |
| C3 | B | The 33rd session is dropped (pool full) and **its request never shows**: no card, no ring segment, A double skips it. Rare, but it is a pending permission nobody can see. |
| C4 | B | ASK's glyph follows the grant mask, whatever the label says: mask 0 shows "Allow" as a vermillion ✗, mask 0xFF shows "No" as a ✓ hold-to-grant; "don't ask again" has the same ✓ as a one-time yes. One grant option plus approve/deny gives no way to deny on the watch. A permission with no title, text or tool shows only the discs, approve at full emphasis. |
| C5 | B | Long commands wrap at the first space: "cd" alone on the first row, then a path, and the part that matters (`rm -rf …`) never shows. |
| C6 | B | Sessions with no folder share one sigil, so they cannot be told apart. |
| C7 | B | A job far past its estimate shows a full ring, like one about to finish. |
| C8 | C | Start looks like working; a failed job with exit 0 shows "0"; a 0/0 diff shows an empty pill; a one-word message still offers Read; the preview is often a markdown heading ("Summary"); reader pages split code blocks early. |

Fine as they are: long ASCII strings end with dots everywhere; 0 and 32 sessions; every state's glyph; jobs; done
figures up to 9999 files and 111 h; 4×4 questions with long labels; the reader with long text, URLs and lists;
battery at 0 % and charging; quiet mode; the hold bound to its request through clears, replacements and other
sessions' requests; deny never reachable by a swipe; the first touch on a dark display never approves.

## 6. Bench and simulator gaps found on the way

- The bench player cannot serve content the watch fetches (the last message, the graph, terminal scrollback), drop
  a session, or answer a command with an error, so READ, GRAPH, SCROLL and the failure paths are measured only in
  the simulator.
- The simulator's `rx` line is cut at ~255 bytes (`sim_script.c` `arg[512]`), so long text cannot go through the
  real decode path in a script.
- The simulator cannot show anything that depends on the watch's slowness (§1 is the example); the "paced
  simulator" in BENCH.md §8 would.

## 7. Issues

f724 (fixed on this branch). A findings: 883f (E1), 4b25 (E2), 5f79 (E3), 09b8 (E4), 6b60 (C1), 9689 (C2). Grouped: 9df4 (E5-E9, N1-N5), 67d1 (C3-C5), 4094 (C6-C8), d379 (§6), and 0145 for P1-P5. Repro scripts: `simulator/scripts/ux/events` and `simulator/scripts/ux/content` (run from the repo root with `build/sim/agentlink_sim --no-ws --headless --script FILE`; `events/e22*` with `--idle-off 3`).
