#!/usr/bin/env python3
"""Report documentation coverage per language and hold it to a baseline that only shrinks.

Usage: python3 scripts/check.d/doc_coverage.py [--update-baseline] [--list] [--lang c,rust,py,js]

What counts as documented (a text scan, not a compiler; keys are `lang path [kind] symbol`):
  c     C-DOC-02  every function declared in a public header (firmware/components/*/include,
                  firmware/boards/*/*/include; generated zcbor_* and agentlink_codec_* excluded)
                  has a comment directly above it or trailing on its line. Two levels: no comment
                  at all is `undocumented` and is held to the baseline; a plain /* */ or // comment
                  instead of a Doxygen /** */ block is reported as info only, until phase 4.
  rust  RS-31     every `pub` item, `pub` field, variant of a `pub` enum and method of a `pub`
                  trait in the library crates (agentlink-proto, -keys, -delta) has `///` docs.
                  Test modules are skipped. Items made by macros are not seen: `missing_docs`
                  (cargo) covers those once RS-31 is enforced.
  py      PY-21     every first-party module has a docstring, and so does each public (no leading
                  `_`) top-level function, class, and method of a public class. test_*.py exempt.
  js    JS-15     every exported function, class and arrow-function const in atlas/js,
                  atlas/tools, tools/, firmware/tools and daemon/integrations has a /** */ block.

Fails (exit 1) on an undocumented item not in scripts/check.d/baselines/doc_coverage.txt, and on a
baseline entry that no longer occurs (so the baseline only shrinks). --update-baseline rewrites it.
Standard library only (Python 3.11).
"""

from __future__ import annotations

import argparse
import ast
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASELINE = HERE / "baselines" / "doc_coverage.txt"
RULES = {"c": "C-DOC-02", "rust": "RS-31", "py": "PY-21", "js": "JS-15"}
RUST_CRATES = ("agentlink-proto", "agentlink-keys", "agentlink-delta")
JS_ROOTS = ("atlas/js/", "atlas/tools/", "tools/", "firmware/tools/", "daemon/integrations/")


@dataclass
class Item:
    """One undocumented item: its stable key and where it is now."""

    lang: str
    path: str
    line: int
    symbol: str
    kind: str = ""

    @property
    def key(self) -> str:
        """The baseline key: no line numbers, so reformatting does not change it."""
        return " ".join(x for x in (self.lang, self.path, self.kind, self.symbol) if x)


def repo_root() -> Path:
    """Return the top of the working tree."""
    out = subprocess.run(
        ["git", "-C", str(HERE), "rev-parse", "--show-toplevel"],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(out.stdout.strip())


def repo_files(root: Path) -> list[str]:
    """Tracked and untracked-but-not-ignored files, repo-relative."""
    out = subprocess.run(
        ["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard"],
        check=True,
        capture_output=True,
        text=True,
    )
    return sorted({p for p in out.stdout.splitlines() if (root / p).is_file()})


# ---- C -------------------------------------------------------------------------------------------

C_HEADER_RE = re.compile(
    r"^firmware/(components/[^/]+/include|boards/[^/]+/[^/]+/include)/[^/]+\.h$"
)
C_EXCLUDE_RE = re.compile(r"/(zcbor_|agentlink_codec_)[^/]*\.h$")


def c_strip_strings(line: str) -> str:
    """Blank out string literals so their braces and comment markers do not count."""
    return re.sub(r'"(\\.|[^"\\])*"', '""', line)


def c_scan(text: str) -> list[tuple[str, int, str]]:
    """Return (name, line, doc) per top-level function declaration; doc is doxy, plain or none.

    A heuristic, not a C parser (from the phase 0 tooling.md measurement): a declaration is a
    statement at brace depth 0 (outside extern "C"), not a typedef, containing '(' and ending ');'.
    """
    lines = text.splitlines()
    decls = []
    depth = 0
    last_comment = None  # (kind, index of its last line)
    in_block = None
    stmt = None  # (start index, text)
    for i, raw in enumerate(lines):
        line = c_strip_strings(raw)
        s = line.strip()
        if in_block is not None:
            if "*/" in s:
                last_comment = (in_block, i)
                in_block = None
            continue
        if stmt is None:
            kind = None
            if s.startswith(("/**", "/*!")):
                kind = "doxy"
            elif s.startswith("/*"):
                kind = "plain"
            elif s.startswith(("///", "//!")):
                last_comment = ("doxy", i)
                continue
            elif s.startswith("//"):
                last_comment = ("plain", i)
                continue
            if kind is not None:
                if "*/" in s[2:]:
                    last_comment = (kind, i)
                else:
                    in_block = kind
                continue
            if not s:
                last_comment = None  # a blank line detaches the comment
                continue
            if s.startswith("#"):
                continue
        code = re.sub(r"/\*.*?\*/", "", line).split("//")[0]
        if 'extern "C"' in raw or (s == "}" and depth == 0):
            continue
        if stmt is None:
            stmt = (i, "")
        stmt = (stmt[0], stmt[1] + " " + code.strip())
        depth += code.count("{") - code.count("}")
        if depth <= 0 and (";" in code or code.rstrip().endswith("}")):
            start, st = stmt
            st = st.strip()
            if (
                "(" in st
                and st.endswith(");")
                and not st.startswith("typedef")
                and not re.match(r"[^(]*\(\s*\*", st)
                and "=" not in st.split("(")[0]
            ):
                m = re.search(r"([A-Za-z_]\w*)\s*\(", st)
                name = m.group(1) if m else "?"
                trailing = raw[raw.find(";") :] if ";" in raw else ""
                if "/**<" in trailing or "///<" in trailing:
                    doc = "doxy"
                elif last_comment and last_comment[1] == start - 1:
                    doc = last_comment[0]
                elif "/*" in trailing or "//" in trailing:
                    doc = "plain"
                else:
                    doc = "none"
                decls.append((name, start + 1, doc))
            stmt = None
            depth = max(depth, 0)
            last_comment = None  # consecutive declarations do not share one comment
    return decls


def scan_c(root: Path, files: list[str]) -> tuple[list[Item], str]:
    """Undocumented public-header functions, and an info line with the comment styles."""
    items = []
    tot = {"doxy": 0, "plain": 0, "none": 0}
    heads = [p for p in files if C_HEADER_RE.match(p) and not C_EXCLUDE_RE.search(p)]
    for p in heads:
        for name, line, doc in c_scan((root / p).read_text(encoding="utf-8", errors="replace")):
            tot[doc] += 1
            if doc == "none":
                items.append(Item("c", p, line, name, "undocumented"))
    n = sum(tot.values())
    info = (
        f"c: {len(heads)} public headers, {n} functions: {tot['doxy']} /** */, "
        f"{tot['plain']} plain /* */ or // (info: C-DOC-02 wants /** */), {tot['none']} none"
    )
    return items, info


# ---- Rust ----------------------------------------------------------------------------------------

RS_ITEM_RE = re.compile(
    r"^(?P<vis>pub(?:\s*\([^)]*\))?\s+)?(?:default\s+)?(?:const\s+(?=fn|unsafe|async|extern))?"
    r"(?:async\s+)?(?:unsafe\s+)?(?:extern\s+\"[^\"]*\"\s+)?"
    r"(?P<kw>fn|struct|enum|trait|type|const|static|mod|union|impl|use|macro_rules!)\b\s*"
    r"(?:mut\s+)?(?P<name>[A-Za-z_]\w*)?"
)
RS_FIELD_RE = re.compile(r"^pub\s+(?P<name>[A-Za-z_]\w*)\s*:")
RS_VARIANT_RE = re.compile(r"^(?P<name>[A-Z]\w*)\s*(?:[({=,]|$)")


def rs_clean(line: str) -> str:
    """Drop string and char literals and line comments, keeping braces that are code."""
    line = re.sub(r'"(\\.|[^"\\])*"', '""', line)
    line = re.sub(r"'(\\.|[^'\\])'", "' '", line)
    return line.split("//")[0]


def rs_strip_generics(s: str) -> str:
    """Remove balanced <...> groups so `impl<T: X<Y>> Foo<T>` reads as `impl Foo`."""
    out, depth = [], 0
    for ch in s:
        if ch == "<":
            depth += 1
        elif ch == ">" and depth:
            depth -= 1
        elif not depth:
            out.append(ch)
    return "".join(out)


def rs_scan(text: str) -> list[tuple[str, int]]:
    """Return (symbol, line) for each public item without `///` docs in one source file."""
    missing = []
    # Types declared without plain `pub` in this file: their inherent methods are not public API.
    private = set(re.findall(r"(?m)^\s*(?:pub\s*\([^)]*\)\s+)?(?:struct|enum|union)\s+(\w+)", text))
    private -= set(re.findall(r"(?m)^\s*pub\s+(?:struct|enum|union)\s+(\w+)", text))
    # Frames: (kind, name, public). Kinds: mod impl trait struct enum skip.
    stack: list[tuple[str, str, bool]] = [("mod", "", True)]
    doc = False
    test_attr = False
    pending: tuple[str, str, bool] | None = None
    in_comment = False
    attr_open = 0
    for i, raw in enumerate(text.splitlines()):
        s = raw.strip()
        if in_comment:
            if "*/" in s:
                in_comment = False
            continue
        if attr_open:
            attr_open += s.count("[") - s.count("]")
            continue
        top = stack[-1]
        if top[0] != "skip":
            if s.startswith("///") or s.startswith("#[doc") or s.startswith("/**"):
                doc = True
                if s.startswith("/**") and "*/" not in s[3:]:
                    in_comment = True
                continue
            if s.startswith("/*"):
                in_comment = "*/" not in s[2:]
                continue
            if s.startswith("//") or not s:
                continue
            if s.startswith("#[") or s.startswith("#!["):
                if re.match(r"#\[cfg\((test|all\(test)", s):
                    test_attr = True
                attr_open = s.count("[") - s.count("]")
                continue
        code = rs_clean(raw).strip()
        if top[0] != "skip" and pending is None:
            # Strip leading inline attributes on fields, e.g. `#[n(0)] pub x: u8`.
            body = re.sub(r"^(#\[[^\]]*\]\s*)+", "", code)
            kind, tname, tpub = top
            if kind in ("struct",):
                m = RS_FIELD_RE.match(body)
                if m and tpub and not doc:
                    missing.append((f"{tname}.{m.group('name')}", i + 1))
            elif kind == "enum":
                names = []
                if "(" not in body and "{" not in body and "=" not in body:
                    names = [x.strip() for x in body.rstrip(",").split(",") if x.strip()]
                    names = [x for x in names if re.fullmatch(r"[A-Z]\w*", x)]
                else:
                    m = RS_VARIANT_RE.match(body)
                    names = [m.group("name")] if m else []
                if tpub and not doc:
                    missing.extend((f"{tname}::{n}", i + 1) for n in names)
                if "{" in body:  # struct-like variant: its fields are not tracked
                    pending = ("skip", "", False)
            else:
                m = RS_ITEM_RE.match(body)
                if m:
                    kw, name = m.group("kw"), m.group("name") or ""
                    vis = (m.group("vis") or "").strip()
                    is_pub = vis == "pub" or (kind == "trait" and tpub)
                    if kw == "impl":
                        head = rs_strip_generics(body)
                        hm = re.match(r"impl\s+(?:(?P<tr>[\w:]+)\s+for\s+)?(?P<ty>[\w:]+)", head)
                        ty = hm.group("ty").split("::")[-1] if hm else "?"
                        inherent = bool(hm and not hm.group("tr"))
                        pending = ("impl", ty, inherent and ty not in private)
                    elif kw == "macro_rules!":
                        pending = ("skip", "", False)
                    elif kw == "use":
                        pass
                    elif test_attr:
                        pending = ("skip", "", False)
                    else:
                        in_impl = kind == "impl"
                        reportable = is_pub and (not in_impl or tpub)
                        if kind == "impl" and not tpub:
                            reportable = False  # trait impl methods inherit the trait's docs
                        sym = f"{tname}::{name}" if kind in ("impl", "trait") and tname else name
                        if reportable and not doc and kw != "mod":
                            missing.append((sym, i + 1))
                        if kw == "mod" and reportable and not doc and not code.endswith(";"):
                            missing.append((sym, i + 1))
                        if kw in ("struct", "enum", "trait", "union"):
                            pending = (
                                "struct" if kw == "union" else kw,
                                name,
                                vis == "pub",
                            )
                        elif kw == "mod":
                            pending = ("mod", name, vis == "pub")
                        else:
                            pending = ("skip", "", False)
                test_attr = False
            doc = False
        # Brace accounting: the first `{` after an item header opens that item's frame.
        for ch in code:
            if ch == "{":
                # A brace that opens no tracked item (a fn body, a block) hides what is inside.
                stack.append(pending if pending is not None else ("skip", "", False))
                pending = None
            elif ch == "}":
                if len(stack) > 1:
                    stack.pop()
            elif ch == ";" and pending is not None and pending[0] != "enum":
                pending = None  # `pub struct X;`, `mod x;`, `fn f();` in a trait
            elif ch == "(" and pending is not None and pending[0] == "struct" and "{" not in code:
                pending = ("skip", "", False)  # tuple struct: positional fields are not tracked
    return missing


def scan_rust(root: Path, files: list[str]) -> list[Item]:
    """Undocumented public items in the library crates."""
    items = []
    for crate in RUST_CRATES:
        pre = f"daemon/crates/{crate}/src/"
        for p in files:
            if p.startswith(pre) and p.endswith(".rs"):
                text = (root / p).read_text(encoding="utf-8", errors="replace")
                for sym, line in rs_scan(text):
                    items.append(Item("rust", p, line, sym))
    return items


# ---- Python --------------------------------------------------------------------------------------


def scan_py(root: Path, files: list[str]) -> list[Item]:
    """Modules, public functions, classes and methods without a docstring."""
    items = []
    for p in files:
        if (
            not p.endswith(".py")
            or Path(p).name.startswith("test_")
            or p.startswith("docs/esp_idf/")
        ):
            continue
        try:
            tree = ast.parse((root / p).read_text(encoding="utf-8", errors="replace"), p)
        except SyntaxError as e:
            items.append(Item("py", p, e.lineno or 1, "<syntax-error>"))
            continue
        if ast.get_docstring(tree) is None:
            items.append(Item("py", p, 1, "<module>"))
        defs = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
        for node in tree.body:
            if not isinstance(node, defs) or node.name.startswith("_"):
                continue
            if ast.get_docstring(node) is None:
                items.append(Item("py", p, node.lineno, node.name))
            if isinstance(node, ast.ClassDef):
                for sub in node.body:
                    if (
                        isinstance(sub, defs)
                        and not sub.name.startswith("_")
                        and ast.get_docstring(sub) is None
                    ):
                        items.append(Item("py", p, sub.lineno, f"{node.name}.{sub.name}"))
    return items


# ---- JS ------------------------------------------------------------------------------------------

JS_EXPORT_RE = re.compile(
    r"^export\s+(?:default\s+)?(?:(?:async\s+)?function\s*\*?\s*(?P<fn>[A-Za-z_$][\w$]*)?"
    r"|class\s+(?P<cls>[A-Za-z_$][\w$]*)"
    r"|(?:const|let|var)\s+(?P<var>[A-Za-z_$][\w$]*)\s*(?::[^=]+)?=\s*(?P<rest>.*))"
)
JS_FUNC_VALUE_RE = re.compile(
    r"^(?:async\s+)?(?:function\b|\([^;]*?\)\s*(?::[^=]+)?=>|[A-Za-z_$][\w$]*\s*=>)", re.S
)


def js_has_jsdoc(lines: list[str], i: int) -> bool:
    """True when the nearest non-blank line above line i ends a /** */ block."""
    j = i - 1
    while j >= 0 and not lines[j].strip():
        j -= 1
    if j < 0 or not lines[j].rstrip().endswith("*/"):
        return False
    while j >= 0:
        s = lines[j].strip()
        if s.startswith("/**"):
            return True
        if s.startswith("/*"):
            return False
        j -= 1
    return False


def scan_js(root: Path, files: list[str]) -> list[Item]:
    """Exported functions, classes and arrow-function consts without JSDoc."""
    items = []
    for p in files:
        if not p.startswith(JS_ROOTS) or not re.search(r"\.(js|mjs|ts)$", p):
            continue
        if re.search(r"\.test\.(js|mjs|ts)$", p) or "/vendor/" in p:
            continue
        lines = (root / p).read_text(encoding="utf-8", errors="replace").splitlines()
        for i, line in enumerate(lines):
            m = JS_EXPORT_RE.match(line)
            if not m:
                continue
            if m.group("var"):
                value = " ".join([m.group("rest")] + lines[i + 1 : i + 6])
                if not JS_FUNC_VALUE_RE.match(value.strip()):
                    continue
                name = m.group("var")
            else:
                name = m.group("fn") or m.group("cls") or "default"
            if not js_has_jsdoc(lines, i):
                items.append(Item("js", p, i + 1, name))
    return items


# ---- baseline and main ---------------------------------------------------------------------------


def read_baseline() -> dict[str, int]:
    """Baseline key -> line number in the baseline file."""
    if not BASELINE.exists():
        return {}
    keys = {}
    for n, line in enumerate(BASELINE.read_text().splitlines(), 1):
        line = line.strip()
        if line and not line.startswith("#"):
            keys[line] = n
    return keys


def write_baseline(items: list[Item]) -> None:
    """Rewrite the baseline with today's undocumented items, sorted."""
    BASELINE.parent.mkdir(parents=True, exist_ok=True)
    keys = sorted({it.key for it in items})
    head = [
        "# Undocumented items held by scripts/check.d/doc_coverage.py: may only shrink.",
        "# One per line: lang path [kind] symbol. Regenerate with --update-baseline.",
    ]
    BASELINE.write_text("\n".join(head + keys) + "\n")


def main(argv: list[str]) -> int:
    """Scan, compare with the baseline, print the results; return the exit status."""
    global BASELINE
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--update-baseline", action="store_true", help="rewrite the baseline")
    ap.add_argument("--list", action="store_true", help="also print baselined items")
    ap.add_argument("--lang", default="c,rust,py,js", help="comma-separated subset to scan")
    ap.add_argument(
        "--baseline", type=Path, help="baseline file (default: baselines/doc_coverage.txt)"
    )
    args = ap.parse_args(argv)
    if args.baseline:
        BASELINE = args.baseline.resolve()
    langs = set(args.lang.split(","))
    if not langs <= set(RULES):
        print(f"doc_coverage: unknown --lang {args.lang}", file=sys.stderr)
        return 2

    root = repo_root()
    files = repo_files(root)
    items: list[Item] = []
    info = []
    if "c" in langs:
        c_items, c_info = scan_c(root, files)
        items += c_items
        info.append(c_info)
    if "rust" in langs:
        items += scan_rust(root, files)
    if "py" in langs:
        items += scan_py(root, files)
    if "js" in langs:
        items += scan_js(root, files)

    if args.update_baseline:
        if langs != set(RULES):
            print("doc_coverage: --update-baseline needs all languages", file=sys.stderr)
            return 2
        write_baseline(items)
        print(f"doc_coverage: baseline rewritten with {len({i.key for i in items})} entries")
        return 0

    base = read_baseline()
    rel_base = BASELINE.relative_to(root) if BASELINE.is_relative_to(root) else BASELINE
    seen = set()
    failures = 0
    for it in sorted(items, key=lambda x: (x.path, x.line)):
        seen.add(it.key)
        if it.key in base:
            if args.list:
                print(f"{it.path}:{it.line}: {RULES[it.lang]} (baselined) {it.symbol}")
            continue
        failures += 1
        what = "has no comment" if it.lang == "c" else "has no doc comment"
        hint = {
            "c": "add a /** @brief ... */ block above it",
            "rust": "add a `///` summary line above it",
            "py": "add a Google-style docstring",
            "js": "add a /** ... */ JSDoc block above it",
        }[it.lang]
        print(f"{it.path}:{it.line}: {RULES[it.lang]} {it.symbol} {what} — {hint}")
    for key, n in sorted(base.items(), key=lambda kv: kv[1]):
        if key.split(" ", 1)[0] in langs and key not in seen:
            failures += 1
            rule = RULES.get(key.split(" ", 1)[0], "DOC")
            print(
                f"{rel_base}:{n}: {rule} baseline is stale: `{key}` no longer occurs — remove it "
                "(or run --update-baseline)"
            )
    counts = {lang: sum(1 for i in items if i.lang == lang) for lang in RULES if lang in langs}
    for line in info:
        print(f"doc_coverage: {line}")
    status = "FAIL" if failures else "ok"
    per = ", ".join(f"{k} {v}" for k, v in counts.items())
    print(f"doc_coverage: {status}: undocumented {per}; {failures} new or stale vs baseline")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
