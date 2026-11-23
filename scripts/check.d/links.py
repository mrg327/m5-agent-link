#!/usr/bin/env python3
"""Check that every relative Markdown link and image in first-party docs resolves (DOC-26).

Usage: python3 scripts/check.d/links.py   (from anywhere inside the repository)

For each tracked first-party .md file, follow every relative inline link, image and reference
definition, outside code blocks and code spans:
  - the target file or directory must exist;
  - a `#anchor` on a .md target (or a bare `#anchor`) must name a heading in that file, using
    GitHub's slug rules, or an explicit `<a name="...">` / `id="..."`.
An absolute path (`/home/...`, `/docs/...`) is an error too (DOC-25).
Skipped: URLs with a scheme (https:, mailto:, ...), vendored mirrors (DOC-4) and targets that are
gitignored, such as the PDFs `scripts/fetch_docs.py` downloads.
Prints `path:line: DOC-26 message — how to fix` for each break and exits 1 if there are any.
Standard library only (Python 3.11).
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import urllib.parse

# Vendored mirrors written by scripts/fetch_docs.py (DOC-4): not edited by hand, so not checked.
VENDORED = (
    "docs/StopWatch.md",
    "docs/arduino/",
    "docs/esp_idf/",
    "docs/github/",
    "docs/guide/",
    "docs/tools/",
    "docs/uiflow2/",
)

FENCE_RE = re.compile(r"^ {0,3}(`{3,}|~{3,})")
CODE_SPAN_RE = re.compile(r"(`+)(?:(?!\1).)+?\1")
INLINE_RE = re.compile(
    r"!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*<?([^)\s>]+)>?(?:\s+(?:\"[^\"]*\"|'[^']*'))?\s*\)"
)
REF_DEF_RE = re.compile(r"^ {0,3}\[[^\]]+\]:\s*<?(\S+?)>?(?:\s|$)")
HEADING_RE = re.compile(r"^ {0,3}(#{1,6})\s+(.*?)\s*#*\s*$")
SETEXT_RE = re.compile(r"^ {0,3}(=+|-+)\s*$")
HTML_ANCHOR_RE = re.compile(r"<a\s[^>]*?(?:name|id)=[\"']([^\"']+)[\"']", re.I)
SCHEME_RE = re.compile(r"^[a-z][a-z0-9+.-]*:", re.I)


def first_party(path: str) -> bool:
    return path.endswith(".md") and not any(
        path == v or (v.endswith("/") and path.startswith(v)) for v in VENDORED
    )


def slug(text: str) -> str:
    """GitHub's heading anchor: rendered text, lower case, punctuation dropped, spaces to dashes."""
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)  # links and images keep their text
    text = re.sub(r"<[^>]+>", "", text)  # inline HTML
    text = re.sub(r"(\*\*|__|\*|`|~~)", "", text)  # emphasis and code markers
    text = re.sub(r"(?<![\w])_|_(?![\w])", "", text)  # _emphasis_ but not snake_case
    text = text.strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


def scan(text: str):
    """Yield (line_no, kind, value) for links and anchors outside code. kind: link | anchor."""
    fence = None
    seen: dict[str, int] = {}
    prev = ""
    for no, line in enumerate(text.splitlines(), 1):
        m = FENCE_RE.match(line)
        if fence:
            if m and m.group(1)[0] == fence[0] and len(m.group(1)) >= len(fence):
                fence = None
            prev = ""
            continue
        if m:
            fence = m.group(1)
            prev = ""
            continue
        heading = None
        h = HEADING_RE.match(line)
        if h:
            heading = h.group(2)
        elif (
            SETEXT_RE.match(line) and prev.strip() and not prev.lstrip().startswith(("-", "*", "|"))
        ):
            heading = prev.strip()
        if heading is not None:
            base = slug(heading)
            n = seen.get(base, 0)
            seen[base] = n + 1
            yield no, "anchor", base if n == 0 else f"{base}-{n}"
        for a in HTML_ANCHOR_RE.findall(line):
            yield no, "anchor", a
        code_free = CODE_SPAN_RE.sub("", line)
        for target in INLINE_RE.findall(code_free):
            yield no, "link", target
        r = REF_DEF_RE.match(code_free)
        if r:
            yield no, "link", r.group(1)
        prev = line


def main() -> int:
    root = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
    ).stdout.strip()
    os.chdir(root)
    files = [
        f
        for f in subprocess.run(
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "*.md"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.split("\0")
        if f and first_party(f) and os.path.isfile(f)
    ]

    anchors: dict[str, set[str]] = {}
    links: list[tuple[str, int, str]] = []

    def load(path: str) -> set[str]:
        if path not in anchors:
            try:
                with open(path, encoding="utf-8-sig") as fh:
                    found = list(scan(fh.read()))
            except (OSError, UnicodeDecodeError):
                found = []
            anchors[path] = {v for _, k, v in found if k == "anchor"}
            if path in files_set:
                links.extend((path, no, v) for no, k, v in found if k == "link")
        return anchors[path]

    files_set = set(files)
    for f in files:
        load(f)

    missing: list[tuple[str, int, str, str]] = []  # (file, line, target, resolved path)
    errors: list[str] = []
    for f, no, target in links:
        if target.startswith("/"):
            errors.append(
                f"{f}:{no}: DOC-25 absolute link '{target}'"
                " — make it relative to this file, so it works in a clone and on GitHub"
            )
            continue
        if SCHEME_RE.match(target):
            continue  # external: checked weekly, not per change (DOC-26)
        path_part, _, anchor = target.partition("#")
        path_part = urllib.parse.unquote(path_part)
        anchor = urllib.parse.unquote(anchor)
        dest = os.path.normpath(os.path.join(os.path.dirname(f), path_part)) if path_part else f
        if not os.path.exists(dest):
            missing.append((f, no, target, dest))
            continue
        if anchor and dest.endswith(".md") and os.path.isfile(dest):
            if anchor.lower() not in {a.lower() for a in load(dest)}:
                errors.append(
                    f"{f}:{no}: DOC-26 anchor '#{anchor}' not found in {dest}"
                    " — link to an existing heading (GitHub slug: lower case, dashes)"
                )

    if missing:  # skip targets that are gitignored (fetched, not committed)
        out = subprocess.run(
            ["git", "check-ignore", "--no-index", "--stdin", "-z"],
            input="\0".join(d for *_, d in missing),
            capture_output=True,
            text=True,
        ).stdout
        ignored = set(out.split("\0"))
        for f, no, target, dest in missing:
            if dest not in ignored:
                errors.append(
                    f"{f}:{no}: DOC-26 link target '{target}' does not exist"
                    " — fix the relative path or drop the link"
                )

    for e in sorted(errors):
        print(e)
    print(
        f"links: {len(files)} first-party .md files, {len(links)} links (external ones skipped),"
        f" {len(errors)} broken"
    )
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
