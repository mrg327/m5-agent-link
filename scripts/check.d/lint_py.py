#!/usr/bin/env python3
"""Lint first-party Python with Ruff and mypy against a per-rule, per-file baseline.

Usage: python3 scripts/check.d/lint_py.py [--update-baseline] [--no-mypy] [-j N]

  ruff  PY-2   `uvx ruff@RUFF_VERSION check` with the rule set in ruff.toml, over every tracked
               or untracked-not-ignored *.py. Keys are (rule, path), e.g. `D103`.
  mypy  PY-3   `mypy --strict`, one run per script in its own environment so its dependencies
               resolve: a PEP 723 block gives `uv run --no-project --with-requirements FILE`;
               a header line `uv run --with a --with b python FILE` gives those `--with`s; anything
               else runs under plain `uv run --no-project --with mypy`. Keys are `mypy:<code>`.

The baseline (scripts/check.d/baselines/lint_py.txt) holds `RULE<TAB>path<TAB>count` lines.
Exit 1 when a (rule, path) count rises or a new pair appears (its findings are printed as
`path:line: RULE message`), and also when a count falls, so the baseline stays exact: run
--update-baseline to shrink it. Caches live under ${XDG_CACHE_HOME:-~/.cache}/agentlink-check/.
Standard library only (Python 3.11).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

RUFF_VERSION = "0.17.6"
MYPY_VERSION = "2.4.1"
HERE = Path(__file__).resolve().parent
BASELINE = HERE / "baselines" / "lint_py.txt"
CACHE = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "agentlink-check"
MYPY_LINE_RE = re.compile(
    r"^(?P<path>[^:\n]+):(?P<line>\d+): error: (?P<msg>.*?)(?:  \[(?P<code>[\w-]+)\])?$"
)
UV_WITH_RE = re.compile(r"^#\s*uv run\s+(?P<args>(?:--with\s+\S+\s+)+)python\b")
HEADER_LINES = 40


@dataclass(frozen=True)
class Finding:
    """One lint finding: the baseline key is (rule, path); line and message are for the reader."""

    rule: str
    path: str
    line: int
    msg: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.rule} {self.msg}"


def git(root: Path, *args: str) -> str:
    """Run git in the repository and return its stdout."""
    return subprocess.run(
        ["git", "-C", str(root), *args], check=True, capture_output=True, text=True
    ).stdout


def python_files(root: Path) -> list[str]:
    """First-party *.py files, repository-relative.

    Tracked and untracked-but-not-ignored files, less ruff.toml's `extend-exclude` (vendored code,
    node_modules), so Ruff and mypy see the same set.
    """
    out = git(root, "ls-files", "--cached", "--others", "--exclude-standard", "--", "*.py")
    files = sorted({p for p in out.splitlines() if (root / p).is_file()})
    cmd = ["uvx", f"ruff@{RUFF_VERSION}", "check", "--no-cache", "--force-exclude", "--show-files"]
    shown = subprocess.run(
        [*cmd, "--", *files], cwd=root, check=True, capture_output=True, text=True
    ).stdout
    kept = {Path(p).resolve().relative_to(root).as_posix() for p in shown.splitlines() if p}
    return [p for p in files if p in kept]


def run_ruff(root: Path, files: list[str]) -> list[Finding]:
    """Run the pinned Ruff over files and return its findings."""
    cmd = ["uvx", f"ruff@{RUFF_VERSION}", "check", "--no-cache", "--force-exclude", "--quiet"]
    cmd += ["--output-format=json", "--exit-zero", "--", *files]
    out = subprocess.run(cmd, cwd=root, check=True, capture_output=True, text=True).stdout
    findings = []
    for item in json.loads(out or "[]"):
        path = Path(item["filename"]).resolve().relative_to(root).as_posix()
        line = (item.get("location") or {}).get("row") or 0
        findings.append(Finding(item.get("code") or "syntax", path, line, item["message"]))
    return findings


def mypy_command(root: Path, rel: str) -> list[str]:
    """The uv command that runs mypy on one script inside that script's own environment."""
    head = (root / rel).read_text(encoding="utf-8", errors="replace").splitlines()[:HEADER_LINES]
    cmd = ["uv", "run", "--quiet", "--no-project", "--with", f"mypy=={MYPY_VERSION}"]
    if any(line.strip() == "# /// script" for line in head):
        cmd += ["--with-requirements", rel]
    else:
        for line in head:
            if m := UV_WITH_RE.match(line):
                cmd += m.group("args").split()
                break
    key = hashlib.sha256(rel.encode()).hexdigest()[:16]
    cache_dir = CACHE / "mypy" / f"{MYPY_VERSION}-{key}"
    return [
        *cmd,
        "mypy",
        "--strict",
        "--python-version=3.11",
        f"--cache-dir={cache_dir}",
        "--no-error-summary",
        "--no-pretty",
        "--show-error-codes",
        "--hide-error-context",
        "--no-color-output",
        rel,
    ]


def run_mypy_one(root: Path, rel: str) -> list[Finding]:
    """Run mypy on one script; return the errors it reports in that script."""
    proc = subprocess.run(
        mypy_command(root, rel), cwd=root, check=False, capture_output=True, text=True
    )
    if proc.returncode not in (0, 1):
        msg = (proc.stderr or proc.stdout).strip().splitlines()
        return [Finding("mypy:crash", rel, 0, msg[-1] if msg else f"exit {proc.returncode}")]
    findings = []
    for line in proc.stdout.splitlines():
        if (m := MYPY_LINE_RE.match(line)) and Path(m.group("path")).as_posix() == rel:
            code = m.group("code") or "misc"
            findings.append(Finding(f"mypy:{code}", rel, int(m.group("line")), m.group("msg")))
    return findings


def run_mypy(root: Path, files: list[str], jobs: int) -> list[Finding]:
    """Run mypy on every file, in parallel; each run checks only its own file."""
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        results = pool.map(lambda rel: run_mypy_one(root, rel), files)
        return [f for batch in results for f in batch]


def load_baseline() -> Counter[tuple[str, str]]:
    """Read the baseline file into {(rule, path): count}."""
    counts: Counter[tuple[str, str]] = Counter()
    if BASELINE.exists():
        for line in BASELINE.read_text(encoding="utf-8").splitlines():
            if line.strip() and not line.startswith("#"):
                rule, path, count = line.split("\t")
                counts[(rule, path)] = int(count)
    return counts


def write_baseline(counts: Counter[tuple[str, str]]) -> None:
    """Write {(rule, path): count} as sorted `RULE<TAB>path<TAB>count` lines."""
    lines = [
        "# Ruff and mypy findings held by scripts/check.d/lint_py.py: counts may only go down.",
        "# RULE<TAB>path<TAB>count, sorted. Regenerate with --update-baseline.",
    ]
    lines += [f"{rule}\t{path}\t{n}" for (rule, path), n in sorted(counts.items()) if n]
    BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    """Run the linters and compare against the baseline; return the exit code."""
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--update-baseline", action="store_true", help="rewrite the baseline")
    ap.add_argument(
        "--no-mypy", action="store_true", help="Ruff only (the baseline is not checked)"
    )
    ap.add_argument(
        "-j", "--jobs", type=int, default=os.cpu_count() or 4, help="parallel mypy runs"
    )
    args = ap.parse_args(argv)
    if not shutil.which("uv") or not shutil.which("uvx"):
        sys.exit("lint_py: uv not found - install it: https://docs.astral.sh/uv/")

    root = Path(git(HERE, "rev-parse", "--show-toplevel").strip())
    files = python_files(root)
    t0 = time.monotonic()
    findings = run_ruff(root, files)
    t1 = time.monotonic()
    if not args.no_mypy:
        findings += run_mypy(root, files, max(1, args.jobs))
    t2 = time.monotonic()
    print(f"lint_py: {len(files)} files, ruff {t1 - t0:.1f}s, mypy {t2 - t1:.1f}s", file=sys.stderr)

    crashed = [f for f in findings if f.rule == "mypy:crash"]
    for f in crashed:
        print(f"{f.path}:0: mypy:crash {f.msg} - run the uv command by hand to see why")
    if crashed:
        return 1

    counts = Counter((f.rule, f.path) for f in findings)
    if args.update_baseline:
        write_baseline(counts)
        print(f"lint_py: baseline rewritten, {sum(counts.values())} findings", file=sys.stderr)
        return 0

    base = load_baseline()
    if args.no_mypy:
        base = Counter({k: v for k, v in base.items() if not k[0].startswith("mypy:")})
    failed = False
    for key in sorted(counts):
        if counts[key] > base.get(key, 0):
            failed = True
            for f in sorted((f for f in findings if (f.rule, f.path) == key), key=lambda f: f.line):
                print(f)
            print(f"  ^ {key[0]} in {key[1]}: {counts[key]} now, baseline {base.get(key, 0)}")
    shrunk = [k for k in sorted(base) if counts.get(k, 0) < base[k]]
    for rule, path in shrunk:
        print(f"{path}:0: {rule} {counts.get((rule, path), 0)} now, baseline {base[(rule, path)]}")
    if shrunk:
        failed = True
        print("lint_py: baseline can shrink: run lint_py.py --update-baseline")
    total = sum(counts.values())
    print(f"lint_py: {'FAIL' if failed else 'ok'}, {total} findings", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
