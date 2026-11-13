---
id: 20261110-1e57
title: KEYS: swipe keyboard on the watch, decoded on the PC
status: fixed
severity: med
area: firmware
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-10T18:30:58Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on: macos-arm64
gh:
---

## Summary

# KEYS: swipe keyboard on the watch, decoded on the PC

## Goal
Fine-grained text input when voice isn't possible. The watch is an input surface only; the text being
composed appears in the agent's terminal on the PC (kitty), word by word. Nothing reaches the agent until
a hold-to-send, the same rule as Dictate.

## Principles (kept from voice/reply)
- The watch sends geometry and indices only (stroke points, candidate index). The daemon owns all text.
- Typing is refused in the same cases as `type`/`dictate`: an open request, a waiting approval or question,
  or a numbered selection list on screen. Kitty only at first (the `type` op is already kitty-only).
- Strokes, candidates and composed text are `<private>` in the trace and are never written to the audit log.

## UX (UI-V2 §4.8 KEYS)
- Entry: "Type..." in the Reply list, after "Dictate..." (shown when `welcome.feats` has "keys").
- Screen: round QWERTY inside r≈180 (rows 10/9/7, the middle row widest, with the keys made as large as
  the circle allows). The outer ring (r 180-233) is reserved for candidate arcs. Letters drawn as labels,
  the stroke trail drawn as an LVGL line and faded on lift.
- One stroke = one word (SHARK2 decode on the PC). A tap = one letter (taps accumulate into a word,
  also decoded with near-key correction). A space comes automatically after each committed word.
- The daemon types the top candidate into the terminal right away. 3-4 alternatives show as rim arcs;
  tapping one replaces the last word (the daemon sends backspaces and the new word).
- Buttons: A tap = space or next word / accept, A held 1.2 s = send (Enter); B = delete the last word;
  B held = leave (asks the daemon to erase the uncommitted composition, or keeps it: decide in the spike);
  A+B = home.
- `?123` key toggles a symbols/digits layer (tap only) for paths, flags and identifiers.

## Protocol 0.7 (PROTOCOL.md §7, CDDL + vectors)
```
kb_start  { kid, sid, layout: { w, h, keys: [[cp, x, y, w, h], ...] } }   device → daemon
kb_stroke { kid, seq, pts: bstr }   int16 x, y, uint16 dt per point; chunked ≤ 240 B like audio; last chunk flagged
kb_cands  { kid, seq, cands: [tstr ≤ 4], typed: uint }   daemon → device (typed = index already in the terminal)
cmd ops: kb_pick(arg = idx) · kb_del · kb_send(hold_ms ≥ 1000) · kb_end(arg 0 keep / 1 erase)
welcome.feats += "keys"; device advertises feature_keys
```
The layout comes from the device, so the decoder doesn't depend on the layout and the firmware can
change it without a daemon release.

## Decoder (daemon, pure Rust, MIT/Apache, no GPL)
- `keys/layout.rs`: key geometry from kb_start, plus ideal word paths (key-centre polyline).
- `keys/lexicon.rs`: base English frequency list (permissively licensed, about 20k words, embedded) plus
  a **context vocabulary** rebuilt on kb_start: the agent's last question (`q`), the screen text from
  kitty `get-text --extent screen`, `git ls-files` basenames for the session's repo, and words the user
  sent recently. Identifiers are split and also kept whole (`check_dram.py`, `--output-on-failure`,
  `snake_case`). The context vocabulary gets a strong prior boost.
- `keys/decode.rs`: SHARK2: resample to 64 points, prune by start/end key and path length, then score
  the location and shape channels (Gaussian) × word prior. Taps use per-letter nearest keys with a
  Gaussian spatial model plus the same prior. Target under 20 ms per stroke for 30k words.
- `keys/predict.rs`: next-word bigram from the context text. Optional LLM rerank over an
  OpenAI-compatible URL (`AGENTLINK_KB_LLM_URL`, off by default); it never blocks the top-1 path.
- Eval harness (`cargo test` plus `agentlink kb-eval`): synthetic noisy strokes per word from the real
  layout geometry (jitter, corner cutting, speed), reporting top-1 and top-4 accuracy. Gate: top-4 ≥ 95%
  and top-1 ≥ 80% on the 2k most common words.

## Firmware
- `ui_keys.c` (new MODE_KEYS), `keys.c` (stroke recorder: samples raw points from `touch_read_cb`
  while in KEYS, downsamples, packs, sends; ≤ 2.5 s / 160 points per stroke).
- nav.c: the KEYS table maps only buttons and the rim-candidate tap; touch gestures are not turned into
  scrub or swipe while in KEYS (the recorder owns the touch).
- Touch: the LVGL indev reads at 33 ms (about 30 Hz), which is enough for SHARK2 after resampling. If
  needed, read touch at about 60 Hz in KEYS only.
- link_tx/link_rx: kb_* messages; hello minor 7.

## Simulator
- Mouse drag = stroke; script commands `stroke x,y x,y …` and `key <char>`; a fake daemon answer for
  the UI tests; scripts `keys_basic.txt`, `keys_pick.txt`, `keys_refusal.txt`.

## Work split (parallel Sonnet sub-agents, strict file ownership)
0. Orchestrator: protocol spec (PROTOCOL.md §7, CDDL, vectors, generated codecs), UI-V2 §4.8, issues.
1. A — daemon decoder `daemon/crates/agentlinkd/src/keys/{layout,lexicon,decode,predict}.rs` + eval.
2. B — daemon plumbing `device.rs`/`typing.rs`/new `keys/session.rs`: kb_* handling, composition,
   kitty typing and erasing, context gathering, refusals, trace privacy.
3. C — firmware `ui_keys.c`, `keys.c`, nav table, link tx/rx, Reply-list entry.
4. D — simulator input plus scripts.
Steps 1-4 start in parallel after step 0 is committed. Then: integrate, flash, tune on the device (key
size, stroke thresholds, prior weights).

## Milestones
- M1 (spike, about 1 day): decoder + eval harness only, with layout geometry taken from a mock JSON.
  Proves accuracy on this screen size before any UI work.
- M2: end to end, letters only, top-1 typed into kitty, B deletes, A-hold sends.
- M3: rim candidates, symbols layer, context vocabulary from terminal/repo/question.
- M4: optional LLM rerank and SpeakFaster-style "initials" mode (tap the first letters, pick a phrase).

## Open decisions (defaults chosen)
- Text appears in the terminal as you type (default) rather than being held until send. This avoids
  showing a text body on the watch. Risk: the agent's TUI could steal the half-typed input; mitigated
  by the refusals and by only allowing it when the session isn't working.
- Non-kitty terminals: unsupported at first (same as `type`).

## Environment (reporter)

- agent: copilot-cli/opus
- host: mac-work
- platform: macos-arm64
- os: macOS 26.5 (Darwin 25.5.0)
- git: 9c5bd1c main
- toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74

## Log

- 2026-11-10T18:30:58Z · copilot-cli/opus@mac-work · macos-arm64 · 9c5bd1c: filed
- 2026-11-10T18:33:57Z · copilot-cli/opus@mac-work · macos-arm64 · b7c755b: claimed
- 2026-11-10T18:33:57Z · copilot-cli/opus@mac-work · macos-arm64 · b7c755b: Plan fixed: layout geometry (40x48 keys, rows y=166/214/262/310, candidates in the top cap), proto 0.7 (kb_start/kb_stroke/kb_cands, cmd kb_pick/kb_del/kb_send/kb_end). Sub-agents: A decoder (daemon keys/), B daemon plumbing, C firmware+simulator.
- 2026-11-10T18:43:56Z · copilot-cli/sonnet-keys-decoder@mac-work · macos-arm64 · 17020b3+dirty: A: agentlink-keys decoder committed 17020b3. Release eval on 2000 estimated-common words (synthetic swipes, key sigma 0.25 key widths): strokes top-1 81.6%, top-4 97.9%; developer words 95.7/99.6; taps 97.3/100; decode mean 0.26 ms, max 1.2 ms. Lexicon: SCOWL 2020.12.07 (permissive), 39k words, rank estimated (no frequencies).
- 2026-11-10T18:49:58Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · 377e945+dirty: firmware+simulator side started (agent C): keys.c, ui_keys.c, MODE_KEYS, sim script commands
- 2026-11-10T18:50:24Z · copilot-cli/sonnet-daemon@mac-work · macos-arm64 · 377e945+dirty: daemon plumbing: agentlinkd keys.rs (kb_start/stroke/cands, kb_pick/del/send/end), welcome.keys gating, trace redaction; fmt/clippy/test workspace green
- 2026-11-10T19:03:39Z · copilot-cli/sonnet-fw@mac-work · macos-arm64 · ed8de70: firmware+simulator KEYS done in ed8de70: MODE_KEYS, keys.c/ui_keys.c, kb_start/kb_stroke/kb_cands + ops 13-16, hello minor 7, sim scripts keys_basic/pick/refusal; ctest 66/66 pass, idf build ok, DRAM free 133293 B, atlas check ok
- 2026-11-10T19:06:32Z · copilot-cli/opus@mac-work · macos-arm64 · a150955: Integrated: 17020b3 decoder, 377e945 proto 0.7, c75190e daemon, ed8de70 firmware+sim, a150955 symbols layer glue. Checks: cargo fmt/clippy/test workspace green; ctest 66/67 (1 skipped); idf build ok, DRAM free 133293 B; atlas 130/130. Flashed; hello proto 0.7 with feature keys, welcome keys:true. Next: on-device tuning with real strokes.
- 2026-11-10T19:26:52Z · copilot@mac · macos-arm64 · 72997bd: commit 72997bd: keys 44x54 (y 170/224/278, bottom 332), badges r190 199/161 deg, double tap below keys (y>=386, 400 ms) goes back; sim 68/68, cargo test, atlas 130/130, DRAM free 133285 B; flashed
- 2026-11-10T20:45:42Z · copilot-cli/opus@mac-work · macos-arm64 · e7b3aba: User on the StopWatch: typing works acceptably; leaving KEYS erases the uncommitted text in the agent's input box (desired, kb_end erase confirmed). Remaining: optional M4 (LLM rerank, initials mode).
- 2026-11-11T05:20:23Z · claude-code@linux-dev · linux-x86_64 · 5681f17+dirty: From the UI v3 audit (issue 7732, docs/design/UI-PROFILE.md finding L2 on branch ui-v3): in the simulator shots keys_cands.png and keys_symbols.png the outer keys of the top row (q/p, 1/0) touch the rim and the candidate row is cut by it; v3 keeps content inside r <= 205 (theme_chord). ui-v3 does not edit ui_keys.c or keys.c; it only changes shared tokens (typeface Figtree via theme_font, legend look, toast slot on y 364), so KEYS will pick those up at merge. Worth checking the key grid against the new chord rule then.
- 2026-11-13T19:33:47Z · copilot-cli/opus@mac-work · macos-arm64 · ed9c889: fixed: user confirmed on the watch, 2026-11-13
    - agent: copilot-cli/opus
    - host: mac-work
    - platform: macos-arm64
    - os: macOS 26.5 (Darwin 25.5.0)
    - git: ed9c889 main
    - toolchains: cc 21.0.0, cmake 4.4.5, ninja 1.13.2, node 26.13.0, python 3.15.2, sdl2 2.32.74
