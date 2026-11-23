# Rust: the `daemon/` workspace

How Rust is written in `daemon/`. Issue 20261122-db53 (phase 0). We follow the
[Rust API Guidelines](https://rust-lang.github.io/api-guidelines/checklist.html), the official
[Rust Style Guide](https://doc.rust-lang.org/style-guide/) through rustfmt, and clippy. On top of them
we adopt Microsoft's [Pragmatic Rust Guidelines](https://microsoft.github.io/rust-guidelines/) lint
table and its `#[expect(..., reason)]` rule. This page lists only our choices and deltas, so read the
upstream guides for everything else.

Each rule ends with how it is checked: `checked: <tool>`, `review`, or `planned: phase N`
([README](README.md#how-a-rule-becomes-enforced)). CI (`.github/workflows/daemon.yml`) runs
`cargo fmt --check`, `cargo clippy --workspace -- -D warnings` and `cargo test --workspace`, so
any lint set to `warn` fails CI as soon as it is enabled. That is why new lints land with a baseline.

## 1. Base

- **RS-1. Format with rustfmt defaults.** There is no `rustfmt.toml`, and we keep it that way. The
  default `max_width = 100` already matches the 100-column C limit and Google's Rust guide. Use
  `#[rustfmt::skip]` only for hand-aligned tables or generated code. `checked: cargo fmt --check`
- **RS-2. Clippy's default groups are clean.** `clippy::all` (correctness, suspicious, style,
  complexity, perf) gives no warnings in the workspace. `checked: cargo clippy -D warnings`
- **RS-3. Follow the API Guidelines checklist.** These are the sections that matter most here:
  - Naming: **C-CASE** (RFC 430 casing), **C-CONV** (`as_` is free and borrowed, `to_` is costly,
    `into_` consumes), **C-GETTER** (no `get_` prefix), **C-ITER** / **C-ITER-TY**,
    **C-WORD-ORDER**.
  - Interoperability: **C-COMMON-TRAITS** (derive `Debug`, `Clone`, `PartialEq`, `Eq`, `Default`
    where they make sense), **C-GOOD-ERR**, **C-DEBUG**.
  - Type safety: **C-NEWTYPE**, **C-CUSTOM-TYPE** (an enum instead of a `bool` or `Option`
    argument), **C-STRUCT-PRIVATE** in library crates.
  - Docs: **C-CRATE-DOC**, **C-FAILURE** (`# Errors`, `# Panics`, `# Safety`),
    **C-QUESTION-MARK**, **C-LINK**.
  - We skip C-METADATA, C-RELNOTES, C-HTML-ROOT and C-STABLE, as Fuchsia does: nothing here is
    published to crates.io.

  `review`; partly `planned: phase 3` (pedantic covers some of C-CONV and C-GETTER)
- **RS-4. Names are short and say what the thing is.** Use at most two short words, and don't repeat
  the module name (`focus::Error`, not `focus::FocusError`, in new code). Avoid `Manager`,
  `Service` and `Factory` (Microsoft M-SHORT-NAMES, M-WEASEL-WORDS). Constructors are `new`, and
  `try_new` when they return `Result`. Spell the protocol's words the way
  [glossary.md](glossary.md) and [protocol.md](protocol.md) do. `review`

```rust
// bad                                  // good
fn get_battery(&self) -> u8             fn battery(&self) -> u8
fn send(msg: &Msg, urgent: bool)        fn send(msg: &Msg, prio: Prio)
struct SessionManager { .. }            struct Registry { .. }
```

## 2. Crate layout and dependency direction

The workspace is six crates under `daemon/crates/` (Microsoft M-CRATES-FLAT-FOLDER). Shared versions
and metadata live in the root `daemon/Cargo.toml` (`[workspace.package]`,
`[workspace.dependencies]`). The edition is 2021 and the MSRV is 1.88.

| Crate | Kind | Role | Internal deps |
|---|---|---|---|
| `agentlink-proto` | lib | Wire protocol types and CBOR codec, mirroring `protocol/agentlink.cddl` | none |
| `agentlink-keys` | lib | Gesture-typing (SHARK2) decoder for the round keyboard | none |
| `agentlink-delta` | lib | Applies detools sequential firmware patches (bsdiff + heatshrink) | none |
| `agentlink-hook` | bin | Tiny shim run by agent hooks; forwards hook JSON to the daemon and never blocks | none |
| `agentlink` | bin | CLI: status, trace, decode, hooks, device, OTA, remotes | proto |
| `agentlinkd` | bin | The daemon: session registry, hook IPC, BLE/USB/WebSocket transports, trace | proto, keys, delta |

The allowed direction is **binaries → libraries, never the reverse, and never library → library**:

```text
agentlinkd ──► agentlink-proto ◄── agentlink
     ├───────► agentlink-keys
     └───────► agentlink-delta
agentlink-hook (no internal deps)
```

- **RS-5. `agentlink-proto`, `agentlink-keys` and `agentlink-delta` depend on no workspace crate.**
  Each is a leaf that can be tested, fuzzed or reused alone. `planned: phase 2` (a
  `cargo metadata` assertion in the structure check)
- **RS-6. `agentlink-hook` depends on no workspace crate, and has only `serde_json` and
  platform deps.** It runs on every agent hook, so it must start fast and stay tiny. Adding a
  dependency to it needs a reason in the issue. `planned: phase 2`
- **RS-7. No binary crate depends on another binary crate.** Code that two binaries share moves
  into a library crate, or into `agentlink-proto` if it is protocol. `planned: phase 2`
- **RS-8. Refer to sibling crates through `[workspace.dependencies]`** (`agentlink-keys.workspace =
  true`), not `path = "../…"` (M-CRATES-IN-WORKSPACE). `agentlinkd` still uses paths for `keys` and
  `delta`, which is a baseline item. `review`; `planned: phase 2`
- **RS-9. A new crate goes under `daemon/crates/`, is named `agentlink-<word>`, inherits
  `version`, `edition` and `license` from the workspace, and has a one-line `description`.** Its
  row goes into the table above and into [structure.md](structure.md). `review`

## 3. Modules, files and tests

This is what the code does today, written down so new code matches.

- **RS-10. A module is a file: `foo.rs`, with children in `foo/`.** Don't add new `mod.rs` files.
  `agentlinkd/src/focus/mod.rs` is the one existing exception, and moves to `focus.rs` when that
  module is next touched. `review`
- **RS-11. Binary crates declare their modules in `main.rs`**. `agentlinkd` has no `lib.rs`, and
  that is fine while nothing outside it needs its code. `review`
- **RS-12. Unit tests sit at the bottom of the file they test, in `#[cfg(test)] mod tests`**
  (53 files do this today). Cross-module and end-to-end tests go in `tests/` (`agentlinkd/tests/
  smoke.rs`, `agentlink-keys/tests/gate.rs`, `agentlink-proto/tests/vectors.rs`). Tools that are run
  by hand go in `examples/`. `review`
- **RS-13. Every file starts with `//!` saying what it is for** in one to three lines, and why if
  that is not obvious. Most files already do (see `pointer.rs`). `review`; `planned: phase 4`
  (`missing_docs` covers public modules)
- **RS-14. Keep `pub` narrow.** Use `pub(crate)` for items shared inside a crate. Library crates
  re-export their API from `lib.rs`, and each item has one public path (M-SINGLE-ITEM-PATH).
  `review`

## 4. Errors

- **RS-15. Library crates return a typed error built with `thiserror`.** These are `proto`, `keys`
  and `delta`. The error is an enum when callers need to match on the cause, otherwise a struct. It
  implements `std::error::Error` and `Display`, and keeps its source with `#[source]` (C-GOOD-ERR).
  Never return `Result<_, String>` from a library. `review`; `planned: phase 3`
- **RS-16. Binary crates use `anyhow::Result` at the edges**, which means `main`, command handlers
  and task loops. Add context with `.context("what we were doing")`. One error style per binary
  (M-APP-ERROR). `review`
- **RS-17. Inside a binary, a module whose callers match on the cause gets its own error enum.**
  `pointer::PointerError` and `focus::FocusError` are the model. A module whose callers only log the
  error uses `anyhow`. New code does not add `Result<_, String>`. `review`
- **RS-18. Don't swallow errors silently.** `let _ = fallible();` and `.ok();` need a comment that
  says why ignoring is right (for example, "best effort: the window may be gone"). `review`

```rust
// bad: a library error the caller cannot match on
pub fn apply_patch(base: &[u8], patch: &[u8]) -> Result<Vec<u8>, String>
// good
#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("patch ends early")] Truncated,
    #[error("patch reads outside the base image")] OutOfBounds,
}
```

Where we are today: no crate depends on `thiserror`. `agentlink-delta` (`Error(&'static str)`) and
`agentlink-keys` (`layout::Error`) implement `std::error::Error` by hand. `agentlink-proto` re-exports
`minicbor`'s error types as `DecodeError` and `EncodeError`, which is acceptable because they are
already typed errors. `agentlinkd` has 27 functions that return `Result<_, String>`. Moving to
`thiserror` is a baseline item, done crate by crate when each crate is touched. It does not block
CI.

## 5. Panics

A panic in `agentlinkd` takes down the link to the watch for every session, so we treat it as a bug
(Microsoft M-PANIC-IS-STOP, M-PANIC-ON-BUG).

- **RS-19. No `unwrap()` and no `expect()` outside tests.** Use `?`, `ok_or`, `let … else`,
  `unwrap_or` / `unwrap_or_default`, or a `match`. `planned: phase 3` (`clippy::unwrap_used`,
  `clippy::expect_used`, with a baseline)
- **RS-20. Tests may unwrap and expect.** `daemon/clippy.toml` will carry:

  ```toml
  allow-unwrap-in-tests = true
  allow-expect-in-tests = true
  allow-panic-in-tests = true
  allow-indexing-slicing-in-tests = true
  ```

  `planned: phase 3` (this file lands with the lint table; it is not added in phase 0)
- **RS-21. A justified exception uses `#[expect]` with a reason**, on the smallest item that
  needs it, never `#[allow]`. If the reason goes away, `#[expect]` warns that it is no longer
  needed. `checked: review` today; `planned: phase 3` (`clippy::allow_attributes`,
  `allow_attributes_without_reason`)

  ```rust
  #[expect(clippy::expect_used, reason = "the regex is a literal; a bad one is a build-time bug")]
  static TITLE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^\d+ ").expect("valid regex"));
  ```

- **RS-22. Mutex poisoning is not a reason to unwrap.** Most of today's unwraps are `.lock()
  .unwrap()` (69 of the 105 in `agentlinkd`). New code uses `.lock().unwrap_or_else(PoisonError::
  into_inner)` or the crate's small `lock()` helper once one exists. Converting the existing calls is
  a mechanical baseline item. `review`; `planned: phase 3`
- **RS-23. `panic!`, `unreachable!`, `todo!` and `unimplemented!`.** `todo!` and `unimplemented!`
  never reach `main` (`planned: phase 3`, `clippy::todo`, `clippy::unimplemented` at deny).
  `unreachable!` is allowed for a state the types cannot rule out, and carries a message that says
  why it cannot happen. An explicit `panic!` is allowed only for a broken invariant, with the values
  in the message (M-PANIC-MESSAGE). `review`
- **RS-24. Indexing and slicing (`v[i]`, `&b[a..c]`) are allowed when the bound has just been
  checked or is fixed by the type** (a `[u8; 16]` indexed by a constant). Data that comes from the
  watch, the hook or a file uses `get()` and returns an error. We do not enable
  `clippy::indexing_slicing` workspace-wide because it is too noisy for codec loops, but
  `agentlink-proto` and `agentlink-delta`, which parse untrusted bytes, may turn it on locally.
  `review`
- **RS-25. Arithmetic on lengths and offsets that come from outside uses `checked_*`** (as
  `agentlink-delta` already does), so a crafted patch or frame cannot overflow. `review`

## 6. Unsafe

`unsafe` exists only for OS calls: CoreGraphics and `SendInput` in `agentlinkd/src/pointer.rs`, Win32
in `agentlinkd/src/focus/windows_terminal.rs` and `agentlink-hook/src/main.rs`, and `libc::kill` in
`agentlinkd/src/keepawake.rs`.

- **RS-26. `proto`, `keys`, `delta` and `agentlink` forbid unsafe** with `#![forbid(unsafe_code)]`
  in their crate root (`tests/` may use `libc` for timing). `planned: phase 3`
- **RS-27. In `agentlinkd` and `agentlink-hook`, unsafe lives only in OS-binding modules**, which
  carry `#![expect(unsafe_code, reason = "…")]` at the top of the module. `planned: phase 3`
  (workspace `unsafe_code = "deny"`)
- **RS-28. Every `unsafe` block has a `// SAFETY:` comment just above it** that says why the call
  is sound. Every `unsafe fn` has a `# Safety` doc section. `planned: phase 3`
  (`clippy::undocumented_unsafe_blocks`)
- **RS-29. Wrap unsafe in a small safe function**, so callers never write `unsafe` themselves.
  `review`

## 7. Docs

- **RS-30. Every library crate starts `lib.rs` with `//!` crate docs**: one sentence saying what the
  crate is and where it sits, then a short example (C-CRATE-DOC). `proto`, `keys` and `delta`
  already have crate docs. `review`
- **RS-31. Every `pub` item in a library crate has `///` docs.** The first line is a summary of
  about 15 words or fewer (M-FIRST-DOC-SENTENCE), and the comment says what the code does not.
  `planned: phase 4` (`missing_docs = "warn"` on library crates)
- **RS-32. A `pub fn` that returns `Result` has `# Errors`, and one that can panic has
  `# Panics`.** `unsafe fn` has `# Safety`. `planned: phase 3` (pedantic `missing_errors_doc`,
  `missing_panics_doc`; `missing_safety_doc` is already on)
- **RS-33. Examples in docs are doc tests that compile and run, and use `?` instead of `unwrap`**
  (C-QUESTION-MARK). `checked: cargo test --workspace` (runs doc tests)
- **RS-34. Link related items with intra-doc links** (`` [`Envelope::from_bytes`] ``) rather than
  bare names (C-LINK). `review`; `planned: phase 4` (`cargo doc` with
  `-D rustdoc::broken_intra_doc_links`)
- **RS-35. No design essays or work logs in doc comments.** Design goes in `docs/design/`, and
  history goes in commits and issues (M-NO-META-DESIGN-DOCUMENTATION). `review`

## 8. Planned lint table

`planned: phase 3, lands with a baseline.` This goes into `daemon/Cargo.toml`. Each crate opts in
with `[lints] workspace = true`. It is Microsoft's
[M-STATIC-VERIFICATION](https://microsoft.github.io/rust-guidelines/guidelines/universal/) table, plus
our panic, unsafe and doc lints, and minus the pedantic lints that fight this codebase. Lints that
have existing hits run on changed lines only until their count reaches zero
([README](README.md#how-a-rule-becomes-enforced)).

```toml
[workspace.lints.rust]
ambiguous_negative_literals = "warn"
missing_debug_implementations = "warn"
redundant_imports = "warn"
redundant_lifetimes = "warn"
trivial_numeric_casts = "warn"
unsafe_op_in_unsafe_fn = "warn"
unused_lifetimes = "warn"
unsafe_code = "deny"            # OS-binding modules opt out with #![expect(unsafe_code, reason = "…")]
# missing_docs: set per library crate with #![warn(missing_docs)] in lib.rs (phase 4); a workspace
# setting would also hit the three binaries, whose items are not an API.

[workspace.lints.clippy]
# groups, overridden by the single lints below
all = { level = "warn", priority = -1 }
pedantic = { level = "warn", priority = -1 }
cargo = { level = "warn", priority = -1 }
# pedantic lints that cost more than they give here
module_name_repetitions = "allow"   # existing names; RS-4 covers new ones in review
must_use_candidate = "allow"
missing_fields_in_debug = "allow"
cast_possible_truncation = "allow"  # wire and pixel maths; we review casts instead
cast_sign_loss = "allow"
cast_precision_loss = "allow"
cast_possible_wrap = "allow"
too_many_lines = "allow"            # function size is a review rule, not a lint, for Rust
similar_names = "allow"
# cargo lints that do not fit an unpublished workspace
cargo_common_metadata = "allow"
multiple_crate_versions = "allow"
# panics (RS-19, RS-23); tests are exempt through daemon/clippy.toml
unwrap_used = "warn"
expect_used = "warn"
todo = "deny"
unimplemented = "deny"
dbg_macro = "warn"
# unsafe (RS-28)
undocumented_unsafe_blocks = "warn"
unnecessary_safety_comment = "warn"
unnecessary_safety_doc = "warn"
# overrides must say why (RS-21)
allow_attributes = "warn"
allow_attributes_without_reason = "warn"
# Microsoft's restriction picks
as_pointer_underscore = "warn"
assertions_on_result_states = "warn"
clone_on_ref_ptr = "warn"
deref_by_slicing = "warn"
disallowed_script_idents = "warn"
empty_drop = "warn"
empty_enum_variants_with_brackets = "warn"
empty_structs_with_brackets = "warn"
fn_to_numeric_cast_any = "warn"
if_then_some_else_none = "warn"
map_err_ignore = "warn"
redundant_type_annotations = "warn"
renamed_function_params = "warn"
semicolon_outside_block = "warn"
unneeded_field_pattern = "warn"
unused_result_ok = "warn"
too_long_first_doc_paragraph = "warn"
literal_string_with_formatting_args = "allow"
```

Notes:

- `nursery` stays off, because its lints are still being developed.
- `restriction` is never enabled as a group (the clippy README says so). Lints from it are picked
  one at a time, as above.
- CI runs clippy without `--all-targets` today, so tests and examples are not linted. Phase 3 adds
  `--all-targets`, which is safe because `clippy.toml` exempts tests from the panic lints.
- Each `#[allow]` that exists today (8 of them) becomes an `#[expect(..., reason = "…")]` when
  `allow_attributes` lands.

## Where the code stands today

Measured 2026-11-22 on branch `conventions` with rustc/clippy 1.95.

**How measured.** We ran `cargo clippy --workspace --message-format=short -- -W clippy::unwrap_used
-W clippy::expect_used -W clippy::panic -W clippy::todo -W clippy::unimplemented
-W clippy::indexing_slicing -W missing_docs -W clippy::undocumented_unsafe_blocks
-W clippy::missing_errors_doc -W clippy::missing_panics_doc`, with `CARGO_TARGET_DIR` set to a
scratch directory, and counted unique `file:line:col` warnings per crate and lint. This is CI's
invocation, without `--all-targets`, so `#[cfg(test)]` modules, `tests/` and `examples/` are left
out, which is exactly the scope of RS-19. Build scripts are included (`agentlinkd/build.rs`
accounts for 2 of its unwraps). As a cross-check, a grep of `.unwrap()` / `.expect(` in `src/`
up to each file's first `#[cfg(test)]` gives similar totals (105 / 11). It overcounts in
`agentlink`, where some test helpers sit outside the test module. `unsafe` was counted by grep
for `unsafe {` and `unsafe … fn`.

| Crate | `unwrap_used` | `expect_used` | `indexing_slicing` (index + slice) | `missing_docs` | `missing_errors_doc` | `missing_panics_doc` | `unsafe` blocks | `undocumented_unsafe_blocks` |
|---|--:|--:|--:|--:|--:|--:|--:|--:|
| `agentlink-proto` | 0 | 0 | 7 (4 + 3) | 23 | 2 | 0 | 0 | 0 |
| `agentlink-keys` | 2 | 0 | 55 (54 + 1) | 35 | 2 | 1 | 0 | 0 |
| `agentlink-delta` | 1 | 1 | 37 (23 + 14) | 1 | 1 | 0 | 0 | 0 |
| `agentlink-hook` | 0 | 0 | 30 (28 + 2) | n/a (bin) | 0 | 0 | 1 | 0 |
| `agentlink` | 0 | 3 | 132 (124 + 8) | n/a (bin) | 0 | 0 | 0 | 0 |
| `agentlinkd` | 105 | 9 | 163 (129 + 34) | n/a (bin) | 0 | 0 | 15 + 1 `unsafe fn` | 1 |
| **Total** | **108** | **13** | **424** | **59** | **5** | **1** | **17** | **1** |

`clippy::panic`, `clippy::todo` and `clippy::unimplemented` report nothing. The only unsafe block
without a `// SAFETY:` comment is the second `libc::kill` at `agentlinkd/src/keepawake.rs:325`.
`missing_docs` applies only to library crates, because binaries export nothing. The 424 indexing
hits are why RS-24 keeps `indexing_slicing` out of the workspace table.

What this means for phase 3:

- The panic baseline is 121 sites, and 69 of them are `Mutex::lock().unwrap()` in `agentlinkd`
  (RS-22), so it can shrink quickly.
- Library crates can ban unwrap and expect almost at once (4 sites).
- `missing_docs` on the three library crates is 59 items.

Other baseline items:

- No crate depends on `thiserror`. There are two hand-written library errors (`delta`, `keys`) and 27
  `Result<_, String>` functions in `agentlinkd` (RS-15, RS-17).
- `agentlinkd` refers to `agentlink-keys` and `agentlink-delta` by path, not through
  `[workspace.dependencies]` (RS-8).
- One `mod.rs` (`agentlinkd/src/focus/mod.rs`) (RS-10).
- 8 `#[allow]` attributes, of which 3 have a reason comment (RS-21).
- No `daemon/clippy.toml` and no `[workspace.lints]` yet (section 8).
