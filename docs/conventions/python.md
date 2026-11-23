# Python

Python here is tooling: code generators, vector validators, end-to-end tests that drive the daemon,
build checks and mock-ups. There is no Python package and no `pyproject.toml`; every file is a
standalone script. Base style is the [Google Python Style Guide](https://google.github.io/styleguide/pyguide.html).
This page lists only our deltas and choices, and how each one is checked.

## Inventory (2026-11-22)

20 files, about 4,000 lines. `requires-python = ">=3.11"` where declared; dev machines run 3.12.

| Directory | Files | How it runs |
|---|---|---|
| `protocol/tools/` | `gen_vectors.py`, `validate_vectors.py` | `uv run` (PEP 723 block, `#!/usr/bin/env -S uv run --script`); CI `protocol.yml` |
| `protocol/fixtures/ota/` | `gen.py` | `uv run` (PEP 723 block, no shebang) |
| `scripts/` | `fetch_docs.py` | `uv run` (PEP 723 block) |
| `daemon/tools/` | nine `*_e2e.py` | `uv run --with cbor2 --with websockets python …` (deps on the command line, no PEP 723 block) |
| `atlas/tools/` | `extract.py` | `python3` (stdlib only); CI `atlas.yml` runs `--check` |
| `firmware/tools/` | `check_dram.py`, `fonts/tabular.py` | `python` inside the ESP-IDF environment (CI `firmware.yml`) |
| `simulator/tools/` | `ram_budget.py` | `python3` from ctest; exit 77 means skipped |
| `daemon/crates/agentlink-keys/data/` | `build_lexicon.py` | `python3`, by hand, to rebuild the lexicon |
| `daemon/crates/agentlinkd/src/focus/` | `kitty_wheel.py` | kitty's own Python (`kitten @ kitten …`); the daemon writes it out |
| `docs/design/glyphs/` | `mock.py` | `python3` with Pillow, by hand |

None of them uses `argparse` yet; they read `sys.argv` directly. No Ruff or mypy config exists.

## Baseline (measured 2026-11-22, Ruff 0.17.6, mypy via `uv run`, all 20 files)

| Check | Result |
|---|---|
| `ruff format --check` | 19 of 20 files would be reformatted (same at 88 or 100 columns) |
| `ruff check --select E4,E7,E9,F` (phase 1 blocking set) | 223: E701 109, E702 81, E401 19, E741 9, E731 3, F541 1, E703 1 |
| `ruff check --select E,F,W,I,D,UP,B`, 88 columns, no convention | 1,234 (E501 802) |
| same, 100 columns, `convention = "google"` | 944: E501 519, E701 109, D103 103, E702 81, D102 36, … |
| same, after a 100-column `ruff format` (run on a scratch copy) | 393: E501 160, D103 103, D102 36, E401 19, D107 13, I001 12, D101 10, D100 10, … |
| `mypy --strict --ignore-missing-imports`, per file | 1,165 errors: `no-untyped-call` 942, `no-untyped-def` 175, rest 48 |

Two things follow. The one-time format (phase 1) removes most of the E7 and E501 findings, so it
comes first. And Ruff 0.17's built-in default rule set is much wider than the old `E4,E7,E9,F`
(131 findings with no config at all, e.g. `SIM115`, `PLW1510`, `ASYNC230`), so we always pass an
explicit `select` and pin the Ruff version.

## Tools

**PY-1. Format with Ruff.** `ruff format` (Black-compatible), 100 columns to match C, double quotes,
4-space indent. The one reformat lands as a single commit listed in `.git-blame-ignore-revs`.
planned: phase 1 (`ruff format --check` blocking).

**PY-2. Lint with Ruff, explicit rule list.** Never `select = ["ALL"]`: new rules would switch on with
every upgrade. Phase 1 blocks on `E4,E7,E9,F` everywhere. The wider list below runs on changed lines
with a per-file, per-rule count baseline that may only shrink; a rule blocks everywhere once its count
is zero. planned: phase 1 (narrow set), phase 3 (full set, baseline).

**PY-3. Type-check with `mypy --strict`.** Each script is checked in its own environment, so imports
from its PEP 723 dependencies resolve:

```sh
uv run --no-project --with mypy --with-requirements protocol/tools/gen_vectors.py \
    mypy --strict protocol/tools/gen_vectors.py
```

New files must pass with zero errors. Existing files carry an error count in the baseline (1,165 today)
that may only shrink. We do not add `ANN` to Ruff: mypy's `disallow_untyped_defs` already covers it.
planned: phase 3.

**PY-4. Pin the tool versions.** `scripts/check` runs `uvx ruff@0.17.6` and a pinned mypy, so every
machine and CI get the same findings. A version bump is its own commit, with any new findings fixed
or baselined in it. planned: phase 1.

### Planned `ruff.toml` (repository root)

A `ruff.toml`, not a `pyproject.toml`: a root `pyproject.toml` would make `uv run` treat the
repository as a Python project. Not created in phase 0.

```toml
# Ruff config for the repository's Python tools. docs/conventions/python.md
required-version = "==0.17.6"
line-length = 100
target-version = "py311"
extend-exclude = ["firmware/managed_components", "**/build", "**/node_modules", "atlas/vendor"]

[format]
quote-style = "double"
docstring-code-format = true

[lint]
select = [
  "E", "W",   # pycodestyle
  "F",        # pyflakes
  "I",        # import order (isort)
  "N",        # pep8-naming: Google naming
  "D",        # docstrings, google convention below
  "UP",       # pyupgrade to 3.11 idioms (X | None, builtin generics)
  "B",        # bugbear: mutable defaults, loop-variable closures
  "SIM",      # simplify; SIM115 = open() without `with`
  "C4",       # comprehensions
  "BLE",      # no blind `except Exception`
  "RUF",      # Ruff's own checks
  "PLW1510",  # subprocess.run without check=
]
ignore = [
  "D105",     # magic methods need no docstring
  "D107",     # document __init__ arguments in the class docstring (Google)
]

[lint.pydocstyle]
convention = "google"

[lint.per-file-ignores]
"**/test_*.py" = ["D"]
```

### Planned `mypy.ini` (repository root)

```ini
# mypy config for the repository's Python tools. docs/conventions/python.md
[mypy]
python_version = 3.11
strict = True
warn_unreachable = True
cache_dir = .mypy_cache

[mypy-kitty.*]
ignore_missing_imports = True
```

## Scripts

**PY-5. One script, one file, with a PEP 723 block.** Declare `requires-python` and every third-party
dependency inline, and make the file runnable as `uv run path/to/tool.py` from the repository root.
Executable scripts use `#!/usr/bin/env -S uv run --script`. This replaces the `uv run --with …`
lines in the `daemon/tools/*_e2e.py` headers when those files are next touched. checked: review.

```python
#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["cbor2>=5.6,<5.7"]
# ///
"""Regenerate protocol/vectors from the CDDL examples."""
```

**PY-6. Bound every dependency.** Pin (`==`) or bound (`>=5.6,<5.7`) each entry so a release cannot
change our output; `scripts/fetch_docs.py` has three unbounded ones today. checked: review.

**PY-7. Host-interpreter scripts say so.** A script that must run under another program's Python
(kitty for `kitty_wheel.py`, the ESP-IDF env for `firmware/tools/`, ctest for `ram_budget.py`) stays
stdlib-only and names its interpreter in the module docstring. checked: review.

**PY-8. `main()` and the `__main__` guard.** Top-level code only defines things. Work happens in
`main`, which returns the exit code. Importing a script must not run it (tests and mypy import it).
checked: review.

```python
def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    ...
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Bad: the e2e scripts' pattern, where checks run at import time and end in a bare
`sys.exit(0 if ok else 1)` at module level.

**PY-9. `argparse` for arguments.** Any script with an option or more than one positional argument
uses `argparse`, with `--help` text a reader can act on. A single optional positional may stay as
`sys.argv[1:]`. checked: review.

**PY-10. Exit codes mean one thing.** `0` success; `1` the check or task failed; `2` bad usage
(argparse's own); `77` skipped, for scripts ctest runs. Use `sys.exit("tool: message")` for a fatal
error: it prints to stderr and exits 1. checked: review.

**PY-11. Checker output is fixable.** A script that checks something prints one line per finding as
`path:line: RULE-ID message - how to fix`, so an agent can correct itself, and exits 1 if any
finding remains. checked: review.

**PY-12. Paths from the repository, not the cwd.** Find files relative to `Path(__file__)` or take
them as arguments. Use `pathlib`, not string joins. Never write a home path, hostname or username
into output that gets committed: this repository is public. checked: review.

**PY-13. `subprocess` without a shell.** Pass a list, not a string with `shell=True`, and pass
`check=True` or test `returncode` yourself. planned: phase 3 (`PLW1510`); review for `shell=True`.

## Code

**PY-14. Naming follows Google.** `lower_with_under` for modules, functions, variables and
parameters; `CapWords` for classes; exceptions end in `Error`; `CAPS_WITH_UNDER` for module
constants; one leading `_` for internals. Module file names never contain `-`. Avoid single-letter
names outside counters, comprehensions and math (`l`, `O`, `I` never: E741). Names that come from the
protocol keep the CDDL spelling (see [protocol.md](protocol.md)). planned: phase 3 (`N`,
`E741`).

**PY-15. Type every function signature.** Parameters and return types on every `def`, including
private helpers. Use `X | None`, builtin generics (`list[str]`) and `collections.abc` for
parameters. Use `Any` only at a JSON or CBOR boundary, and narrow it right away. planned: phase 3
(mypy strict, `UP`).

**PY-16. Errors.** No bare `except:` and no `except Exception` that swallows: catch the narrowest type
and keep `try` blocks short. Do not use `assert` to check inputs; raise `ValueError` or a module
`…Error`. In e2e tests `assert` is fine, since they are tests. planned: phase 3 (`E722`,
`BLE`, `B`); review for `assert`.

**PY-17. No mutable defaults, no closures over loop variables.** Use `None` and create the list in
the body; bind loop values as default arguments or with `functools.partial`. checked: planned:
phase 3 (`B006`, `B008`, `B023`).

**PY-18. One statement per line, one import per line.** No `if x: y`, no `a; b`, no
`import a, b`. Imports are grouped stdlib, third-party, local, and sorted. planned: phase 1
(`E401`, `E701`, `E702`), phase 3 (`I`).

**PY-19. Use `with` for files and sockets.** Never `open(...).read()` without a context manager.
planned: phase 3 (`SIM115`).

**PY-20. Logging, if used, takes `%` arguments.** `log.info("sent %d frames", n)`, not an f-string, so
the string is only built when the level is on. Plain `print` to stdout is fine for scripts.
checked: review (add Ruff `G` if logging spreads).

## Docstrings

**PY-21. Every module, public class and public function has a Google-style docstring.** The module
docstring says what the script does and how to run it; it replaces the `#` comment banners the
e2e scripts start with today. Summary line ends with a period and fits in one line. `Args:`,
`Returns:` (or `Yields:`) and `Raises:` sections follow when they add information; `Returns:` may be
left out when the summary already says it. Classes list `Attributes:` and their `__init__`
arguments. planned: phase 3 (`D`, google convention), blocking in phase 4.

```python
def decode_frame(buf: bytes, *, strict: bool = True) -> Frame:
    """Decode one COBS-framed message from the USB stream.

    Args:
        buf: Bytes between two 0x00 delimiters, delimiters excluded.
        strict: Reject frames with trailing bytes after the CBOR item.

    Returns:
        The decoded frame, with its CRC already verified.

    Raises:
        FrameError: The CRC does not match or the COBS encoding is invalid.
    """
```

**PY-22. Comments say why.** A comment explains a constraint, a source (a doc section, a firmware
file) or a non-obvious choice, not what the next line does. `TODO(ISSUE-ID): what` links an issue in
`issues/`. checked: review.

## Tests

**PY-23. End-to-end tests are scripts.** `daemon/tools/*_e2e.py` start their own daemon on another
port with a scratch `AGENTLINK_HOME`, never touch the user's daemon, and exit 1 on the first failed
expectation with a line that says what was expected. checked: review.

**PY-24. Unit tests, when added, use pytest.** Name files `test_<tool>.py` next to the tool, and
tests `test_<function>_<state>`; run with `uv run --with pytest pytest path`. checked: review.

## Running the checks

```sh
scripts/check python         # planned: phase 1 (format + narrow lint), phase 3 (full lint, mypy)
```

Until then, run Ruff by hand without touching the repository config:

```sh
uvx ruff@0.17.6 format --check --line-length 100 FILES
uvx ruff@0.17.6 check --select E4,E7,E9,F FILES
```
