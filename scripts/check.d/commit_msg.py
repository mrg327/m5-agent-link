#!/usr/bin/env python3
"""Check commit messages against docs/conventions/changes.md (CHG-16 to CHG-28).

Usage:
  python3 scripts/check.d/commit_msg.py --file PATH     # one message (commit-msg hook)
  python3 scripts/check.d/commit_msg.py [--range A..B]  # each commit in a range (CI, scripts/check)
  options: --since-rev REV (default: baselines/commit_msg_since.txt), --strict-areas, --quiet

Range mode checks every commit reachable from B (default HEAD) and not from A, minus everything
reachable from the cutoff revision, so history from before the rules is never checked. Merges are
checked against the merge-subject rule only (CHG-28, CHG-29).

Rules (each failure prints `where:line: CHG-n message — how to fix`):
  CHG-16  subject is `area: summary` or `a, b: summary`, lowercase areas; a work commit (not
          `issue:`, not a merge) names an issue ID YYYYMMDD-xxxx, in the subject or the body
  CHG-17  summary does not start with a past-tense or third-person verb (a short list)
  CHG-18  subject at most 72 characters, no trailing full stop
  CHG-19  summary is not on the deny list (`wip`, `fix bug`, `update`, `phase 1`, ...)
  CHG-20  a blank line follows the subject
  CHG-21  body lines at most 72 columns; URLs, trailers, indented lines and fenced code exempt
  CHG-22  trailers go last, in one paragraph after a blank line
  CHG-23  areas are spelled one way (`issue` not `issues`); an area outside the list warns
  CHG-26  `Co-Authored-By` spelled so; with --file and AGENT_NAME set, the trailer is required and
          `Signed-off-by` is refused
  CHG-28  a merge subject is `Merge branch 'x': summary (issue ID)` or git's default
          `Merge branch 'main' into x`

Exit status: 0 clean (warnings allowed), 1 on any failure, 2 on a usage error.
Standard library only (Python 3.11).
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
SINCE_FILE = HERE / "baselines" / "commit_msg_since.txt"

MAX_SUBJECT = 72
MAX_BODY = 72

SUBJECT_RE = re.compile(r"^([a-z0-9/_-]+(?:, [a-z0-9/_-]+)*): (\S.*)$")
ISSUE_ID_RE = re.compile(r"\b\d{8}-[0-9a-f]{4}\b")
URL_RE = re.compile(r"\b[a-z][a-z0-9+.-]*://\S+", re.I)
TRAILER_RE = re.compile(r"^([A-Za-z][A-Za-z0-9-]*): \S")
FENCE_RE = re.compile(r"^\s*(```|~~~)")
# Git's default merge subjects (CHG-29), optionally followed by `: summary`.
MERGE_DEFAULT_RE = re.compile(
    r"^Merge (?:remote-tracking )?branch '[^']+'(?: of \S+)? into \S+(?:: \S.*)?$"
)
MERGE_SUMMARY_RE = re.compile(r"^Merge branch '[^']+': \S.*$")

# CHG-23: top-level components, then the module and crate names the page lists.
AREAS = {
    "firmware", "sim", "daemon", "protocol", "atlas", "bench", "board", "tools", "scripts",
    "docs", "ci", "issue",
    "ui", "nav", "power", "ble", "ota", "voice", "jobs", "keys", "core",
    "agentlinkd", "agentlink", "hook",
}  # fmt: skip
# CHG-23: "Spell each one one way."
AREA_SPELLING = {"issues": "issue", "proto": "protocol", "simulator": "sim", "hooks": "hook"}

# CHG-19: summaries that say nothing in `git log --oneline`.
DENY_RE = re.compile(
    r"^(wip|fix(es)?|fix bugs?|bug ?fix(es)?|updates?|update \w+|changes|cleanup|misc|tweaks?"
    r"|minor( changes| fixes)?|phase \d+|more|stuff|todo)\.?$",
    re.I,
)
# CHG-17: leading words that are not imperative. Kept short on purpose: a heuristic, not grammar.
NOT_IMPERATIVE = {
    w: w2
    for w, w2 in [
        ("added", "add"), ("adds", "add"), ("adding", "add"),
        ("fixed", "fix"), ("fixes", "fix"), ("fixing", "fix"),
        ("updated", "update"), ("updates", "update"), ("updating", "update"),
        ("removed", "remove"), ("removes", "remove"), ("removing", "remove"),
        ("changed", "change"), ("changes", "change"), ("changing", "change"),
        ("renamed", "rename"), ("renames", "rename"), ("renaming", "rename"),
        ("moved", "move"), ("moves", "move"), ("moving", "move"),
        ("dropped", "drop"), ("drops", "drop"), ("dropping", "drop"),
        ("implemented", "implement"), ("implements", "implement"),
        ("refactored", "refactor"), ("refactors", "refactor"), ("refactoring", "refactor"),
        ("improved", "improve"), ("improves", "improve"),
        ("created", "create"), ("creates", "create"),
        ("deleted", "delete"), ("deletes", "delete"),
        ("bumped", "bump"), ("bumps", "bump"),
        ("reverted", "revert"), ("reverts", "revert"),
    ]
}  # fmt: skip

KNOWN_TRAILERS = {"co-authored-by", "claude-session", "fixes", "signed-off-by"}


@dataclass
class Report:
    """Failures and warnings for one message."""

    where: str
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def err(self, line: int, rule: str, msg: str) -> None:
        """Record a failure at a 1-based message line."""
        self.errors.append(f"{self.where}:{line}: {rule} {msg}")

    def warn(self, line: int, rule: str, msg: str) -> None:
        """Record a warning at a 1-based message line."""
        self.warnings.append(f"{self.where}:{line}: {rule} warning: {msg}")


def clean_message(text: str) -> list[str]:
    """Return the message lines as git stores them: no comments, scissors part or edge blanks."""
    lines = []
    for line in text.splitlines():
        if re.match(r"^# -+ >8 -+$", line):
            break
        if line.startswith("#"):
            continue
        lines.append(line.rstrip())
    while lines and not lines[0]:
        lines.pop(0)
    while lines and not lines[-1]:
        lines.pop()
    return lines


def trailer_block(lines: list[str]) -> int:
    """Return the index where the final trailer paragraph starts, or len(lines) if there is none."""
    end = len(lines)
    start = end
    while start > 1 and lines[start - 1]:
        start -= 1
    if start <= 1 or start == end:
        return end
    para = lines[start:end]
    if all(TRAILER_RE.match(x) for x in para) and any(
        TRAILER_RE.match(x).group(1).lower() in KNOWN_TRAILERS for x in para
    ):
        return start
    return end


def check_merge(subject: str, lines: list[str], rep: Report) -> None:
    """CHG-28/29: merge subjects; the body is free (git writes conflict lists there)."""
    if MERGE_DEFAULT_RE.match(subject):
        return
    if MERGE_SUMMARY_RE.match(subject):
        if not ISSUE_ID_RE.search("\n".join(lines)):
            rep.err(
                1,
                "CHG-28",
                "merge names no issue ID — add the issues the branch closes: "
                "(issue YYYYMMDD-xxxx) or (issues A, B)",
            )
        return
    rep.err(
        1,
        "CHG-28",
        f"merge subject {subject!r} — use \"Merge branch 'x': what it adds (issue ID)\" "
        "or git's default \"Merge branch 'main' into x\"",
    )


def check_message(
    text: str, where: str, *, is_merge: bool | None = None, hook: bool = False,
    strict_areas: bool = False,
) -> Report:  # fmt: skip
    """Check one commit message; `hook` turns on the AGENT_NAME trailer rules (CHG-26)."""
    rep = Report(where)
    lines = clean_message(text)
    if not lines:
        rep.err(1, "CHG-16", "empty message — write `area: summary (issue ID)`")
        return rep
    subject = lines[0]
    if is_merge is None:
        is_merge = subject.startswith("Merge ")
    if is_merge:
        check_merge(subject, lines, rep)
        return rep
    if hook and re.match(r"^(fixup|squash|amend)! ", subject):
        return rep  # squashed away before it reaches main; the range check sees the result

    # Subject shape (CHG-16), length and full stop (CHG-18).
    m = SUBJECT_RE.match(subject)
    areas: list[str] = []
    summary = subject
    if not m:
        rep.err(
            1,
            "CHG-16",
            f"subject {subject!r} is not `area: summary` — start with a lowercase area from "
            "changes.md §5 and a colon, e.g. `daemon: …`; two areas as `docs, protocol: …`",
        )
    else:
        areas = m.group(1).split(", ")
        summary = m.group(2)
        for a in areas:
            if a in AREA_SPELLING:
                rep.err(1, "CHG-23", f"area `{a}` — spell it `{AREA_SPELLING[a]}`")
            elif a not in AREAS and a.split("/")[0] not in AREAS:
                msg = f"area `{a}` is not in the changes.md §5 list — use the narrowest listed area"
                (rep.err if strict_areas else rep.warn)(1, "CHG-23", msg)
        if len(areas) > 2:
            rep.warn(
                1, "CHG-24", f"{len(areas)} areas — three or more usually means split the commit"
            )
    if len(subject) > MAX_SUBJECT:
        rep.err(
            1,
            "CHG-18",
            f"subject is {len(subject)} characters (max {MAX_SUBJECT}) — move detail to the body",
        )
    if subject.endswith("."):
        rep.err(1, "CHG-18", "subject ends in a full stop — drop it")

    # Deny list (CHG-19) and imperative mood (CHG-17), on the summary without the issue suffix.
    bare = re.sub(r"\s*\((issues?\s+)?[^()]*\d{8}-[0-9a-f]{4}[^()]*\)\s*$", "", summary).strip()
    if DENY_RE.match(bare):
        rep.err(1, "CHG-19", f"summary {bare!r} says nothing on its own — say what changed and why")
    first = bare.split(" ", 1)[0].lower().rstrip(",:;")
    if first in NOT_IMPERATIVE:
        rep.err(
            1,
            "CHG-17",
            f"summary starts with {first!r} — use the imperative: {NOT_IMPERATIVE[first]!r}",
        )

    # Issue ID (CHG-16, changes.md §8 item 3): work commits only.
    if "issue" not in areas and not ISSUE_ID_RE.search("\n".join(lines)):
        rep.err(
            1,
            "CHG-16",
            "no issue ID — add `(issue YYYYMMDD-xxxx)` to the subject (or the body if the "
            "subject is full); file one with scripts/issue",
        )

    # Blank line after the subject (CHG-20).
    if len(lines) > 1 and lines[1]:
        rep.err(2, "CHG-20", "no blank line after the subject — insert one")

    # Body wrapping (CHG-21) and trailer placement (CHG-22).
    tstart = trailer_block(lines)
    in_fence = False
    for i in range(1, tstart):
        line = lines[i]
        if FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence or not line:
            continue
        exempt = URL_RE.search(line) or line.startswith(("    ", "\t"))
        if len(line) > MAX_BODY and not exempt:
            rep.err(
                i + 1,
                "CHG-21",
                f"body line is {len(line)} columns (max {MAX_BODY}) — rewrap at 72; "
                "indent pasted output by 4 spaces to exempt it",
            )
        t = TRAILER_RE.match(line)
        if t and t.group(1).lower() in KNOWN_TRAILERS and i >= 2:
            rep.err(
                i + 1,
                "CHG-22",
                f"`{t.group(1)}:` trailer is not in the last paragraph — move trailers to the "
                "end, after a blank line",
            )

    # Trailer spelling and the agent rules (CHG-26).
    keys = []
    for i in range(tstart, len(lines)):
        t = TRAILER_RE.match(lines[i])
        if not t:
            continue
        key = t.group(1)
        keys.append(key.lower())
        if key.lower() == "co-authored-by" and key != "Co-Authored-By":
            rep.err(i + 1, "CHG-26", f"trailer spelled `{key}` — write `Co-Authored-By`")
    if hook and os.environ.get("AGENT_NAME"):
        if "co-authored-by" not in keys:
            rep.err(
                len(lines),
                "CHG-26",
                "no `Co-Authored-By:` trailer (AGENT_NAME is set) — add one naming the agent and "
                "model, e.g. `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`",
            )
        if "signed-off-by" in keys:
            rep.err(len(lines), "CHG-26", "an agent never adds `Signed-off-by:` — remove it")
    return rep


def git(*args: str) -> str:
    """Run git in the repository and return stdout; raise on failure."""
    return subprocess.run(
        ["git", "-C", str(HERE), *args], check=True, capture_output=True, text=True
    ).stdout


def read_since(arg: str | None) -> str | None:
    """Return the cutoff revision: --since-rev, else the baseline file, else None."""
    if arg:
        return arg
    if SINCE_FILE.exists():
        for line in SINCE_FILE.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                return line
    return None


def range_commits(rng: str | None, since: str | None) -> list[tuple[str, bool]]:
    """Return (sha, is_merge) for each commit to check, oldest first."""
    if rng is None:
        spec = ["HEAD"]
    elif ".." in rng:
        a, b = rng.split("..", 1)
        spec = [b or "HEAD"] + ([f"^{a}"] if a else [])
    else:
        spec = [rng]
    if since:
        spec.append(f"^{since}")
    out = git("rev-list", "--reverse", "--parents", *spec, "--")
    res = []
    for row in out.splitlines():
        parts = row.split()
        res.append((parts[0], len(parts) > 2))
    return res


def main(argv: list[str]) -> int:
    """Parse arguments, check the messages, print the results; return the exit status."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--file", help="check one message file (commit-msg hook)")
    mode.add_argument("--range", help="check commits in A..B (default: HEAD, after the cutoff)")
    ap.add_argument("--since-rev", help=f"cutoff revision (default: {SINCE_FILE.name})")
    ap.add_argument("--strict-areas", action="store_true", help="unknown areas fail, not warn")
    ap.add_argument("-q", "--quiet", action="store_true", help="print failures and summary only")
    args = ap.parse_args(argv)

    reports = []
    if args.file:
        try:
            text = Path(args.file).read_text(encoding="utf-8", errors="replace")
        except OSError as e:
            print(f"commit_msg: cannot read {args.file}: {e}", file=sys.stderr)
            return 2
        reports.append(check_message(text, args.file, hook=True, strict_areas=args.strict_areas))
    else:
        since = read_since(args.since_rev)
        try:
            if since:
                git("cat-file", "-e", f"{since}^{{commit}}")
            commits = range_commits(args.range, since)
        except subprocess.CalledProcessError as e:
            print(
                f"commit_msg: git failed: {e.stderr.strip() or e} — check the range; a shallow "
                f"clone needs full history (actions/checkout fetch-depth: 0) for cutoff {since}",
                file=sys.stderr,
            )
            return 1
        for sha, is_merge in commits:
            text = git("log", "-1", "--format=%B", sha)
            reports.append(
                check_message(text, sha[:12], is_merge=is_merge, strict_areas=args.strict_areas)
            )

    n_err = sum(len(r.errors) for r in reports)
    n_warn = sum(len(r.warnings) for r in reports)
    n_bad = sum(1 for r in reports if r.errors)
    for r in reports:
        for line in r.errors:
            print(line)
        if not args.quiet:
            for line in r.warnings:
                print(line)
    what = "message" if args.file else "commit(s)"
    status = "FAIL" if n_err else "ok"
    print(
        f"commit_msg: {status}: {len(reports)} {what} checked, {n_bad} failing, "
        f"{n_err} error(s), {n_warn} warning(s)"
    )
    if n_err and args.file:
        print("commit_msg: rules in docs/conventions/changes.md §5; to retry, run "
              '`git commit -e -F "$(git rev-parse --git-dir)/COMMIT_EDITMSG"`')  # fmt: skip
    return 1 if n_err else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
