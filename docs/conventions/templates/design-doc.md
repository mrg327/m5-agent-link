---
status: draft            # draft | accepted | implemented | superseded
owner: ui                # a role: architecture | protocol | firmware | daemon | ui | tools | conventions
issue: YYYYMMDD-xxxx     # the tracking issue; a list if several
last_reviewed: YYYY-MM-DD
# superseded_by: OTHER.md  (only when status is superseded)
summary: 'What it lets the user do, in one line (the index shows it)'
---

# Feature name: what it lets the user do, in one line

Protocol 0.NN (§n) if it changes the wire. Code: `path/to/main/file.c`, `daemon/crates/…/x.rs`.
Copy this file to `docs/design/FEATURE-NAME.md` and delete the hints in *italics*. Rules:
[docs.md](../docs.md).

## 1. Problem

*What the user sees today and why it is wrong or missing. Name the screen, the command, the
measurement. Two to five sentences.*

Goals:

- *What this must do, each one testable.*

Not goals:

- *What it deliberately leaves out, so nobody adds it by accident.*

## 2. Decisions

*One line each, dated and attributed. Add to this list; do not rewrite it ([docs.md](../docs.md)
DOC-12, DOC-14).*

- Decided (who, YYYY-MM-DD): *what was chosen*, over *the runner-up*, because *the reason*.

## 3. Design

*How it works. For a screen: start with a sketch, a mock render or a glyph table, then the text the
screen shows, each word justified (glyphs over words). For the protocol: the message, its keys and
its version. For the firmware: the modules, the state they own, and the task each runs on.*

### 3.1 Safety and limits

*What must never happen (an approve without a hold, a request answered after it changed), and the
budgets that apply (internal RAM, BLE throughput, battery).*

## 4. Checks

*How we know it works, by test size.*

| Size | Test | Command |
|---|---|---|
| small | *unit test, one process, no I/O* | *`ctest -R …`, `cargo test -p …`* |
| medium | *simulator script, mock daemon, protocol vectors* | *`simulator/scripts/…`* |
| large | *bench task on the real watch; boot log read after the flash* | *`agentlink bench bench/tasks/….txt`* |

## 5. Later

*Open questions, deferred work and ideas, each with an issue ID once filed.*
