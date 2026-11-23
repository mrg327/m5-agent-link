#!/usr/bin/env bash
# scripts/check.d/format.sh: formatting for every language (docs/conventions/, phase 1).
#
# Usage: scripts/check.d/format.sh [--fix]
#   (no flag)  check only; exit 1 if any file needs formatting
#   --fix      run the formatters in write mode (the `//` comment check still only reports)
#
# Run by `scripts/check format`. Each formatter is pinned so CI and humans get the same result:
#   C/C++   uvx --from clang-format==23.1.5 clang-format   (.clang-format, .clang-format-ignore)
#           plus a check for `//` comments (first-party C uses /* */ only)
#   Python  uvx ruff@0.17.6 format on tracked *.py         (ruff.toml)
#   JS/TS   npx --yes prettier@3.10.1                       (.prettierrc.json, .prettierignore)
#   Rust    cargo fmt in daemon/                            (rustfmt.toml)
#
# A missing uvx, npx or cargo is a failure with an install hint. Locally, CHECK_ALLOW_SKIP=1 turns
# that into a skip (ignored when CI is set). Exit status: 0 all passed, 1 something failed,
# 77 nothing failed but something was skipped (scripts/check reports that as SKIP), 2 usage error.
#
# Written for bash 3.2 too (the macOS system bash): no associative arrays, no mapfile.

set -uo pipefail

ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$ROOT" || exit 2

CLANG_FORMAT="uvx --from clang-format==23.1.5 clang-format"
RUFF="uvx ruff@0.17.6"
PRETTIER="npx --yes prettier@3.10.1"

FIX=0
case "${1:-}" in
"") ;;
--fix) FIX=1 ;;
-h | --help)
    sed -n '/^# Usage:/,/^#   --fix/s/^# \{0,1\}//p' "$0"
    exit 0
    ;;
*)
    echo "format: unknown argument: $1" >&2
    exit 2
    ;;
esac

ALLOW_SKIP=0
if [ "${CHECK_ALLOW_SKIP:-0}" = "1" ] && [ -z "${CI:-}" ]; then
    ALLOW_SKIP=1
fi

RESULTS=""
N_FAIL=0
N_SKIP=0

result() { # LANG RESULT NOTE
    RESULTS="$RESULTS$(printf '  %-8s %-5s %s' "$1" "$2" "$3")
"
    case "$2" in
    FAIL) N_FAIL=$((N_FAIL + 1)) ;;
    SKIP) N_SKIP=$((N_SKIP + 1)) ;;
    esac
}

# have LANG TOOL HINT: succeed if TOOL is on PATH; otherwise record FAIL (or SKIP) and fail.
have() {
    command -v "$2" >/dev/null 2>&1 && return 0
    if [ "$ALLOW_SKIP" -eq 1 ]; then
        echo "--- format $1: SKIP, $2 not found ($3)"
        result "$1" SKIP "$2 not found (CHECK_ALLOW_SKIP=1)"
    else
        echo "--- format $1: FAIL, $2 not found; $3 (or set CHECK_ALLOW_SKIP=1 locally to skip)"
        result "$1" FAIL "$2 not found"
    fi
    return 1
}

# tracked FILE-PATTERN...: tracked files that exist in the work tree, one per line.
tracked() {
    git ls-files -- "$@" | while IFS= read -r f; do
        [ -f "$f" ] && printf '%s\n' "$f"
    done
}

# show OUTPUT-FILE: print a tool's output, trimmed to the first 60 lines.
show() {
    local n
    n="$(wc -l <"$1" | tr -d ' ')"
    head -n 60 "$1"
    [ "$n" -gt 60 ] && echo "    ... ($((n - 60)) more lines)"
    return 0
}

TMP="$(mktemp -d "${TMPDIR:-/tmp}/check-format.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# ---- C/C++ --------------------------------------------------------------------------------------

check_c() {
    local files=() f n rc
    while IFS= read -r f; do files+=("$f"); done <<EOF
$(tracked '*.c' '*.h' '*.cpp' '*.hpp')
EOF
    [ -z "${files[0]:-}" ] && files=()
    if [ "${#files[@]}" -eq 0 ]; then
        result C PASS "no files"
        result C-// PASS "no files"
        return
    fi
    if ! have C uvx "install uv from https://docs.astral.sh/uv/"; then
        result C-// SKIP "needs clang-format's file list"
        return
    fi
    # clang-format skips files matched by .clang-format-ignore even when named explicitly, and
    # --verbose prints "Formatting [i/N] path" for each file it does process: that is the
    # first-party list the `//` check below uses, so both checks share one ignore list.
    echo "==> format C: clang-format on ${#files[@]} tracked file(s)"
    if [ "$FIX" -eq 1 ]; then
        $CLANG_FORMAT --verbose -i "${files[@]}" >"$TMP/c.out" 2>&1
    else
        $CLANG_FORMAT --verbose --dry-run --Werror "${files[@]}" >"$TMP/c.out" 2>&1
    fi
    rc=$?
    sed -n 's/^Formatting \[[0-9]*\/[0-9]*\] //p' "$TMP/c.out" >"$TMP/c.files"
    n="$(grep -c . "$TMP/c.files" || true)"
    if [ "$rc" -eq 0 ]; then
        result C PASS "$n file(s), $((${#files[@]} - n)) ignored"
    else
        grep -v '^Formatting \[' "$TMP/c.out" >"$TMP/c.err" || true
        show "$TMP/c.err"
        if [ "$FIX" -eq 1 ]; then
            result C FAIL "clang-format -i failed"
        else
            n="$(grep -o '^[^:]*:[0-9]*:[0-9]*: error' "$TMP/c.err" | cut -d: -f1 | sort -u |
                grep -c . || true)"
            result C FAIL "$n file(s) need formatting (scripts/check.d/format.sh --fix)"
        fi
    fi
    check_line_comments "$TMP/c.files"
}

# check_line_comments LIST: fail on any `//` comment in the files named in LIST. A small C
# tokenizer skips string and character literals and /* */ comments, so "http://" in a string or a
# `//` inside a block comment does not count.
check_line_comments() {
    local py
    if [ ! -s "$1" ]; then
        result C-// PASS "no first-party files"
        return
    fi
    if command -v python3 >/dev/null 2>&1; then
        py="python3"
    elif command -v uv >/dev/null 2>&1; then
        py="uv run --no-project python"
    else
        have C-// python3 "install Python 3" || return
    fi
    echo "==> format C-//: no // comments in $(grep -c . "$1") first-party file(s)"
    if $py - "$1" >"$TMP/slash.out" 2>&1 <<'PY'; then
import sys

def line_comments(src):
    """Yield the 1-based line of each // comment in C/C++ source."""
    i, n, line = 0, len(src), 1
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
        elif src.startswith("//", i):
            yield line
            while i < n and src[i] != "\n":
                i += 1
        elif src.startswith("/*", i):
            end = src.find("*/", i + 2)
            end = n if end < 0 else end + 2
            line += src.count("\n", i, end)
            i = end
        elif src.startswith('R"', i) and not (i and (src[i - 1].isalnum() or src[i - 1] == "_")):
            # C++ raw string R"delim( ... )delim"
            open_paren = src.find("(", i + 2)
            if open_paren < 0:
                i += 2
                continue
            close = ")" + src[i + 2 : open_paren] + '"'
            end = src.find(close, open_paren)
            end = n if end < 0 else end + len(close)
            line += src.count("\n", i, end)
            i = end
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            i = j + 1
        else:
            i += 1

bad = 0
with open(sys.argv[1], encoding="utf-8") as listing:
    paths = [p.strip() for p in listing if p.strip()]
for path in paths:
    with open(path, encoding="utf-8", errors="replace") as f:
        src = f.read()
    lines = src.split("\n")
    for ln in line_comments(src):
        bad += 1
        if bad <= 40:
            print(f"{path}:{ln}: // comment (use /* */): {lines[ln - 1].strip()[:80]}")
if bad > 40:
    print(f"... {bad - 40} more")
print(f"{bad}")
sys.exit(1 if bad else 0)
PY
        result C-// PASS "none"
    else
        sed '$d' "$TMP/slash.out"
        result C-// FAIL "$(tail -n 1 "$TMP/slash.out") // comment(s); first-party C uses /* */"
    fi
}

# ---- Python -------------------------------------------------------------------------------------

check_python() {
    local files=() f
    while IFS= read -r f; do files+=("$f"); done <<EOF
$(tracked '*.py')
EOF
    [ -z "${files[0]:-}" ] && files=()
    if [ "${#files[@]}" -eq 0 ]; then
        result Python PASS "no files"
        return
    fi
    have Python uvx "install uv from https://docs.astral.sh/uv/" || return
    # Tracked *.py only: given a directory, Ruff would also format Python blocks inside *.md.
    # --force-exclude applies ruff.toml's excludes to the named files; the cache stays outside
    # the work tree.
    local opts=(--force-exclude --cache-dir "${XDG_CACHE_HOME:-$HOME/.cache}/agentlink-check/ruff")
    echo "==> format Python: ruff format on ${#files[@]} tracked file(s) (ruff.toml)"
    if [ "$FIX" -eq 1 ]; then
        $RUFF format "${opts[@]}" "${files[@]}" >"$TMP/py.out" 2>&1
    else
        $RUFF format --check "${opts[@]}" "${files[@]}" >"$TMP/py.out" 2>&1
    fi
    local rc=$?
    if [ "$rc" -eq 0 ]; then
        result Python PASS "$(tail -n 1 "$TMP/py.out")"
    else
        show "$TMP/py.out"
        result Python FAIL "$(tail -n 1 "$TMP/py.out")"
    fi
}

# ---- JS/TS --------------------------------------------------------------------------------------

check_js() {
    local files=() f ignore=() n
    while IFS= read -r f; do files+=("$f"); done <<EOF
$(tracked '*.js' '*.mjs' '*.ts')
EOF
    [ -z "${files[0]:-}" ] && files=()
    if [ "${#files[@]}" -eq 0 ]; then
        result JS/TS PASS "no files"
        return
    fi
    have JS/TS npx "install Node.js 22 or newer (npx comes with it)" || return
    [ -f .prettierignore ] && ignore=(--ignore-path .prettierignore)
    echo "==> format JS/TS: prettier on ${#files[@]} tracked file(s)"
    if [ "$FIX" -eq 1 ]; then
        $PRETTIER --write --log-level warn "${ignore[@]}" "${files[@]}" >"$TMP/js.out" 2>&1
    else
        $PRETTIER --check "${ignore[@]}" "${files[@]}" >"$TMP/js.out" 2>&1
    fi
    local rc=$?
    if [ "$rc" -eq 0 ]; then
        result JS/TS PASS "${#files[@]} tracked file(s), .prettierignore applied"
    else
        show "$TMP/js.out"
        n="$(grep -c '^\[warn\] [^ ]*\.[a-z]*$' "$TMP/js.out" || true)"
        result JS/TS FAIL "$n file(s) need formatting"
    fi
}

# ---- Rust ---------------------------------------------------------------------------------------

check_rust() {
    have Rust cargo "install Rust with rustup (stable, with the rustfmt component)" || return
    echo "==> format Rust: cargo fmt (in daemon/)"
    if [ "$FIX" -eq 1 ]; then
        (cd daemon && cargo fmt) >"$TMP/rs.out" 2>&1
    else
        (cd daemon && cargo fmt --check) >"$TMP/rs.out" 2>&1
    fi
    local rc=$?
    if [ "$rc" -eq 0 ]; then
        result Rust PASS "daemon workspace"
    else
        show "$TMP/rs.out"
        result Rust FAIL "$(grep -c '^Diff in ' "$TMP/rs.out" || true) hunk(s) differ"
    fi
}

check_c
check_python
check_js
check_rust

echo
echo "format:"
printf '%s' "$RESULTS"
[ "$N_FAIL" -gt 0 ] && exit 1
[ "$N_SKIP" -gt 0 ] && exit 77
exit 0
