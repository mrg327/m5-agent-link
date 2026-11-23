#!/usr/bin/env python3
"""Lint first-party C with clang-tidy and an nm prefix check against a per-rule, per-file baseline.

Usage: python3 scripts/check.d/lint_c.py [--update-baseline] [--analyzer] [--no-nm] [-j N]

  clang-tidy  `uvx --from clang-tidy==TIDY_VERSION clang-tidy` with the repository's .clang-tidy,
              over every first-party .c file in the simulator's compile database (the files
              .clang-format-ignore lists are left out, and so are their headers, by
              HeaderFilterRegex). A finding in a header is counted once however many files include
              it. Keys are (check, path), e.g. `readability-function-size`. The firmware-only
              files (firmware/main, firmware/boards) are not in that database.
  --analyzer  adds clang-analyzer-* (about 5x the run time). Its rows in the baseline are checked
              only when it runs.
  nm          C-NAME-01 backstop: every symbol the simulator's `core` objects export must match one
              of the module-prefix regexes in .clang-tidy. Key `nm-prefix`, per source file.

The compile database is configured, and core's objects compiled for nm, in
${XDG_CACHE_HOME:-~/.cache}/agentlink-check/sim-db/<worktree hash>, never in the worktree; LVGL is
cloned once into .../agentlink-check/src/ and shared between worktrees.

The baseline (scripts/check.d/baselines/lint_c.txt) holds `RULE<TAB>path<TAB>count` lines. Exit 1
when a (rule, path) count rises or a new pair appears (its findings are printed as
`path:line: RULE message`), and also when a count falls, so the baseline stays exact: run
--update-baseline to shrink it. Standard library only (Python 3.11).
"""

from __future__ import annotations

import argparse
import fnmatch
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

TIDY_VERSION = "23.1.4"
ANALYZER = (
    "clang-analyzer-*,-clang-analyzer-security.insecureAPI.*,"
    "-clang-analyzer-optin.performance.Padding,-clang-analyzer-optin.core.EnumCastOutOfRange"
)
LVGL_URL = "https://github.com/lvgl/lvgl.git"
HERE = Path(__file__).resolve().parent
BASELINE = HERE / "baselines" / "lint_c.txt"
CACHE = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "agentlink-check"
TIDY_RE = re.compile(
    r"^(?P<path>[^:\n]+):(?P<line>\d+):(?P<col>\d+): (?P<sev>warning|error): "
    r"(?P<msg>.*) \[(?P<check>[\w.,-]+)\]$"
)
NM_RE = re.compile(r"^(?P<obj>[^:]+\.o): *(?:[0-9a-f]+ )?(?P<type>[A-Z]) (?P<sym>\S+)$")
IGNORED_KEYS = (
    "GlobalFunctionIgnoredRegexp",
    "GlobalVariableIgnoredRegexp",
    "GlobalConstantIgnoredRegexp",
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


def run(cmd: list[str], cwd: Path | None = None) -> str:
    """Run cmd, exit with its output if it fails, and return its stdout."""
    proc = subprocess.run(cmd, cwd=cwd, check=False, capture_output=True, text=True)
    if proc.returncode != 0:
        tail = "\n".join((proc.stdout + proc.stderr).strip().splitlines()[-20:])
        sys.exit(f"lint_c: `{' '.join(cmd)}` failed:\n{tail}")
    return proc.stdout


def git(root: Path, *args: str) -> str:
    """Run git in the repository and return its stdout."""
    return run(["git", "-C", str(root), *args])


def ignore_patterns(root: Path) -> list[str]:
    """The globs in .clang-format-ignore: generated and vendored C."""
    lines = (root / ".clang-format-ignore").read_text(encoding="utf-8").splitlines()
    return [s for line in lines if (s := line.strip()) and not s.startswith("#")]


def ignored(rel: str, patterns: list[str]) -> bool:
    """Whether rel matches one of the .clang-format-ignore globs (`**` spans directories)."""
    return any(fnmatch.fnmatchcase(rel, p) for p in patterns)


def lvgl_source(root: Path) -> Path:
    """A shared LVGL checkout at the tag simulator/CMakeLists.txt pins, cloned on first use."""
    text = (root / "simulator" / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"FetchContent_Declare\(lvgl\b[^)]*?GIT_TAG\s+(\S+)", text, re.S)
    if not m:
        sys.exit("lint_c: no LVGL GIT_TAG in simulator/CMakeLists.txt")
    src = CACHE / "src" / f"lvgl-{m.group(1)}"
    if not (src / "lvgl.h").exists():
        shutil.rmtree(src, ignore_errors=True)
        src.parent.mkdir(parents=True, exist_ok=True)
        print(f"lint_c: cloning LVGL {m.group(1)} into {src}", file=sys.stderr)
        run(["git", "clone", "--quiet", "--depth=1", f"--branch={m.group(1)}", LVGL_URL, str(src)])
    return src


def compile_db(root: Path) -> Path:
    """Configure the simulator in the cache (again if a CMakeLists.txt is newer); return its dir."""
    build = CACHE / "sim-db" / hashlib.sha256(str(root).encode()).hexdigest()[:12]
    db = build / "compile_commands.json"
    lists = [root / "simulator" / "CMakeLists.txt"]
    lists += (root / "firmware" / "components").glob("*/CMakeLists.txt")
    if not db.exists() or db.stat().st_mtime < max(p.stat().st_mtime for p in lists):
        cmd = ["cmake", "-S", str(root / "simulator"), "-B", str(build), "-G", "Ninja"]
        cmd += ["-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DCMAKE_BUILD_TYPE=Debug"]
        cmd += [f"-DFETCHCONTENT_SOURCE_DIR_LVGL={lvgl_source(root)}"]
        run(cmd)
        db.touch()
    return build


def c_files(root: Path, build: Path) -> list[str]:
    """First-party .c files in the compile database, repository-relative, ignore globs dropped."""
    known = set(git(root, "ls-files", "--cached", "--others", "--exclude-standard").splitlines())
    patterns = ignore_patterns(root)
    files = set()
    for entry in json.loads((build / "compile_commands.json").read_text(encoding="utf-8")):
        path = Path(entry["directory"], entry["file"]).resolve()
        if path.suffix == ".c" and path.is_relative_to(root):
            rel = path.relative_to(root).as_posix()
            if rel in known and not ignored(rel, patterns):
                files.add(rel)
    return sorted(files)


def tidy_binary() -> str:
    """The pinned clang-tidy from the PyPI wheel, resolved once so each file skips uvx."""
    cmd = ["uvx", "--quiet", "--from", f"clang-tidy=={TIDY_VERSION}", "python", "-c"]
    cmd += ["import shutil; print(shutil.which('clang-tidy'))"]
    exe = run(cmd).strip()
    version = run([exe, "--version"])
    if f"version {TIDY_VERSION}" not in version:
        sys.exit(f"lint_c: {exe} is not clang-tidy {TIDY_VERSION}")
    return exe


def tidy_one(exe: str, root: Path, build: Path, rel: str, analyzer: bool) -> list[str]:
    """Run clang-tidy on one file and return its diagnostic lines."""
    cmd = [exe, f"-p={build}", "--quiet"]
    if analyzer:
        cmd.append(f"--checks={ANALYZER}")
    proc = subprocess.run([*cmd, rel], cwd=root, check=False, capture_output=True, text=True)
    lines = [line for line in proc.stdout.splitlines() if TIDY_RE.match(line)]
    if proc.returncode != 0 and not lines:
        tail = (proc.stderr or proc.stdout).strip().splitlines()
        lines = [f"{rel}:0:0: error: {tail[-1] if tail else proc.returncode} [clang-tidy-crash]"]
    return lines


def primary(checks: str) -> str:
    """One name for a finding that clang-tidy reports under aliases (`bugprone-x,cert-y`).

    Most CERT checks are aliases of bugprone-/misc- checks that are also on, so the non-CERT name
    is kept and the finding is counted once.
    """
    names = checks.split(",")
    return next((n for n in names if not n.startswith("cert-")), names[0])


def run_tidy(root: Path, build: Path, files: list[str], jobs: int, analyzer: bool) -> list[Finding]:
    """Run clang-tidy over files in parallel; a header finding counts once."""
    exe = tidy_binary()
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        batches = pool.map(lambda rel: tidy_one(exe, root, build, rel, analyzer), files)
        lines = {line for batch in batches for line in batch}
    findings = set()
    for line in lines:
        m = TIDY_RE.match(line)
        if not m:
            continue
        path = Path(root, m.group("path")).resolve()
        if not path.is_relative_to(root):
            continue
        rel = path.relative_to(root).as_posix()
        findings.add(Finding(primary(m.group("check")), rel, int(m.group("line")), m.group("msg")))
    return sorted(findings, key=lambda f: (f.path, f.line, f.rule))


def prefix_regexes(root: Path) -> list[re.Pattern[str]]:
    """The *IgnoredRegexp patterns of .clang-tidy: the names an exported symbol may take."""
    pats = []
    for line in (root / ".clang-tidy").read_text(encoding="utf-8").splitlines():
        key, _, value = line.strip().partition(":")
        if key.rsplit(".", 1)[-1] in IGNORED_KEYS:
            pats.append(re.compile(value.strip().strip("'")))
    if len(pats) != len(IGNORED_KEYS):
        sys.exit(f"lint_c: .clang-tidy must set {', '.join(IGNORED_KEYS)}")
    return pats


def run_nm(root: Path, build: Path) -> list[Finding]:
    """Compile the simulator's core objects and flag exported symbols outside the prefix allowlist.

    Only core's own objects are built (not LVGL or the library archive), so this costs seconds.
    """
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    sources = {}
    for entry in entries:
        if entry.get("output", "").startswith("core/CMakeFiles/core.dir/"):
            src = Path(entry["directory"], entry["file"]).resolve()
            sources[str(build / entry["output"])] = src.relative_to(root).as_posix()
    run(["ninja", "-C", str(build), *(Path(o).relative_to(build).as_posix() for o in sources)])
    symbols = run(["nm", "-A", "-g", "--defined-only", *sources])
    pats = prefix_regexes(root)
    patterns = ignore_patterns(root)
    findings = []
    for line in symbols.splitlines():
        m = NM_RE.match(line.strip())
        if not m or any(p.match(m.group("sym")) for p in pats):
            continue
        path = sources.get(m.group("obj"), m.group("obj"))
        if not ignored(path, patterns):
            msg = f"exported `{m.group('sym')}` ({m.group('type')}) lacks a module prefix"
            findings.append(Finding("nm-prefix", path, 0, msg))
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
        "# clang-tidy and nm findings held by scripts/check.d/lint_c.py: counts may only go down.",
        "# RULE<TAB>path<TAB>count, sorted. Regenerate with --update-baseline.",
    ]
    lines += [f"{rule}\t{path}\t{n}" for (rule, path), n in sorted(counts.items()) if n]
    BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8")


def skipped(rule: str, args: argparse.Namespace) -> bool:
    """Whether a baseline row belongs to a part this run left out."""
    if rule == "nm-prefix":
        return bool(args.no_nm)
    return rule.startswith("clang-analyzer-") and not args.analyzer


def compare(findings: list[Finding], args: argparse.Namespace) -> bool:
    """Print what differs from the baseline; return True when the run fails."""
    counts = Counter((f.rule, f.path) for f in findings)
    base = Counter({k: v for k, v in load_baseline().items() if not skipped(k[0], args)})
    failed = False
    for key in sorted(counts):
        if counts[key] > base.get(key, 0):
            failed = True
            for f in (f for f in findings if (f.rule, f.path) == key):
                print(f)
            print(f"  ^ {key[0]} in {key[1]}: {counts[key]} now, baseline {base.get(key, 0)}")
    shrunk = [k for k in sorted(base) if counts.get(k, 0) < base[k]]
    for rule, path in shrunk:
        print(f"{path}:0: {rule} {counts.get((rule, path), 0)} now, baseline {base[(rule, path)]}")
    if shrunk:
        failed = True
        print("lint_c: baseline can shrink: run lint_c.py --update-baseline")
    return failed


def main(argv: list[str] | None = None) -> int:
    """Run the linters and compare against the baseline; return the exit code."""
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--update-baseline", action="store_true", help="rewrite the baseline")
    ap.add_argument("--analyzer", action="store_true", help="add clang-analyzer-* (slow)")
    ap.add_argument("--no-nm", action="store_true", help="skip the nm prefix check")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4, help="parallel files")
    args = ap.parse_args(argv)
    for tool in ("uvx", "cmake", "ninja", "nm"):
        if not shutil.which(tool):
            sys.exit(f"lint_c: {tool} not found")

    root = Path(git(HERE, "rev-parse", "--show-toplevel").strip())
    t0 = time.monotonic()
    build = compile_db(root)
    files = c_files(root, build)
    t1 = time.monotonic()
    findings = run_tidy(root, build, files, max(1, args.jobs), args.analyzer)
    t2 = time.monotonic()
    if not args.no_nm:
        findings += run_nm(root, build)
    t3 = time.monotonic()
    times = f"db {t1 - t0:.1f}s, clang-tidy {t2 - t1:.1f}s, nm {t3 - t2:.1f}s"
    print(f"lint_c: {len(files)} files, {times}", file=sys.stderr)

    crashed = [f for f in findings if f.rule in ("clang-tidy-crash", "clang-diagnostic-error")]
    for f in crashed:
        print(f"{f} - run clang-tidy on the file by hand to see why")
    if crashed:
        return 1

    if args.update_baseline:
        keep = Counter({k: v for k, v in load_baseline().items() if skipped(k[0], args)})
        counts = Counter((f.rule, f.path) for f in findings) + keep
        write_baseline(counts)
        print(f"lint_c: baseline rewritten, {sum(counts.values())} findings", file=sys.stderr)
        return 0

    failed = compare(findings, args)
    print(f"lint_c: {'FAIL' if failed else 'ok'}, {len(findings)} findings", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
