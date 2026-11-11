---
id: 20261111-dd3d
title: Watch showed an approval card for AskUserQuestion and nothing for multi-select questions
status: fixed
severity: med
area: daemon
platforms: any
owner: claude-code@linux-dev
created: 2026-11-11T01:42:37Z
reporter: claude-code@linux-dev
reported_on: linux-x86_64
verified_on: linux-x86_64
gh:
---

## Summary

User report 2026-11-10: "the watch is getting out of sync ... a multi select question and it is showing
an approval prompt". Trace: PreToolUse(AskUserQuestion) gave a question card; after its 15 s, Claude
Code fired PermissionRequest(AskUserQuestion), which became a generic "AskUserQuestion" approval card
holding the terminal another 15 s. Multi-select / multi-question prompts got no card at all.

## Environment (reporter)

- agent: claude-code
- host: linux-dev
- platform: linux-x86_64
- os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
- git: b7d78b6+dirty main
- toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0

## Log

- 2026-11-11T01:42:37Z · claude-code@linux-dev · linux-x86_64 · b7d78b6+dirty: filed
- 2026-11-11T01:42:37Z · claude-code@linux-dev · linux-x86_64 · b7d78b6+dirty: claimed
- 2026-11-11T01:42:37Z · claude-code@linux-dev · linux-x86_64 · b7d78b6+dirty: fixed: this commit: PermissionRequest for AskUserQuestion makes no card (session sub = question); a question the wheel cannot answer (several questions, multi-select, no options) is a notice: Focus-only card with the question, hook released at once, cleared when the session moves on (10 min cap). daemon 141 tests incl. showing_a_question_is_not_an_approval and questions_the_wheel_cannot_answer_become_notices.
    - agent: claude-code
    - host: linux-dev
    - platform: linux-x86_64
    - os: Ubuntu 24.04.4 LTS (kernel 7.0.0-42-generic)
    - git: b7d78b6+dirty main
    - toolchains: cc 13.3.0-6ubuntu2~24.04.1, cmake 3.28.3, ninja 1.11.1, node 18.19.1, python 3.12.3, rustc 1.95.0, cargo 1.95.0, sdl2 2.30.0
