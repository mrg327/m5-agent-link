#!/usr/bin/env python3
"""Lint first-party JavaScript with ESLint and tsc against a per-rule, per-file baseline.

Usage: python3 scripts/check.d/lint_js.py [--update-baseline]

  eslint  JS-2   ESLint with eslint.config.mjs over every tracked or untracked-not-ignored *.js,
                 *.mjs and *.cjs (the config ignores atlas/vendor and the pi TypeScript). Keys
                 are the rule ID, e.g. `jsdoc/require-param`; a parse error is `eslint:fatal`.
  tsc     JS-3   `tsc --noEmit` on tsconfig.json (checkJs + strict over the Atlas, the board and
                 the font generator) and on daemon/integrations/pi/tsconfig.json (the pi
                 extension against pi's types). Keys are the error code, e.g. `TS7006`.

The tools are pinned in tools/lint/package.json and package-lock.json. They are installed with
`npm ci --prefix tools/lint` when tools/lint/node_modules is missing or was installed from a
different lockfile (a SHA-256 stamp inside node_modules records which); node_modules is
gitignored. Needs Node 22.22.2 or newer and npm.

The baseline (scripts/check.d/baselines/lint_js.txt) holds `RULE<TAB>path<TAB>count` lines.
Exit 1 when a (rule, path) count rises or a new pair appears (its findings are printed as
`path:line: RULE message`), and also when a count falls, so the baseline stays exact: run
--update-baseline to shrink it. Standard library only (Python 3.11).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASELINE = HERE / "baselines" / "lint_js.txt"
TOOLS = Path("tools") / "lint"
STAMP = ".agentlink-lock.sha256"
TSCONFIGS = ("tsconfig.json", "daemon/integrations/pi/tsconfig.json")
TSC_LINE_RE = re.compile(
    r"^(?P<path>[^()\n]+)\((?P<line>\d+),(?P<col>\d+)\): error (?P<code>TS\d+): (?P<msg>.*)$"
)


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


def ensure_tools(root: Path) -> Path:
    """Install the pinned tools if node_modules is missing or stale; return its .bin directory."""
    lock = root / TOOLS / "package-lock.json"
    modules = root / TOOLS / "node_modules"
    want = hashlib.sha256(lock.read_bytes()).hexdigest()
    stamp = modules / STAMP
    have = stamp.read_text(encoding="utf-8").strip() if stamp.exists() else ""
    if have != want:
        print(
            f"lint_js: npm ci --prefix {TOOLS} (lockfile changed or not installed)", file=sys.stderr
        )
        cmd = ["npm", "ci", "--prefix", str(TOOLS), "--ignore-scripts", "--no-audit", "--no-fund"]
        proc = subprocess.run(cmd, cwd=root, check=False, capture_output=True, text=True)
        if proc.returncode != 0:
            sys.stderr.write(proc.stdout + proc.stderr)
            sys.exit(f"lint_js: npm ci failed (exit {proc.returncode})")
        stamp.write_text(want + "\n", encoding="utf-8")
    return modules / ".bin"


def js_files(root: Path) -> list[str]:
    """First-party *.js, *.mjs and *.cjs files, repository-relative (the config drops vendor)."""
    out = git(
        root,
        "ls-files",
        "--cached",
        "--others",
        "--exclude-standard",
        "--",
        "*.js",
        "*.mjs",
        "*.cjs",
    )
    return sorted({p for p in out.splitlines() if (root / p).is_file()})


def rel(root: Path, path: str) -> str:
    """A tool's path (absolute or cwd-relative) as a repository-relative POSIX path."""
    p = Path(path)
    return (p if p.is_absolute() else root / p).resolve().relative_to(root).as_posix()


def run_eslint(root: Path, bin_dir: Path, files: list[str]) -> list[Finding]:
    """Run the pinned ESLint over files and return its findings."""
    cmd = [str(bin_dir / "eslint"), "--format", "json", "--no-warn-ignored", "--", *files]
    proc = subprocess.run(cmd, cwd=root, check=False, capture_output=True, text=True)
    if proc.returncode not in (0, 1):
        msg = (proc.stderr or proc.stdout).strip().splitlines()
        return [Finding("eslint:crash", "eslint.config.mjs", 0, msg[-1] if msg else "no output")]
    findings = []
    for item in json.loads(proc.stdout or "[]"):
        path = rel(root, item["filePath"])
        for m in item["messages"]:
            rule = m.get("ruleId") or ("eslint:fatal" if m.get("fatal") else "eslint:other")
            findings.append(Finding(rule, path, m.get("line") or 0, m["message"]))
    return findings


def run_tsc(root: Path, bin_dir: Path, project: str) -> list[Finding]:
    """Run the pinned tsc on one project and return its errors."""
    cmd = [str(bin_dir / "tsc"), "-p", project, "--noEmit", "--pretty", "false"]
    proc = subprocess.run(cmd, cwd=root, check=False, capture_output=True, text=True)
    findings = []
    for line in proc.stdout.splitlines():
        if m := TSC_LINE_RE.match(line):
            path = rel(root, m.group("path"))
            findings.append(Finding(m.group("code"), path, int(m.group("line")), m.group("msg")))
        elif line.startswith("error TS"):  # a config error: no file position
            findings.append(Finding("tsc:crash", project, 0, line))
    if proc.returncode != 0 and not findings:
        msg = (proc.stderr or proc.stdout).strip().splitlines()
        findings.append(Finding("tsc:crash", project, 0, msg[-1] if msg else "no output"))
    return findings


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
        "# ESLint and tsc findings held by scripts/check.d/lint_js.py: counts may only go down.",
        "# RULE<TAB>path<TAB>count, sorted. Regenerate with --update-baseline.",
    ]
    lines += [f"{rule}\t{path}\t{n}" for (rule, path), n in sorted(counts.items()) if n]
    BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    """Run the linters and compare against the baseline; return the exit code."""
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--update-baseline", action="store_true", help="rewrite the baseline")
    args = ap.parse_args(argv)
    if not shutil.which("node") or not shutil.which("npm"):
        sys.exit("lint_js: node or npm not found - install Node 22.22.2 or newer")

    root = Path(git(HERE, "rev-parse", "--show-toplevel").strip())
    t0 = time.monotonic()
    bin_dir = ensure_tools(root)
    files = js_files(root)
    t1 = time.monotonic()
    with ThreadPoolExecutor(max_workers=1 + len(TSCONFIGS)) as pool:
        eslint = pool.submit(run_eslint, root, bin_dir, files)
        tsc = [pool.submit(run_tsc, root, bin_dir, p) for p in TSCONFIGS]
        findings = eslint.result() + [f for t in tsc for f in t.result()]
    t2 = time.monotonic()
    print(
        f"lint_js: {len(files)} files, setup {t1 - t0:.1f}s, eslint+tsc {t2 - t1:.1f}s",
        file=sys.stderr,
    )

    crashed = [f for f in findings if f.rule.endswith(":crash")]
    for f in crashed:
        print(f"{f.path}:0: {f.rule} {f.msg} - run the tool by hand to see why")
    if crashed:
        return 1

    counts = Counter((f.rule, f.path) for f in findings)
    if args.update_baseline:
        write_baseline(counts)
        print(f"lint_js: baseline rewritten, {sum(counts.values())} findings", file=sys.stderr)
        return 0

    base = load_baseline()
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
        print("lint_js: baseline can shrink: run lint_js.py --update-baseline")
    total = sum(counts.values())
    print(f"lint_js: {'FAIL' if failed else 'ok'}, {total} findings", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
