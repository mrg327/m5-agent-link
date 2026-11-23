#!/usr/bin/env python3
"""The Rust lint check: clippy with the [workspace.lints] table, against a shrinking baseline.

Usage: python3 scripts/check.d/lint_rust.py [--update-baseline] [--target TRIPLE ...]

Runs `cargo clippy --workspace --all-targets --message-format=json --target T` in daemon/ once for
each of Linux, macOS and Windows (TARGETS below), so cfg(target_os) code is linted on every host and
the baseline is the same everywhere. Tests are linted too: daemon/clippy.toml exempts them from the
panic lints. A finding seen on several targets (or in both the lib and its test build) counts once.

Findings are counted per (lint, file) and compared with scripts/check.d/baselines/lint_rust.txt
(lines `RULE<TAB>path<TAB>count`, sorted). Exit 1 when:
  - a compile error or a deny-level lint fires (never baselined);
  - a (lint, file) pair is new or its count went up (those findings are printed as
    `path:line: RULE message`); this replaces `cargo clippy -- -D warnings`;
  - a count went down or a pair is gone: the baseline can shrink, run --update-baseline.
--update-baseline rewrites the baseline from the current findings (compile errors still fail).
--target narrows the run to some triples, for a quick local look; the baseline comparison is then
skipped, because it covers all three.

The lint set depends on the clippy version, so it is pinned (CLIPPY_VERSION). The other targets'
standard libraries are needed: `rustup target add aarch64-apple-darwin x86_64-pc-windows-msvc`.
Build output goes to $CARGO_TARGET_DIR, or ${XDG_CACHE_HOME:-~/.cache}/agentlink-check/cargo-lint.
Standard library only (Python 3.11).
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from collections import Counter
from typing import Any

# A finding: (rule, repo path, line, column, first line of the message).
Finding = tuple[str, str, int, int, str]

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DAEMON = os.path.join(ROOT, "daemon")
BASELINE = os.path.join(ROOT, "scripts", "check.d", "baselines", "lint_rust.txt")

CLIPPY_VERSION = "0.1.95"  # rustc 1.95; CI pins the same toolchain (.github/workflows/daemon.yml)
# Lints that rustc reports under an error code instead of their name.
LINT_CODES = {"E0133": "unsafe_op_in_unsafe_fn"}
TARGETS = ["x86_64-unknown-linux-gnu", "aarch64-apple-darwin", "x86_64-pc-windows-msvc"]


def fail(msg: str) -> None:
    """Print msg and exit 1."""
    print(f"lint_rust: {msg}", file=sys.stderr)
    sys.exit(1)


def check_tools(targets: list[str]) -> None:
    """Exit unless the pinned clippy and the wanted targets are installed."""
    try:
        out = subprocess.run(
            ["cargo", "clippy", "--version"],
            cwd=DAEMON,
            capture_output=True,
            text=True,
            check=False,
        ).stdout
    except FileNotFoundError:
        fail("cargo not found; install Rust with rustup (stable, with clippy)")
    words = out.split()
    if len(words) < 2 or words[1] != CLIPPY_VERSION:
        fail(f"need clippy {CLIPPY_VERSION}, found {out.strip() or 'none'} (rustup default 1.95)")
    try:
        have = subprocess.run(
            ["rustup", "target", "list", "--installed"], capture_output=True, text=True, check=False
        ).stdout.split()
    except FileNotFoundError:
        return  # no rustup: let cargo report a missing target
    missing = [t for t in targets if t not in have]
    if missing:
        fail(f"missing Rust targets; run: rustup target add {' '.join(missing)}")


def repo_path(file_name: str) -> str | None:
    """A span's file as a repo-relative path, or None when it is outside the repository."""
    p = os.path.normpath(os.path.join(DAEMON, file_name))
    if not p.startswith(ROOT + os.sep):
        return None
    return os.path.relpath(p, ROOT).replace(os.sep, "/")


def locate(msg: dict[str, Any], manifest: str) -> tuple[str, int, int]:
    """Repo path and line of a diagnostic: its primary span, followed out of foreign macros."""
    spans = [s for s in msg.get("spans", []) if s.get("is_primary")] or msg.get("spans", [])
    for span in spans:
        s = span
        while s is not None:
            path = repo_path(s["file_name"])
            if path is not None:
                return path, s["line_start"], s["column_start"]
            s = (s.get("expansion") or {}).get("span")
    return manifest, 0, 0  # crate-level lints (the cargo group) have no span


def clippy(target: str, env: dict[str, str]) -> tuple[set[Finding], list[str]]:
    """Run clippy for one target: (set of findings, list of error lines)."""
    cmd = [
        "cargo",
        "clippy",
        "--workspace",
        "--all-targets",
        "--message-format=json",
        "--quiet",
        "--target",
        target,
    ]
    proc = subprocess.run(cmd, cwd=DAEMON, env=env, capture_output=True, text=True, check=False)
    findings: set[Finding] = set()
    errors: list[str] = []
    for line in proc.stdout.splitlines():
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            continue
        if rec.get("reason") != "compiler-message":
            continue
        msg = rec["message"]
        level = msg.get("level", "")
        if level not in ("warning", "error"):
            continue
        code = (msg.get("code") or {}).get("code")
        manifest = repo_path(rec.get("manifest_path", "")) or "daemon/Cargo.toml"
        path, ln, col = locate(msg, manifest)
        text = msg.get("message", "").splitlines()[0] if msg.get("message") else ""
        if level == "error":
            if code is None and not msg.get("spans"):
                continue  # "aborting due to N previous errors"
            errors.append(f"{path}:{ln}: {code or 'error'} {text} [{target}]")
            continue
        if code is None:
            if not msg.get("spans"):
                continue  # "N warnings emitted"
            code = "warning"
        findings.add((LINT_CODES.get(code, code), path, ln, col, text))
    if proc.returncode != 0 and not errors:
        tail = "\n".join(proc.stderr.strip().splitlines()[-20:])
        errors.append(f"cargo clippy failed for {target} (exit {proc.returncode}):\n{tail}")
    return findings, errors


def read_baseline() -> Counter[tuple[str, str]]:
    """The baseline as {(rule, path): count}."""
    base: Counter[tuple[str, str]] = Counter()
    if not os.path.exists(BASELINE):
        return base
    with open(BASELINE, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            rule, path, count = line.split("\t")
            base[(rule, path)] = int(count)
    return base


def write_baseline(counts: Counter[tuple[str, str]]) -> None:
    """Write {(rule, path): count} as the sorted baseline."""
    os.makedirs(os.path.dirname(BASELINE), exist_ok=True)
    with open(BASELINE, "w", encoding="utf-8", newline="\n") as f:
        for (rule, path), n in sorted(counts.items()):
            f.write(f"{rule}\t{path}\t{n}\n")


def main() -> int:
    """Run clippy on every target and compare with the baseline; return the exit status."""
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--target", action="append", choices=TARGETS)
    args = ap.parse_args()
    targets = args.target or TARGETS
    check_tools(targets)

    env = dict(os.environ)
    if not env.get("CARGO_TARGET_DIR"):
        cache = env.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
        env["CARGO_TARGET_DIR"] = os.path.join(cache, "agentlink-check", "cargo-lint")
    env["CARGO_TERM_COLOR"] = "never"

    findings: set[Finding] = set()
    errors: list[str] = []
    for t in targets:
        f, e = clippy(t, env)
        findings |= f
        errors += e
    errors = list(dict.fromkeys(errors))  # the lib and its test build report the same error
    if errors:
        print("\n".join(errors))
        fail(f"{len(errors)} error(s): compile errors and deny-level lints are never baselined")

    # One finding per (rule, file, line, column), whichever targets reported it.
    counts = Counter((rule, path) for rule, path, *_ in findings)
    total = sum(counts.values())
    if args.update_baseline:
        write_baseline(counts)
        print(f"lint_rust: baseline written: {total} findings in {len(counts)} (lint, file) pairs")
        return 0
    if args.target:
        for rule, path, ln, _, text in sorted(findings, key=lambda x: (x[1], x[2], x[3], x[0])):
            print(f"{path}:{ln}: {rule} {text}")
        print(f"lint_rust: {total} findings on {', '.join(targets)} (baseline not compared)")
        return 0

    base = read_baseline()
    grown = {k for k, n in counts.items() if n > base.get(k, 0)}
    shrunk = {k for k, n in base.items() if counts.get(k, 0) < n}
    for rule, path, ln, _, text in sorted(findings, key=lambda x: (x[1], x[2], x[3], x[0])):
        if (rule, path) in grown:
            print(f"{path}:{ln}: {rule} {text}")
    for rule, path in sorted(grown):
        print(
            f"lint_rust: {rule} in {path}: {counts[(rule, path)]} > baseline {base[(rule, path)]}"
        )
    if grown:
        print("lint_rust: fix the new findings above (an exception takes #[expect(..., reason)])")
    for rule, path in sorted(shrunk):
        now, was = counts.get((rule, path), 0), base[(rule, path)]
        print(f"lint_rust: {rule} in {path}: {now} < baseline {was}")
    if shrunk:
        print("lint_rust: baseline can shrink: run lint_rust.py --update-baseline")
    status = "FAIL" if grown or shrunk else "ok"
    print(f"lint_rust: {status}: {total} findings, baseline {sum(base.values())}")
    return 1 if grown or shrunk else 0


if __name__ == "__main__":
    sys.exit(main())
