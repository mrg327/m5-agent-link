---
id: 20261110-1e57
title: KEYS: swipe keyboard on the watch, decoded on the PC
status: claimed
severity: med
area: firmware
platforms: any
owner: copilot-cli/opus@mac-work
created: 2026-11-10T18:30:58Z
reporter: copilot-cli/opus@mac-work
reported_on: macos-arm64
verified_on:
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
