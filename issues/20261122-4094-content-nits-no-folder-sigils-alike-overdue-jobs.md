---
id: 20261122-4094
title: Content nits: no-folder sigils alike, overdue jobs look done, start = working, exit 0 failure, empty diff pill, one-word Read
status: claimed
severity: low
area: firmware
platforms: any
owner: claude-code/content@linux-dev
created: 2026-11-22T19:20:13Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on:
gh:
---

## Summary

UX profile (docs/design/UX-PROFILE.md §5 C6-C8). Sessions with no folder share one sigil (content/s04_empty). A job far past its estimate shows a full ring (j01_jobs). Start looks like working (s05_states). Failed with exit 0 shows "0"; a 0/0 diff shows an empty pill (d01_done). A one-word message still offers Read and the page shows it alone (r02_word). The preview is often a markdown heading; reader pages split code blocks early (r04_md).

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-43-generic)
- git: 3724645 main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 22.23.2, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-22T19:20:13Z · claude-code@linux-dev · linux-x86_64 · 3724645: filed
- 2026-11-23T06:49:13Z · claude-code/content@linux-dev · linux-x86_64 · ab35617: claimed
- 2026-11-23T07:17:41Z · claude-code/content@linux-dev · linux-x86_64 · 06335bf+dirty: No-folder sigils: ui_sigil_set_session(name, sid) rings the dashed folder with arcs hashed from the session id (every session screen switched over). Overdue job: past its estimate the arc turns amber, report 'past its estimate'. Start vs working: the starting hero's arrows are drawn unlit (grey); the hero font has no spare glyph for 'starting' and the Phosphor TTF/npm tools are not on this machine, so a distinct glyph needs a font regen (left). Failed job with exit 0: no '0' mark. 0/0 diff: no lines segment. One-word message: no Read on the card or the AGENT menu (detail_msg_whole: one word, <=32 bytes, whole); a sentence still offers Read since the reader's end carries Reply. Reader: a block that fits one page starts on the next page instead of straddling; no blank line at the top of a page (last_message_card / sigil_agent now 4 pages). Markdown heading preview: the daemon already folds a heading-like first line into the paragraph under it (lastmsg::preview since 0.18); the r04 repro shows 'Summary' only because the simulator's stand-in daemon cuts the first paragraph naively. No change.
- 2026-11-23T07:21:22Z · claude-code/content@linux-dev · linux-x86_64 · ca7ef1b: commit ca7ef1b: overdue arc, exit 0, 0/0 bar, one-word Read, reader blocks, no-folder sigils, starting unlit. Regression tests: sim_content_job_overdue, sim_content_message_word, sim_content_reader_blocks, test_done (0/0). Left open: a distinct glyph for 'starting' needs a Phosphor font regen (hero font has no spare glyph; tools not on this machine), and the sigil / exit-0 / starting changes are checked by render only (the report has no words for them).
