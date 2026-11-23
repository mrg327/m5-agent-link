---
id: 20261122-9df4
title: ASK takeover, landing and back: what the user returns to after an interruption, a drop or an answer
status: open
severity: med
area: firmware
platforms: any
owner:
created: 2026-11-22T19:20:13Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

UX profile (docs/design/UX-PROFILE.md §3 E5-E9, §4 N1-N5). Grouped:
- After answering, a second pending request is neither opened nor selected (events/e13_hold_other_arrives).
- ASK shows no link state; offline the full hold ends in a reject buzz. KEYS offline: buzz only, composition cleared before the result (e16, e17, e26).
- The open session ends: AGENT, READ, KEYS drop to HOME silently (KEYS erases); VOICE on a question answered elsewhere or expired closes silently and loses the recording (e19_*, e24, e25).
- The same session's next request replaces Approved after ~0.55 s with an identical layout (e23c).
- AGENT's selection jumps to the first row on a state change (e20).
- Back from READ/VOICE/KEYS/GRAPH/WATCH opened from AGENT lands on HOME, not AGENT; after a takeover only WATCH and SCROLL resume (events/back/*, e05, e07, e08).
- KEYS: B held and A+B erase unsent text without a confirm; swipe right / long press do nothing there. VOICE: a stray swipe right discards (back/keys_*, back/voice_swr).
- A double works only on HOME and lands on the card; a double tap on an AGENT row commits then reopens AGENT; long press near the top of STATUS/LABS is not back (e18, e23b, back/status_lpt).
Repros under simulator/scripts/ux (branch ux-profile)/events.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 3724645 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T19:20:13Z · claude-code@linux-dev · linux-x86_64 · 3724645: filed
- 2026-11-23T07:05:43Z · claude-code/takeover@linux-dev · linux-x86_64 · ab35617+dirty: claimed
- 2026-11-23T07:22:26Z · claude-code/takeover@linux-dev · linux-x86_64 · 305e034: Done (305e034, 405a679): E5 a second request that arrives in ASK, or while the deck shows another request, waits and opens once the user leaves (takeover_second_request); E7 AGENT, ASK, SCROLL, VOICE, KEYS, READ, GRAPH say 'Session ended' when their session ends (takeover_session_ended); E9 AGENT keeps its selected item across a state change, and a hold on an item that vanished ends (takeover_agent_keeps_item); N1 part: READ/KEYS/VOICE/GRAPH/STATUS/LABS are no longer torn down by a takeover (883f).
- 2026-11-23T07:22:27Z · claude-code/takeover@linux-dev · linux-x86_64 · 305e034: Left: E6 (ASK/KEYS show no link state), E7 VOICE on a question answered elsewhere still closes and loses the recording, E8 (next request replaces Approved with an identical layout), N1 back-to-AGENT (tried: back from READ/VOICE/KEYS/GRAPH/WATCH opened from AGENT to AGENT works, but 9 existing sim tests encode back -> ring, sigil_* among them; needs a product call before a test sweep, reverted), N2 (KEYS B held / A+B erase without confirm; VOICE stray swipe right), N3, N4. N5: the long press at (233,110) lands on STATUS/LABS's top orbit disc, where a hold does nothing as on AGENT's discs; making rim long presses back there still hit the disc, so it is not a nav-table gap (left).
- 2026-11-23T07:25:23Z · claude-code/takeover@linux-dev · linux-x86_64 · 305e034: Released: left open for the items above.
