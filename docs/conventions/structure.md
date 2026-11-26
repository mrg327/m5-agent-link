---
status: accepted
owner: conventions
issue: 20261122-db53
last_reviewed: 2026-11-22
---

# Structure

Where code lives, which component may depend on which, where new code goes, and which files are
generated. The repository map for newcomers is in [`README.md`](../../README.md); this page holds the
rules behind it. Code-level rules are in [c.md](c.md), [rust.md](rust.md), [python.md](python.md) and
[js.md](js.md); protocol names are in [protocol.md](protocol.md). The tour of the code, with the
data flow of one request and these invariants in one list, is [`ARCHITECTURE.md`](../../ARCHITECTURE.md).

The model is the one Google, Chromium and Kubernetes use: a directory is the unit of build, ownership
and docs, and every allowed dependency direction is written down where a script can check it
([checkdeps](https://chromium.googlesource.com/chromium/src/+/HEAD/buildtools/checkdeps/README.md),
[import-boss](https://pkg.go.dev/k8s.io/kubernetes@v1.31.1/cmd/import-boss)). Each rule below has an
ID; the structure check (`scripts/check structure`, below) prints that ID with every finding.

## Top level

The tree is split by component, not by language.

| Path | What it is | Language |
|---|---|---|
| `firmware/` | ESP-IDF project: `components/{core,agentlink_hal,agentlink_proto}`, `main/`, `boards/<name>/`, `tools/` | C, C++ (boards) |
| `simulator/` | The same `core` on SDL2: host HAL, script runner, `tests/`, `scripts/` | C |
| `daemon/` | Rust workspace (`crates/*`), `integrations/` (agent extensions), `tools/` (end-to-end tests) | Rust, TS, Python |
| `protocol/` | `agentlink.cddl` (normative), `vectors/`, `fixtures/`, `tools/` | CDDL, Python |
| `index.html`, `atlas/` | The Atlas: page, JS, extracted facts, `wasm/` (core built for the browser) | JS, Python, C |
| `bench/` | Task scripts the bench plays on the real watch | text |
| `design/` | Design-board state (`board/*.json`), written by `tools/board` and sessions | JSON |
| `docs/` | `design/` (ours), `research/` (ours), `conventions/` (this), the rest vendor docs | Markdown |
| `issues/` | The shared work log, driven by `scripts/issue` | Markdown |
| `scripts/` | Repo-wide entry points run from the root: `issue`, `atlas`, `check`, `fetch_docs.py` | sh, Python |
| `tools/` | Standalone developer tools with their own README: `board/`, `linux/` | JS, C |

- **S-TOP-01** A new top-level directory needs a row in the `README.md` repository map and in this
  table, because an unlisted directory has no owner and no check. `checked: scripts/check structure`. Baseline: none.

## Layering

### Firmware and its hosts (C)

```text
   firmware/main      simulator harness       atlas/wasm/web_main.c
   (main.c)           (main.c, sim_script.c,
        |              tests/)  white-box
        v                  |                       |
   core/include/core.h <---+-----------------------+
        |
   core/src  ------------------------------+
        |                                  |
        v                                  v
   agentlink_hal/include/hal.h       agentlink_proto/include
        ^      |                     (generated codec + zcbor)
        |      v
        |    lvgl.h
        |
   boards/<name>/   simulator/src/sim_*.c   atlas/wasm/web_hal.c
   (implement hal.h; ESP-IDF, M5GFX, SDL2 or Emscripten below)
```

Arrows point from includer to included. `core` and `agentlink_proto` sit in the middle and know
nothing about the boards; the boards and host HALs know nothing about `core`. That `core` builds and
runs against two host HALs (simulator, WASM) is the standing proof that the port is real.

- **S-DEP-01** `core` may include its own `include/`, `src/` and `third_party/`, `hal.h`,
  `agentlink_proto/include`, `lvgl.h` and the C library. It never includes `boards/`, `main/`,
  `simulator/` or `atlas/` headers. `checked: build` (ESP-IDF include isolation: `core` REQUIRES only
  `agentlink_hal agentlink_proto lvgl__lvgl`; the simulator build) and `checked: scripts/check structure`
  (for a clear message). Baseline: none.
- **S-DEP-02** In `core`, ESP-IDF and FreeRTOS headers appear only inside `#ifdef ESP_PLATFORM`, with
  a host fallback beside them, so the same file builds in the simulator. `checked: simulator build`;
  `checked: scripts/check structure`. Current sites, all guarded: `core/src/ring.h:15-17` (lock types),
  `core/src/model.h:28-29` (`EXT_RAM_BSS_ATTR`).
- **S-DEP-03** `agentlink_hal` is header-only and includes only the C library and `lvgl.h` (it hands
  out `lv_display_t` and `lv_indev_t`). It includes nothing else from the project. `checked: build`
  (REQUIRES `lvgl__lvgl` only). Baseline: none.
- **S-DEP-04** `agentlink_proto` includes only its own headers and the C library; it never depends on
  `core`, `hal.h` or LVGL. It is the bottom layer, shared in spirit with the daemon's
  `agentlink-proto` crate. `checked: build` (no REQUIRES). Baseline: none.
- **S-DEP-05** `boards/<name>/` may include `hal.h`, its own headers and sub-components (for example
  `boards/stopwatch/sw_power`), ESP-IDF and managed vendor components. It never includes `core` or
  `agentlink_proto` headers, because a board is an adapter, not a client. `checked: build` (the board
  does not REQUIRE `core`); `checked: scripts/check structure`. Baseline: none.
- **S-DEP-06** `firmware/main` includes `hal.h`, `lvgl.h`, ESP-IDF and only the public `core.h`, never
  `core/src/*`. The same holds for `atlas/wasm/web_main.c`. `checked: build`; `checked: scripts/check structure`.
  Baseline: none.
- **S-DEP-07** A host HAL (`simulator/src/sim_*.c` in the `agentlink_hal` library, `atlas/wasm/web_hal.c`)
  includes `hal.h`, `lvgl.h`, its platform (SDL2, Emscripten) and its own headers; never `core`.
  `checked: scripts/check structure`. Baseline: none.
- **S-DEP-08** The simulator harness (`simulator/src/main.c`, `sim_script.c`, `sim_fw_image.c`) and
  `simulator/tests/*.c` are white-box: they may include `core/src` private headers and the codec
  headers. This is the one sanctioned exception to "public header only"; keep new reach-ins in the
  script runner or a test, not in the host HAL. `review`. Widest today: `sim_script.c:92-104`
  (13 private headers).
- **S-DEP-09** No `#include` path contains `..`. A relative include bypasses the include-path isolation
  that S-DEP-01 to 05 rely on ([Bazel C++](https://bazel.build/docs/bazel-and-cpp)). `checked: scripts/check structure`.
  Baseline: none (`third_party/` included).
- **S-DEP-10** Every function declared in `hal.h` is defined by every HAL: each `boards/<name>/`, the
  simulator and `atlas/wasm`. A feature a board lacks is a `caps` flag plus a no-op, never a missing
  symbol. `checked: link` (each of the three builds fails on a missing definition) and `checked: scripts/check structure`
  (names the missing function before a build). Baseline: none.

### Inside `core`

`core` is one ESP-IDF component, so the build cannot separate its files. The tiers below are what the
code follows today; the include script makes them visible.

```text
   glue    app  link*  ota  power  power_report  debug.c  bench     (may include anything in core)
     |
   ui      ui_*.c                                 (lvgl.h, hal.h, services, pure, leaves)
     |
   service prefs  sound  feedback  voice  keys    (hal.h, link.h, pure, leaves; no LVGL)
     |
   pure    model nav gesture steps graph ring gauges detail done motion sha256 sound_bank
           toy_gate
                                                  (C library and leaves only; unit-tested on the host)
   leaves  debug.h  ui_icons.h                    (no includes beyond the C library)
```

- **S-CORE-01** Pure modules include no `hal.h`, no `lvgl.h` and no `ui*.h` other than the leaf
  `ui_icons.h`, so each builds alone in a `simulator/tests` binary. `checked: ctest` (the test
  binaries link only the module) and `checked: scripts/check structure`. Baseline: none.
- **S-CORE-02** Service modules may use `hal.h` and the send functions in `link.h`, but not LVGL or
  any UI header. `checked: scripts/check structure`. Baseline: none.
- **S-CORE-03** UI files (`ui_*.c`) never include glue headers (`app.h`, `link.h`, `link_internal.h`,
  `ota.h`, `power.h`, `power_report.h`, `bench.h`); a screen reports a user action upward through the
  `ui_actions_t` callbacks given to `ui_init()`, which keeps screens testable from a script.
  `checked: scripts/check structure`. Baseline: `core/src/ui_ring.c:8` (`power.h`, for `power_display_on()`),
  `core/src/ui_theme.c:7` (`app.h`, for `app_now_unix()`).
- **S-CORE-04** LVGL is called from UI files and the glue tier only, and only from `core_step()`'s
  thread (`core/README.md`). Glue use today: `app.c`, `bench.c`/`bench.h` (synthetic input),
  `ota.c:11` (only `lv_tick_get()` as the clock, since `hal.h` has no millisecond clock). `review`;
  `checked: scripts/check structure` for the include half.
- **S-CORE-05** Core decides by `hal_board_info()->caps`, not by board name. `review`. Baseline:
  `core/src/link_tx.c:37-41` maps the board string to the wire `device_class`, which is the one
  accepted use.

### Daemon crates (Rust)

```text
   agentlinkd ----> agentlink-proto      agentlink-hook   (no workspace deps)
       |  \-------> agentlink-keys
       \----------> agentlink-delta
   agentlink (CLI) -> agentlink-proto
```

- **S-CRATE-01** Library crates (`agentlink-proto`, `agentlink-keys`, `agentlink-delta`) depend on no
  workspace crate. `checked: scripts/check structure` (reads each crate's `Cargo.toml`). Baseline: none.
- **S-CRATE-02** Binary crates depend on library crates, never on another binary; `agentlinkd` and the
  `agentlink` CLI talk over IPC. `checked: cargo` (a binary crate cannot be a dependency) and
  `checked: scripts/check structure`. Baseline: none.
- **S-CRATE-03** `agentlink-hook` depends on no workspace crate. It stays tiny and fail-open, and it
  speaks JSON to the daemon socket, not the wire protocol. `checked: scripts/check structure`. Baseline: none.
- **S-CRATE-04** Workspace crate dependencies are declared through `[workspace.dependencies]`.
  `checked: scripts/check structure`. Baseline: `agentlinkd/Cargo.toml` uses `path =` for `agentlink-keys` and `agentlink-delta`.

### Scripts and tools (Python, JS)

- **S-TOOL-01** A Python script is a single file run with `uv run` (inline PEP 723 dependencies) or
  `python3` (stdlib only). It never imports another component's Python; it may read other components'
  source files as data, as `atlas/tools/extract.py` does. `planned: phase 2`. Baseline: none.
- **S-TOOL-02** Atlas JS modules follow the owner split in `atlas/CONTRACT.md`; cross-area calls go
  through the shared `ctx` and bus. `review`; `planned: phase 3` (dependency-cruiser).

## Where new code goes

- **S-NEW-01 Watch screen.** `firmware/components/core/src/ui_<name>.c`, plus `ui_<name>.h` only if
  another file calls it; add it to `CORE_SRCS` in `core/CMakeLists.txt`. Its state and decisions go
  in a pure module `<name>.c` with `simulator/tests/test_<name>.c` and an `add_test` line. Add
  `simulator/scripts/<name>_*.txt` with `shot` lines and a `sim_script_test` entry. Start from glyphs:
  icons come from `ui_icons.h`; a new one is added to `firmware/tools/fonts/gen.mjs` and the fonts are
  regenerated (UI-V3 §2.2). `review`.
- **S-NEW-02 HAL operation.** Declare it in `hal.h` with a Doxygen comment (context it may be called
  from, what a board without the feature does). Implement it in every `boards/<name>/`,
  `simulator/src/sim_*.c` and `atlas/wasm/web_hal.c` in the same commit (S-DEP-10). Add a `caps` flag
  if a board may lack it. `checked: link`.
- **S-NEW-03 Protocol message.** In this order: `protocol/agentlink.cddl` (names per
  [protocol.md](protocol.md)); the symbolic vector in `protocol/tools/gen_vectors.py`, then regenerate
  `protocol/vectors/`; `agentlink_proto/regen.sh` for the C codec; the hand-written types in
  `daemon/crates/agentlink-proto`; handling in `core/src/link_rx.c` / `link_tx.c` and
  `agentlinkd/src/device.rs`; `docs/design/PROTOCOL.md`; `python3 atlas/tools/extract.py` (it fails on
  an unlisted message type). `checked: protocol vectors diff, atlas --check`.
- **S-NEW-04 Daemon feature.** A module `daemon/crates/agentlinkd/src/<feature>.rs`; its CLI side in
  `daemon/crates/agentlink/src/<feature>.rs`; an end-to-end test in `daemon/tools/<feature>_e2e.py`; a
  section in `daemon/README.md`. A new library crate only when the logic needs no daemon state, no
  async runtime and no IO (like `agentlink-keys`, `agentlink-delta`). `review`.
- **S-NEW-05 Board.** `firmware/boards/<name>/` as an ESP-IDF component named `<name>` that REQUIRES
  `agentlink_hal`; optional `firmware/sdkconfig.<name>`; build with `-DAGENTLINK_BOARD=<name>`.
  `checked: build`.
- **S-NEW-06 Tool or script.** Repo-wide entry point run from the root: `scripts/`. Helper for one
  component: `<component>/tools/`. Standalone tool with its own UI or server: `tools/<name>/` with a
  README. Bench task: `bench/tasks/<name>.txt`. `review`.
- **S-NEW-07 Doc.** A design doc is `docs/design/<NAME>.md` (upper case, hyphens), started from
  [templates/design-doc.md](templates/design-doc.md) with the front matter in [docs.md](docs.md).
  Background research goes in `docs/research/`. The rest of `docs/` is fetched vendor documentation:
  do not add our own pages there. Front matter: `checked: scripts/check docs (doc_meta.py)`; the
  rest `review`.

## Generated, vendored and derived files

- **S-GEN-01** A generated file is never edited by hand: change the generator or its input, rerun it,
  and commit both in one commit. `review`; `planned: phase 2` (regenerate-and-diff for each row below).
- **S-GEN-02** A generated file starts with a `GENERATED by <generator> from <input>. Do not edit.`
  line (or the generator's own banner) and is listed as `linguist-generated` in `.gitattributes`, so
  GitHub collapses its diff ([GitHub docs](https://docs.github.com/en/repositories/working-with-files/managing-files/customizing-how-changed-files-appear-on-github)).
  `checked: scripts/check structure` for the `.gitattributes` half (`.gitattributes` marks every row below `linguist-generated`
  and the vendored code and doc mirrors `linguist-vendored`); `review` for the marker line, which JSON
  and binary files cannot carry. Baseline: none; only `tokens.generated.css` has the marker line.
- **S-GEN-03** Formatters and linters skip generated and vendored files. `planned: phase 1`.

| Files | Generator (input) | Check today |
|---|---|---|
| `firmware/components/agentlink_proto/src/agentlink_codec_{encode,decode}.c`, `include/agentlink_codec_{encode,decode,types}.h` | `agentlink_proto/regen.sh` (zcbor 0.9.1, `protocol/agentlink.cddl`) | none; `planned: phase 2` |
| `agentlink_proto/{src,include}/zcbor_*` (vendored) | copied by `regen.sh`, then `patches/*.patch` | none |
| `protocol/vectors/*.json` | `uv run protocol/tools/gen_vectors.py` | `checked: protocol.yml` (regen + `git diff --exit-code`) |
| `protocol/fixtures/ota/{base,new}.bin`, `python.patch` | `uv run protocol/fixtures/ota/gen.py` | none |
| `protocol/fixtures/ota/rust.patch` | the `agentlink-delta` test | `checked: cargo test` (compares) |
| `atlas/data/facts.json`, `atlas/css/tokens.generated.css` | `python3 atlas/tools/extract.py` (firmware, protocol, daemon sources) | `checked: atlas.yml` (`--check`) |
| `atlas/vendor/watch.{js,wasm}` | `atlas/wasm/build.sh` (core + LVGL, Emscripten) | none (needs Emscripten) |
| `firmware/components/core/src/fonts/font_*.c` | `firmware/tools/fonts/gen.mjs` (lv_font_conv, `tabular.py`) | partial: `test_fonts` (equal-width digits) |
| `daemon/crates/agentlink-keys/data/english.txt` | `data/build_lexicon.py` (a SCOWL checkout) | none (input not in the repo) |
| `daemon/Cargo.lock`, `firmware/tools/fonts/package-lock.json` | cargo, npm | tool-maintained |

Hand-kept mirrors, not generated, that must move together: `ui_icons.h` with the code points in
`gen.mjs`; the `agentlink-proto` crate with `agentlink.cddl` (held together by the vectors).

## Directory READMEs

- **S-DOC-01** Every build unit (a top-level directory, an ESP-IDF component or board, a crate, a
  host HAL, a standalone tool) has a `README.md`, or a row in the nearest README above it that names
  it: a table row whose first cell, or a list item whose first code span, is its path. A top-level
  directory's parent row is the `README.md` repository map. `checked: scripts/check structure`. Baseline: none.
- **S-DOC-02** A README says, in this order and briefly: what the directory is (one or two lines); how
  to build and test it (the exact commands, the same as its `scripts/check` area); a file or
  subdirectory → concern table; the dependency rules from this page that bind it, by ID; the generated
  files in it and their generators. `review`.
- **S-DOC-03** A README names paths and symbols rather than linking into source, and every path it
  names exists. `planned: phase 2` (offline link check, phase 1, plus a path-exists check). Baseline:
  `firmware/README.md` "Layout" still describes the old `hal` directory-name workaround, which the
  rename to `agentlink_hal` removed.

## Per-component AGENTS.md

- **S-AGT-01** Phase 4 adds a short `AGENTS.md` in `firmware/`, `simulator/`, `daemon/`, `protocol/`
  and `atlas/`, each with a `CLAUDE.md` containing `@AGENTS.md` as at the root. Each is at most about
  40 lines: the `scripts/check` area to run, the three to five rules from this page that are easiest to
  break there (by ID), and where the design doc is. The root `AGENTS.md` stays the map. `planned: phase 4`.

## CODEOWNERS

- **S-OWN-01** Phase 2 adds `.github/CODEOWNERS`, advisory only: merges are local and need no review.
  It serves as a map of who answers for what. Last matching pattern wins, so broad paths come first.
  Every top-level directory gets an entry. `checked: scripts/check structure`. The file is `.github/CODEOWNERS`.

The proposed map uses roles. In the file each role becomes the maintainer's handle until there are
more people.

| Path | Role |
|---|---|
| `*` | maintainer |
| `firmware/components/core/` | firmware-core |
| `firmware/components/core/src/ui_*`, `src/fonts/`, `firmware/tools/fonts/`, `docs/design/UI-*.md`, `docs/design/GLYPHS.md`, `design/` | ui-design |
| `firmware/components/agentlink_hal/`, `firmware/boards/`, `firmware/main/` | firmware-board |
| `protocol/`, `firmware/components/agentlink_proto/`, `daemon/crates/agentlink-proto/`, `docs/design/PROTOCOL.md` | protocol |
| `simulator/`, `bench/` | test-harness |
| `daemon/` | daemon |
| `daemon/integrations/`, `daemon/crates/agentlink-hook/` | host-integration |
| `atlas/`, `index.html` | atlas |
| `scripts/`, `tools/`, `.github/`, `docs/conventions/` | tooling |
| `docs/design/`, `docs/research/`, `issues/` | maintainer |

## The structure check

`scripts/check structure` runs one Python stdlib script, `scripts/check.d/structure.py` (under a
second). Its rule table is at the top of the script, keyed by the IDs above; each finding prints
`path:line: S-ID message - how to fix`, and existing findings are counted in a baseline that may only
shrink ([README](README.md)): `scripts/check.d/baselines/structure.txt`, one `RULE-ID key` per line;
an entry that no longer occurs fails as stale, and `--update-baseline` rewrites the file. It verifies:

1. **Includes** (S-DEP-01 to 09, S-CORE-01 to 04): parse every `#include` in `firmware/`, `simulator/`
   and `atlas/wasm/`, resolve it through the builds' include directories to the unit that owns the
   header (or an external family: C library, LVGL, ESP-IDF, vendor, SDL2, Emscripten, POSIX), and test
   the edge against the rule table, including the core tiers. ESP-IDF headers in core must sit under
   `#ifdef ESP_PLATFORM`. No `..` paths. S-DEP-08 reach-ins are allowed, not reviewed.
2. **HAL completeness** (S-DEP-10): every non-inline function in `hal.h` has a definition in each HAL,
   (every `boards/<name>/`, `simulator/src/sim_*.c`, `atlas/wasm/web_hal.c`), so a missing one is named
   before the link step.
3. **Crates** (S-CRATE-01 to 04): each workspace member's `Cargo.toml` (no cargo needed), workspace
   edges only, against the allowed graph; a `path =` dependency is S-CRATE-04.
4. **Generated files** (S-GEN-02): each row of the table above, and each vendored path, is marked in
   `.gitattributes` (`git check-attr`). Not yet: the marker line, and regenerate-and-diff for the zcbor
   codec (the vectors and atlas facts are diffed by `scripts/check protocol` and `atlas`).
5. **Maps and READMEs** (S-TOP-01, S-DOC-01, S-OWN-01): every top-level directory is in the
   `README.md` map, this page's Top level table and `CODEOWNERS`; every build unit has a README or a
   parent row. Not yet: every path named in a README exists (S-DOC-03).
6. **Source lists** (S-NEW-01): every `core/src/*.c` appears in `CORE_SRCS`, so a new file is not silently left
   out of one of the three builds (none missing today).
