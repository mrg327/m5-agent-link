# Tooling experiments

What we measured before choosing the C checkers, so phase 1 and phase 3 start from facts. Issue
20261122-db53, measured 2026-11-22 on the simulator build and the ESP32-S3 firmware build. Numbers are
for first-party hand-written C (103 files, about 23,400 lines; fonts, `third_party/`, zcbor and the
generated `agentlink_codec_*` files excluded).

## 1. clang-format

ESP-IDF ships no `.clang-format`; it formats with astyle `--style=otbs` (4 spaces, no column limit),
which is clang-format's `BreakBeforeBraces: Linux`. Our code already matches it: 4 spaces, `char *p`
(1,290 to 0), function braces on their own line, `} else {`, unindented `case`, no `//`. It is dense:
1,142 one-line `if`s, and 2,022 lines over 100 columns.

| Config | Files changed | Lines changed |
|---|---|---|
| LLVM base with our deltas, 100 columns | 95 | 3,498 |
| Tuned, no column limit | 59 | 323 |
| **Tuned, 100 columns (candidate)** | **95** | **2,856** (about 12 %) |
| Candidate with `ReflowComments: false` | 86 | 1,731, but 969 lines stay over 100 |

Most of the churn is the 100-column limit itself. The 323-line floor is multi-statement one-line blocks
(`if (p) { a; b; }`), which clang-format always splits. The candidate needs clang-format 20 or later
(`AllowShortRecordOnASingleLine`); the PyPI wheel (`uvx --from clang-format`, 23.x) works without root.

Candidate, to land in phase 1:

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ContinuationIndentWidth: 4
UseTab: Never
ColumnLimit: 100
PointerAlignment: Right
DerivePointerAlignment: false
BreakBeforeBraces: Linux
IndentCaseLabels: false
AllowShortIfStatementsOnASingleLine: AllIfsAndElse
AllowShortCaseLabelsOnASingleLine: true
AllowShortLoopsOnASingleLine: true
AllowShortBlocksOnASingleLine: Always
AllowShortFunctionsOnASingleLine: All
AllowShortEnumsOnASingleLine: true
AllowShortRecordOnASingleLine: Always
AlignAfterOpenBracket: Align
AlignTrailingComments: {Kind: Leave}
AlignEscapedNewlines: Left
SortIncludes: Never
IncludeBlocks: Preserve
SpaceBeforeParens: ControlStatements
ReflowComments: true
MaxEmptyLinesToKeep: 2
InsertBraces: false
```

## 2. clang-tidy on the simulator's compile database

The simulator build (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) covers 60 of the 61 first-party `.c`
files; `firmware/main/main.c` needs the firmware database (§3). clang-tidy comes from the PyPI wheel
(`uvx --from clang-tidy`, LLVM 23.1.4); pin the version.

**Naming.** `readability-identifier-naming` expresses most of [c.md](c.md) §2 directly:

| Rule | Option |
|---|---|
| UPPER_CASE macros and enum constants | `MacroDefinitionCase`, `EnumConstantCase: UPPER_CASE` |
| `_t` types | `TypedefCase: lower_case`, `TypedefSuffix: _t` |
| lower_snake locals, parameters, members | `LocalVariableCase`, `ParameterCase`, `MemberCase` |
| `s_` file-scope statics | `GlobalVariablePrefix: s_`, `GlobalConstantPrefix: s_` |
| externs keep `g_` | `GlobalVariableIgnoredRegexp: '^g_[a-z0-9_]+$'` |
| public functions carry a module prefix | `GlobalFunctionPrefix: 'MODULE_'` with `GlobalFunctionIgnoredRegexp: '^(app\|ui\|model\|…)_'` |

- A file-scope `static` counts as a *global* to clang-tidy. `StaticVariable*` covers only
  function-local statics, so `StaticVariablePrefix` alone never flags a bad file-scope name.
- No option tells a file-scope `static` from an extern. `misc-use-internal-linkage` covers the other
  half (a definition that should be `static`) and finds nothing today.
- The module-prefix rule uses a sentinel: every exported name outside the allowlist gets a bogus
  `MODULE_` fix-it, so never run it with `--fix`. An `nm` check of exported symbols against the same
  allowlist is the backstop. The allowlist has about 35 prefixes (881 exported symbols).
- `bugprone-unused-return-value.CheckedReturnTypes: '::esp_err_t'` does not work, because
  `esp_err_t` is a typedef of `int`. Use `CheckedFunctions` with a regex of the module prefixes.

**Cost and noise.**

| Run | Wall time | Warnings |
|---|---|---|
| Broad set (bugprone, cert, misc, readability, analyzer) | 6 min 23 s | 5,266 |
| Candidate | 1 min 58 s | 602 |
| Candidate without clang-analyzer | 22 s | 589 |

The analyzer costs about 80 % of the time for 13 extra warnings: run it on changed files only, or
nightly. Dropped as noise:

| Check | Warnings | Why |
|---|---|---|
| `misc-include-cleaner` | 2,065 | include-what-you-use |
| `insecureAPI` | ~430 | "use `memset_s`" |
| `bugprone-signed-bitwise` | 421 | LVGL flag ORs |
| `cert-err33-c` | 263 | ignored `snprintf` results |
| `bugprone-easily-swappable-parameters` | 120 | `int x, int y` |

Braces: by default `readability-braces-around-statements` flags 1,667 bodies, against 1,142
house-style one-line `if`s. With `ShortStatementLines: 1`, the setting C-STY-04 chose, it flags
390, mostly two-line `if (a) x;` / `else y;` pairs.

The 74 naming warnings the candidate reports are all real (61 sound tables without `s_`, 13
`k`-prefixed constants). The analyzer's two warnings that looked worth a look (`bench.c` with an
empty stroke, the UTF-8 decoder in `ui_kit.c`) are false positives on inspection: the stroke length
is checked on arrival and the decoder stops at the terminator.

Functions over 60 lines, counted with blank lines and comments: 17. The longest are `parse_line`
(279, `sim_script.c`), `link_rx_dispatch` (149), `run` (145, `sim_script.c`), `ui_on_gesture` (143)
and `ui_home_card_update` (133).

Phase 3 plan: land the candidate `.clang-tidy` and run it on changed lines with
`clang-tidy-diff.py`; the 602 current warnings are the baseline.

## 3. `idf.py clang-check` on the firmware

- **Against the normal gcc build: unusable.** Every file fails to parse. clang rejects GCC 14's
  `-mdisable-hardware-atomics` and cannot find newlib's headers.
- **With a separate clang-configured build directory: it works.** Configuring takes about 40 s,
  with no full build. Analysis takes about 100 s and covers `boards/` and `main/`, which the
  simulator does not build. Commands:

  ```sh
  idf.py -C firmware -B firmware/build-clang -DIDF_TOOLCHAIN=clang \
    -DSDKCONFIG=firmware/build-clang/sdkconfig reconfigure
  run-clang-tidy -p firmware/build-clang -quiet 'firmware/(components|boards|main)/'
  ```

- **Gotchas:**
  - Call esp-clang's `run-clang-tidy` directly, not `idf.py clang-check`. That tool forces its own
    `-checks`, rewrites `compile_commands.json` in place, writes an untracked `warnings.txt` into
    `firmware/`, and exits 0 on warnings.
  - One m5gfx header throws with exceptions disabled. Pass `-extra-arg=-fexceptions` for
    `boards/stopwatch/display.cpp`.
- **Plan:** the simulator route is the per-commit check. The firmware route is an opt-in
  `scripts/check firmware` step in phase 4, using the same `.clang-tidy`.

The plain gcc firmware build passes. It shows two warnings, both `-Wvolatile` at
`boards/stopwatch/link_usb.cpp:201` and `:208`. `check_dram.py` reports 185 KB of internal RAM
free, against a floor of 100 KB.

## 4. Doxygen and documentation coverage

Doxygen is not installed on the dev machine, and there is no PyPI wheel. Phase 4 needs it from the
system package manager or a pinned release binary. None of the three first-party public headers has a
`/**` comment yet:

| Header | Functions | `/** */` | `/* */` | None |
|---|---|---|---|---|
| `firmware/components/agentlink_hal/include/hal.h` | 50 | 0 | 39 | 11 |
| `firmware/components/core/include/core.h` | 9 | 0 | 8 | 1 |
| `firmware/boards/stopwatch/sw_power/include/sw_power.h` | 7 | 0 | 6 | 1 |

Turning the 53 plain comments into `/** @brief … */` is mechanical. The 13 functions with no comment
need one written, and `sw_power.h` has two `//` comments. A run with `WARN_AS_ERROR` would fail until
that is done, so the coverage counts are the baseline: `/**` comments may only go up, and undocumented
functions only down. Candidate Doxyfile (paths from the repository root):

```text
INPUT = firmware/components/core/include firmware/components/agentlink_hal/include \
        firmware/boards/stopwatch/sw_power/include
FILE_PATTERNS = *.h
EXCLUDE_PATTERNS = */zcbor_*.h */agentlink_codec_*.h
OUTPUT_DIRECTORY = build/doxygen
OPTIMIZE_OUTPUT_FOR_C = YES
EXTRACT_ALL = NO
WARN_IF_UNDOCUMENTED = YES
WARN_IF_INCOMPLETE_DOC = YES
WARN_NO_PARAMDOC = YES
WARN_AS_ERROR = FAIL_ON_WARNINGS
WARN_LOGFILE = build/doxygen/warnings.log
GENERATE_HTML = NO
GENERATE_LATEX = NO
GENERATE_XML = YES
QUIET = YES
```

With `WARN_LOGFILE` set, warnings go to the file, not stderr, so the check must print the log when it
fails.
