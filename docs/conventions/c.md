# C conventions

How to write C in `firmware/` and `simulator/`. The board files under `firmware/boards/` are C++ only
because the display driver (M5GFX) is C++; they follow this page wherever a C rule applies.
Vendored and generated code keeps its upstream style and is out of scope: LVGL,
`core/third_party/detools`, the zcbor files and `agentlink_codec_*` in `agentlink_proto`, and the
font tables in `core/src/fonts`.

Every rule ends with how it is enforced: `checked: <tool>`, `review`, or `planned: phase N` (phases
are listed in [README.md](README.md#phases)). Rules that are not yet checked apply to new and changed
code. Existing violations are counted in a baseline (see [Where the code stands](#where-the-code-stands-today))
and fixed when the code around them is touched.

## 1. Base style

We follow the [ESP-IDF Style Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/contribute/style-guide.html):
4-space indent and no tabs, snake_case, function braces on their own line, control braces on the same
line, a space after `if`/`for`/`while`/`switch`. LVGL, the library we call most, has the same shape.
Our deltas:

- **C-STY-01** Lines are at most 100 columns (ESP-IDF allows about 120). `planned: phase 1`
  (clang-format `ColumnLimit: 100`; `.editorconfig` already shows the limit).
- **C-STY-02** The `*` binds to the name: `char *p`, `const lv_font_t *font`, `void *ctx`. Never
  `char* p` or LVGL's `char * p`. `planned: phase 1` (clang-format `PointerAlignment: Right`).
- **C-STY-03** Comments use `/* */` only, never `//`. One comment style is easier to grep and matches
  LVGL and Zephyr. `planned: phase 1` (a `scripts/check` grep on changed lines).
- **C-STY-04** Every `if`/`else`/`for`/`while`/`do` body has braces, even a one-statement body
  (MISRA C:2012 Rule 15.6). A missing brace is how a second statement ends up outside the branch.
  `planned: phase 1` (clang-format `InsertBraces: true`).
- **C-STY-05** A function definition puts its opening brace on its own line, as ESP-IDF does. Today's
  one-line definitions (`uint32_t hal_retained_get(void) { return s_retained; }`) go in the reformat.
  `planned: phase 1` (clang-format `AllowShortFunctionsOnASingleLine: None`).

None of ESP-IDF, FreeRTOS or LVGL use clang-format (they use Astyle or Uncrustify), so phase 1 writes
a `.clang-format` that reproduces the ESP-IDF look instead of copying an upstream file. The planned
starting point:

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
BreakBeforeBraces: Linux        # function braces on their own line, others attached
PointerAlignment: Right
InsertBraces: true
AllowShortFunctionsOnASingleLine: None
AllowShortIfStatementsOnASingleLine: Never
IncludeBlocks: Regroup          # with IncludeCategories in the C-MOD-05 order
SortIncludes: CaseSensitive
```

The reformat lands as one commit listed in `.git-blame-ignore-revs`.

## 2. Naming

The names below are what the code already does; the rules write them down. Counts of the existing
exceptions are in the table at the end.

- **C-NAME-01** Every non-`static` symbol starts with its module's prefix, and the prefix is the file's
  root name: `power.c` exports `power_*`, `ui_home.c` exports `ui_home_*`, `hal.h` declares `hal_*`.
  Prefixes in use: `hal_` (the HAL, implemented by `boards/*` and `simulator/src/sim_*.c`), `core_`
  (the component API in `core/include/core.h`), `ui_` and `ui_<screen>_`, `model_`, `link_`, `nav_`,
  `keys_`, `detail_`, `feedback_`, `gauges_`, `voice_`, `power_`, `ota_`, `gesture_`, `bench_`,
  `graph_`, `motion_`, `prefs_`, `steps_`, `done_`, `sha256_`, `ring_`, `sim_`, and `sw_` for
  board-private functions shared between StopWatch files. `checked: review`; `planned: phase 3`
  (clang-tidy `readability-identifier-naming`, `FunctionCase: lower_case`, prefix by review).
- **C-NAME-02** Anything used in one file is `static`. File-scope variables, `const` tables included,
  are named `s_<name>`: `static hal_power_state_t s_power_state`. The one exception is ESP-IDF's log
  tag, `static const char *TAG = "sw-board";`. No `g_`, `k` or Hungarian prefixes. `planned: phase 3`
  (clang-tidy `GlobalVariablePrefix: s_` and `GlobalConstantPrefix: s_`: a file-scope `static` is a
  *global* to clang-tidy, and `StaticVariable` covers only function-local statics; see
  [tooling.md](tooling.md)).
- **C-NAME-03** Types are snake_case with `_t`: `hal_board_info_t`, `ring_t`. Function-pointer
  typedefs end in `_cb_t` when they are callbacks (`hal_button_cb_t`, `gesture_cb_t`) and in `_fn_t`
  otherwise. Type names carry the module prefix too (`hal_ota_result_t`, not `ota_result_t`).
  `planned: phase 3` (`TypedefSuffix: _t`).
- **C-NAME-04** Enums are typedef'd. Enum constants are UPPER_CASE and start with the type's name
  without `_t`, or with the module prefix: `HAL_BTN_A` for `hal_button_t`, `DETAIL_NONE` for
  `detail_state_t`. `planned: phase 3` (`EnumConstantCase: UPPER_CASE`; the prefix by review).
- **C-NAME-05** Macros are UPPER_CASE with the module prefix (`UI_MOTION_RISE`, `BENCH_MAX_RECS`).
  `planned: phase 3` (`MacroDefinitionCase: UPPER_CASE`).
- **C-NAME-06** Files are lowercase snake_case and named after their module: `<module>.c` with
  `<module>.h`; screens are `ui_<screen>.c`; simulator HAL files are `sim_<part>.c`; tests are
  `simulator/tests/test_<module>.c`. A header shared by several files of one module and private to it
  is `<module>_internal.h` (`link_internal.h`, `ui_ring_internal.h`). `checked: review`.
- **C-NAME-07** FreeRTOS task entry functions end in `_task` (`link_task`, `imu_task`) and interrupt
  handlers end in `_isr` (`btn_isr`, `te_isr`), from BARR-C. Every task and ISR in the tree already
  does this. `checked: review`.
- **C-NAME-08** Static event handlers, such as LVGL event callbacks and protocol message handlers,
  are named `on_<event>` (`on_delete`, `on_attention`). `checked: review`.

## 3. Module structure

From [BARR-C:2018](https://barrgroup.com/embedded-systems/books/embedded-c-coding-standard) chapter
4 and the ESP-IDF build system:

- **C-MOD-01** One module is one `.c` with one `.h` of the same root name. A large module may split
  its `.c` files (`link.c`, `link_rx.c`, `link_tx.c`) and share a `<module>_internal.h`. `checked: review`.
- **C-MOD-02** A component's public API lives in its `include/` directory (`core/include/core.h`,
  `agentlink_hal/include/hal.h`). Headers in `src/` are private to the component; ESP-IDF's
  `PRIV_INCLUDE_DIRS` keeps them off other components' include paths. `checked: build`
  (ESP-IDF include isolation).
- **C-MOD-03** A header holds only what other files need: prototypes, the types they take, and the
  macros callers use. Struct layouts that callers do not dereference stay in the `.c` behind an
  opaque `typedef struct x x_t;` (MISRA C:2012 Dir 4.8). Every header starts with `#pragma once`
  (all 44 headers do; there are no `#ifndef` guards). A header that C++ board files include
  (`hal.h`) wraps its declarations in `extern "C"`. `checked: review`.
- **C-MOD-04** No mutable `extern` variables. Share state through get/set functions on a `static`,
  as LVGL does. An `extern const` table is allowed. `checked: review`; `planned: phase 3`.
- **C-MOD-05** Includes come in this order, one blank line between groups: the module's own header
  first (proves it is self-contained), then the C standard library, POSIX, ESP-IDF and FreeRTOS,
  LVGL and other third-party headers, other components' public headers (`hal.h`, `core.h`), and
  this component's private headers. `planned: phase 1` (clang-format `IncludeCategories`).
- **C-MOD-06** No `#include` path contains `..`; it would bypass the include isolation of C-MOD-02.
  There are none today. `planned: phase 2` (the structure check in [structure.md](structure.md)).
- **C-MOD-07** `core/` includes only `hal.h`, LVGL and the protocol component, never a board header
  (rule stated in `hal.h`). `checked: build` (core does not `REQUIRES` a board); `planned: phase 2`.

## 4. Error model

Every module returns `esp_err_t`-style codes, including the modules that also build in the
simulator (`core`, the UI, `agentlink_proto`). A function that can fail returns `esp_err_t`; results
go out through pointer parameters. A pure predicate (`bool ui_home_askable(void)`) still returns
`bool`, and a getter that cannot fail returns its value.

**How core gets `esp_err_t` (planned).** On the watch, `esp_err.h` comes from ESP-IDF's `esp_common`,
which every component can include. In the simulator nothing defines `esp_err_t` today. Core, the
simulator HAL and the tests contain no `esp_err_t`, `ESP_OK` or `esp_err.h` at all. The plan is a
small `simulator/src/esp_err.h` shim with the same names and values. `simulator/src` is already a
`PUBLIC` include directory of the simulator's `agentlink_hal` target, which `core` links, so core
code writes `#include "esp_err.h"` once and gets the real header on the watch and the shim on the
desktop. The shim defines `esp_err_t` as `int`, `ESP_OK` (0), `ESP_FAIL` (-1), the generic
`ESP_ERR_*` codes with ESP-IDF's numbers (`ESP_ERR_NO_MEM` 0x101, `ESP_ERR_INVALID_ARG` 0x102,
`ESP_ERR_INVALID_STATE` 0x103, `ESP_ERR_INVALID_SIZE` 0x104, `ESP_ERR_NOT_FOUND` 0x105,
`ESP_ERR_NOT_SUPPORTED` 0x106, `ESP_ERR_TIMEOUT` 0x107), and `esp_err_to_name()`. Codes of our own
get one base per component, picked from a range ESP-IDF does not use, and are named
`<PREFIX>_ERR_<NAME>`. The shim lands with the first module that migrates.

- **C-ERR-01** A function that can fail returns `esp_err_t`, with `ESP_OK` on success. Use a generic
  `ESP_ERR_*` code when one fits. Do not add new `bool`-for-success returns, sentinel `int`s or
  "last error" strings. `review`.
- **C-ERR-02** Check every returned error. Either handle it, return it, or discard it on purpose
  with a `(void)` cast and a comment that says why (MISRA C:2012 Rule 17.7, Dir 4.7; CERT ERR33-C;
  Power of 10 rule 7). `planned: phase 3` (clang-tidy `bugprone-unused-return-value`, which is
  `cert-err33-c`).
- **C-ERR-03** Return early on error. No single-exit rule (we skip BARR-C 6.2.c), no `goto` except
  to one cleanup label at the end of the function. ESP-IDF's `ESP_RETURN_ON_ERROR` is fine in
  firmware-only code. `review`.

  ```c
  esp_err_t err = hal_ota_begin(size);
  if (err != ESP_OK) {
      return err;
  }
  (void)hal_link_send(port, buf, len); /* best effort: the daemon re-asks on timeout */
  ```

- **C-ERR-04** `ESP_ERROR_CHECK()` aborts the device, so it is used only during board bring-up,
  where a failure means the hardware is unusable (`board.cpp` uses it three times). Never in core.
  `review`.

Existing exceptions, migrated when touched: `hal_ota_result_t` (`HAL_OTA_OK`/`_BAD`/`_FAIL`/
`_UNSUPPORTED`) with `hal_ota_error()`, `bool hal_mic_start()` with `hal_mic_error()`, and
`bool` success returns such as `ring_push()`.

## 5. Safety rules

Ten rules picked from [MISRA C:2012](https://misra.org.uk/), [SEI CERT C](https://wiki.sei.cmu.edu/confluence/display/c/SEI+CERT+C+Coding+Standard)
and [The Power of 10](https://spinroot.com/gerard/pdf/P10.pdf), restated in our own words in the way
the [Zephyr coding guidelines](https://docs.zephyrproject.org/latest/contribute/coding_guidelines/index.html)
do. MISRA's texts are paid and may not be copied; the IDs are for cross-reference only. Power of 10
rule 9 (no function pointers) is out: the HAL callbacks and LVGL events need them.

- **C-SAFE-01 No heap after init.** Allocate at start-up, or at the start of a bounded session that
  frees it at the end, and say so in a comment at the call. Everything else uses static storage or
  the ring buffers in `ring.h` (MISRA Dir 4.12, Rule 21.3; Power of 10 rule 3). The code is not
  there yet. Core allocates at run time in `ota.c` (the delta patch buffer, freed when the transfer
  ends) and `bench.c` (the record buffer, on first use). `ui_ring.c`, `ui_wheel.c` and `ui_kit.c`
  `lv_malloc_zeroed()` a block per widget, and LVGL allocates every object from its own pool. The
  boards allocate DMA buffers at init and the screenshot and mic buffers on first use. Simulator-only
  code (`sim_*.c`, tests) is exempt. `review`; `planned: phase 3` (a check that lists allowed
  allocation sites as a baseline).
- **C-SAFE-02 Bounded loops.** Every loop has a bound a reader can see: a counter, a buffer length,
  or a timeout. `for (;;)` is allowed only as the body of a `_task` function (Power of 10 rule 2).
  `review`.
- **C-SAFE-03 No recursion.** Stack depth must be known at build time, because each FreeRTOS task
  has a fixed stack (MISRA Rule 17.2; Power of 10 rule 1). A direct-recursion scan finds none today.
  `planned: phase 3` (clang-tidy `misc-no-recursion`).
- **C-SAFE-04 Fixed-width integers.** Use `uint8_t` to `uint64_t` and `int8_t` to `int64_t` for
  data whose size matters (protocol fields, registers, retained words, times), `size_t` for sizes
  and indices, `bool` for flags. Plain `int` is fine for small loop counters and LVGL coordinates
  (MISRA Dir 4.6; BARR-C 5.2). Times carry their unit in the name (`now_ms`, `t_us`). `review`.
- **C-SAFE-05 Every `switch` has a `default`, and no case falls through by accident.** Mark an
  intended fall-through with `__attribute__((fallthrough));` (MISRA Rules 16.3, 16.4; CERT MSC17-C).
  `checked: GCC -Wextra` (warns on implicit fall-through, does not fail the build); `planned:
  phase 3` (`-Werror=implicit-fallthrough`, clang-tidy `bugprone-switch-missing-default-case`).
- **C-SAFE-06 Buffers travel with their length.** A function that takes a buffer also takes its
  size, and writes stay inside it. Use `snprintf` (247 uses today), never `sprintf`, `strcpy` or
  `strcat`. Prefer indexing to pointer arithmetic (MISRA Rules 18.1, 18.4, 21.17; CERT ARR30-C,
  STR31-C). `planned: phase 3` (clang-tidy `bugprone-unsafe-functions`,
  `bugprone-not-null-terminated-result`, `bugprone-sizeof-expression`).
- **C-SAFE-07 `const` where it can be.** A pointer parameter that is only read is `const`, and
  lookup tables are `static const` so they stay in flash (MISRA Rule 8.13). `planned: phase 3`
  (clang-tidy `readability-non-const-parameter`).
- **C-SAFE-08 Compile clean.** New code adds no warnings at `-Wall -Wextra` in either build. Today
  `core` and `agentlink_sim` build with `-Wall -Wextra` in the simulator, while the simulator's HAL
  files and the tests do not. `checked: review` (warnings show, nothing fails); `planned: phase 3`
  (the flags on every first-party target, then `-Werror` once the count is zero).
- **C-SAFE-09 Assertions for invariants only.** Use `assert()` for "cannot happen" conditions, never
  to handle input from the link, the user or the hardware, which goes through C-ERR. Use
  `_Static_assert` for size and layout facts (`power_report.c` checks that its figures fit the RTC
  words) (Power of 10 rule 5; CERT MSC11-C). `review`.
- **C-SAFE-10 `static inline` over function-like macros.** A function-like macro is allowed only
  when a function cannot do the job (stringizing, `sizeof` of an argument). Any macro kept has every
  parameter and the whole body in parentheses (MISRA Dir 4.9; BARR-C 6.3). `review`; `planned:
  phase 3` (clang-tidy `bugprone-macro-parentheses`).
- **C-SAFE-11 One thread touches LVGL.** HAL callbacks may run in an ISR or a driver task. They copy
  their data into a queue or `ring_t` and return, and they never call LVGL or block (CERT CON43-C;
  rule stated in `hal.h`). `review`.
- **C-SAFE-12 No commented-out code.** Delete it; git keeps it (MISRA Dir 4.4). `review`.

## 6. Documentation

Doc comments follow [ESP-IDF's Documenting Code](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/contribute/documenting-code.html)
(Doxygen with `@` commands) and the content rules of
[Zephyr's Doxygen guide](https://docs.zephyrproject.org/latest/contribute/style/doxygen.html). No
header uses Doxygen today; the existing `/* */` comments above prototypes become `/** */` blocks as
headers are touched.

- **C-DOC-01** Every public header starts with a file comment: what the module is for and the rules
  callers must know. `hal.h` and 41 other headers already have one. In Doxygen form it is
  `/** @file @brief ... */`. `review`; `planned: phase 4` (Doxygen on public headers).
- **C-DOC-02** Every function declared in a public header has a `/** */` block. Its `@brief` is one
  imperative sentence. Each parameter gets `@param[in|out|in,out]` in declaration order and says its
  unit, valid range, and whether it may be NULL or is kept after the call. Each discrete error code
  gets its own `@retval`, success first, and other return values get `@return`. `planned: phase 4`
  (Doxygen `WARN_IF_UNDOCUMENTED`, `WARN_NO_PARAMDOC`, `WARN_AS_ERROR` over the public headers;
  candidate Doxyfile and today's coverage in [tooling.md](tooling.md)).
- **C-DOC-03** Every HAL function, and every function that may be called from an ISR or from a task
  other than its owner's, has a `Context:` line that names where it may be called (`any task`,
  `core task only`, `ISR-safe`) and whether it blocks. `review`.
- **C-DOC-04** Struct and enum members carry a trailing `/**< ... */` comment, which is how
  `hal_board_info_t` and `hal_ota_result_t` already annotate theirs, in `/* */` form. `planned: phase 4`.
- **C-DOC-05** Comments say why. A comment that restates the code is noise. `review`.

A good example, adapted from `hal_ota_write()` in `hal.h` and written with the `esp_err_t` return of
section 4:

```c
/**
 * @brief Append the next bytes of a firmware image to the spare app slot.
 *
 * Bytes arrive in order from offset 0. The first bytes are checked for an app image header.
 * Context: core task only. May block for a flash erase (tens of ms).
 *
 * @param[in] data Image bytes. Must not be NULL. Not kept after the call returns.
 * @param[in] len  Number of bytes in @p data.
 *
 * @retval ESP_OK                The bytes were written.
 * @retval ESP_ERR_INVALID_ARG   The image is refused (too big, or not an app for this chip).
 * @retval ESP_ERR_INVALID_STATE No transfer is open; call hal_ota_begin() first.
 * @retval ESP_ERR_NOT_SUPPORTED This board cannot update itself.
 * @retval ESP_FAIL              The flash write failed. The transfer is closed; see hal_ota_error().
 */
esp_err_t hal_ota_write(const uint8_t *data, size_t len);
```

## 7. Function size

- **C-SIZE-01** A function body is at most 60 lines. Past that, split out the steps as `static`
  helpers that have names. The limit is Power of 10's "one printed page". Functions that are already
  longer go in a baseline and are flagged only when a change touches them. `planned: phase 3`
  (clang-tidy `readability-function-size` with `LineThreshold: 60`, diff-only, baseline file).

## Where the code stands today

Measured on 2026-11-22 over first-party code only: 121 C files (`.c`/`.h`) and 13 C++ board files,
28,999 lines in total (25,836 C, 3,163 C++). Vendored LVGL, `third_party/`, zcbor and
`agentlink_codec_*`, the font tables, `simulator/lv_conf.h` and build directories are excluded.

| Measure | Count | How measured |
|---|---|---|
| `//` comment lines (C-STY-03) | 71: 3 in C (two board headers), 68 in `.cpp` | Tokenizer that skips string literals and `/* */` |
| Lines over 100 columns (C-STY-01) | 2,707 (284 over 120): 2,247 firmware C, 349 simulator, 111 `.cpp` | Characters per line after tab expansion |
| Functions over 60 lines (C-SIZE-01) | 28 of 1,563; 10 over 100 | [lizard](https://github.com/terryyin/lizard) 1.26.1, signature to closing brace |
| ...by place | 15 core, 4 `simulator/src`, 8 test `main()`s, 1 board | Longest: `parse_line` in `sim_script.c` (281), `link_rx_dispatch` (151), `ui_on_gesture` (145) |
| Pointer declarations `T *p` / `T * p` / `T* p` (C-STY-02) | 2,333 / 0 / 0 | Regex on code with strings and comments blanked, after known type names |
| One-line function definitions (C-STY-05) | 312 C | lizard, length 1 |
| Brace-less one-statement bodies (C-STY-04) | about 1,356 lines | Rough grep for `if/for/while (...) stmt;` |
| Headers with `#pragma once` / `#ifndef` guards | 44 / 0 | grep |
| Doxygen `/**` blocks in headers (C-DOC-02) | 0 | grep |
| `esp_err_t` in core, simulator, tests (C-ERR-01) | 0 | grep; boards use it for ESP-IDF calls |
| Warnings, simulator build (C-SAFE-08) | 2, both `-Wformat-truncation` in `sim_script.c` | Host GCC 13 build of every target; firmware build not measured |
| `assert()` / `_Static_assert` (C-SAFE-09) | 0 / 1 | grep |
| `sprintf`, `strcpy` / `strcat` / `strtok` / `atoi` (C-SAFE-06) | 0 / 1 / 2 / 14 | grep |

Existing naming and structure exceptions, fixed when touched:

| Rule | Exceptions |
|---|---|
| C-NAME-01 prefix = file root | 87 public functions: `theme_*` in `ui_theme.c` (31), `ring_*` in `ui_ring.c` and `ui_ring_draw.c` (15, which also clashes with the FIFO `ring_*` in `ring.c`), `preport_*` in `power_report.c` (11), `dbg_*` in `debug.c` (9), `wheel_*` in `ui_wheel.c` (9), `snd_*` in `sound.c` and `sound_bank.c` (5), `ws_*` in `sim_ws.c` (5), `sess_state_name` and `agent_name` in `model.c` (2). `core_*` is defined in `app.c` (9); the file should become `core.c` |
| C-NAME-02 `s_` statics | Of 627 file-scope statics, 482 use `s_` and 12 are `TAG`. The rest: 76 lowercase `static const` tables (72 in `sound_bank.c`), 26 `kCamel` constants (9 C, 17 `.cpp`), 26 in tests (`failures`, `g_*`), 3 mutable lowercase (`debug.c` `active`, `motion.c` `s`, `sim_display.c` `down`), and 2 others |
| C-NAME-03 `_t` types | 3 function-pointer typedefs end in `_fn` (`detail_send_fn`, `done_measure_fn`, `ring_used_fn`). 42 of 103 header typedefs lack the module prefix, mostly the domain types in `model.h` (`session_t`, `attention_t`, `agent_t`) and `nav.h` |
| C-NAME-04 enum prefixes | All 334 constants of the 47 enums are UPPER_CASE. 14 enums use a short prefix instead of the module's: `G_PRESS`, `GK_`/`GS_`/`GL_` (graph), `MODE_`/`ZONE_`/`WHEN_`/`SCROLL_` (nav), `ACT_`, `PM_`, `PRS_`, `AG_`, `ST_`, `E_` |
| C-NAME-05 macros | None: all 935 `#define`s are UPPER_CASE. 36 are function-like (C-SAFE-10) |
| C-MOD-01 own header first | Of 62 non-test `.c` files, 34 have a same-name header and 25 of those include it first. 28 have none by design: the screens share `ui_internal.h`, `sim_*.c` implement `hal.h`, `link_rx.c`/`link_tx.c` share `link_internal.h` |
| C-MOD-04 extern globals | 4 mutable: `g_link`, `g_rx_env`, `g_tx_env` (`link_internal.h`), `g_ui_actions` (`ui_internal.h`). 6 `extern const` tables are allowed |
| C-NAME-07 `_task`/`_isr` | None: all 6 task functions and 3 ISRs follow it |
