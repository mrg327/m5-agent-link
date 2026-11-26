#!/usr/bin/env python3
"""The structure check: dependency, layout and map rules from docs/conventions/structure.md.

Usage: python3 scripts/check.d/structure.py [--update-baseline]   (from anywhere in the repository)

It verifies, each finding tagged with its rule ID:
  1. Includes (S-DEP-01..09, S-CORE-01..04): every #include in firmware/, simulator/ and atlas/wasm/
     is resolved through the build's include directories to the unit that owns the header, and the
     edge is tested against RULES below. ESP-IDF and FreeRTOS headers in core only under
     #ifdef ESP_PLATFORM (S-DEP-02). No ".." in an include path (S-DEP-09).
  2. HAL completeness (S-DEP-10): every non-inline function declared in hal.h is defined at file
     scope by each HAL: every firmware/boards/<name>/, simulator/src/sim_*.c, atlas/wasm/web_hal.c.
  3. Source lists (S-NEW-01): every core/src/*.c is in CORE_SRCS (core/CMakeLists.txt, which the
     simulator and atlas/wasm builds include through add_subdirectory).
  4. Crates (S-CRATE-01..04): the workspace edges in daemon/crates/*/Cargo.toml.
  5. Generated and vendored files (S-GEN-02): each is marked in .gitattributes.
  6. Maps and READMEs (S-TOP-01, S-DOC-01, S-OWN-01): each top-level directory is in the README.md
     map, the structure.md table and .github/CODEOWNERS; each build unit has a README.md or a row
     naming it in the nearest README above it.

Prints `path:line: RULE-ID message - how to fix` per finding and a one-line summary. Existing
findings are listed in scripts/check.d/baselines/structure.txt (one stable key per line, no line
numbers). Exit 1 on a finding not in the baseline, or a baseline entry that no longer occurs (the
baseline only shrinks). --update-baseline rewrites the baseline from the current findings.
Standard library only (Python 3.11).
"""

from __future__ import annotations

import fnmatch
import os
import re
import subprocess
import sys
import tomllib
from dataclasses import dataclass

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BASELINE = os.path.join(ROOT, "scripts", "check.d", "baselines", "structure.txt")

FW = "firmware/components"
CORE = f"{FW}/core"

# ----------------------------------------------------------------------------------------------
# The rule table. Change it together with docs/conventions/structure.md.
# ----------------------------------------------------------------------------------------------

# Include directories, in search order after the includer's own directory (the union of the three
# builds' -I paths; a header's owner is decided by the directory it is found in).
INCLUDE_DIRS = [
    f"{CORE}/include",
    f"{CORE}/src",
    f"{CORE}/third_party/detools",
    f"{CORE}/third_party/detools/heatshrink",
    f"{FW}/agentlink_hal/include",
    f"{FW}/agentlink_proto/include",
    "firmware/boards/stopwatch",
    "firmware/boards/stopwatch/sw_power/include",
    "simulator/src",
    "atlas/wasm",
]

# Units: (path prefix or fnmatch glob, unit). First match wins, so specific entries come first.
UNITS = [
    (f"{CORE}/include/", "core.pub"),
    (f"{CORE}/src/fonts/", "core.fonts"),
    (f"{CORE}/src/", "core.src"),
    (f"{CORE}/third_party/", "core.third_party"),
    (f"{FW}/agentlink_hal/", "hal"),
    (f"{FW}/agentlink_proto/test/", "proto.test"),
    (f"{FW}/agentlink_proto/", "proto"),
    ("firmware/boards/", "board"),
    ("firmware/main/", "main"),
    ("simulator/src/main.c", "sim.harness"),
    ("simulator/src/sim_script.c", "sim.harness"),
    ("simulator/src/sim_fw_image.c", "sim.harness"),
    ("simulator/src/", "sim.hal"),
    ("simulator/tests/", "sim.tests"),
    ("simulator/lv_conf.h", "lv_conf"),
    ("atlas/wasm/lv_conf.h", "lv_conf"),
    ("atlas/wasm/web_main.c", "web.main"),
    ("atlas/wasm/", "web.hal"),
]

# External header families, by the include path (first match wins).
EXTERNAL = [
    ("lvgl", re.compile(r"^(lvgl\.h|lvgl/.*|lv_[\w/]*\.h)$")),
    (
        "esp",
        re.compile(
            r"^(freertos/.*|esp_.*|driver/.*|nvs.*|sdkconfig\.h|host/.*|nimble/.*|services/.*|soc/.*"
            r"|hal/.*|rom/.*|esp32\w*/.*|lwip/.*)$"
        ),
    ),
    ("vendor", re.compile(r"^(M5\w*\.h|lgfx/.*|M5\w*/.*)$")),
    ("sdl", re.compile(r"^(SDL\.h|SDL2/.*)$")),
    ("emscripten", re.compile(r"^emscripten(\.h|/.*)$")),
    (
        "clib",
        re.compile(
            r"^(assert|complex|ctype|errno|fenv|float|inttypes|iso646|limits|locale|math|setjmp|signal"
            r"|stdalign|stdarg|stdatomic|stdbool|stddef|stdint|stdio|stdlib|stdnoreturn|string|tgmath"
            r"|threads|time|uchar|wchar|wctype)\.h$"
        ),
    ),
    (
        "posix",
        re.compile(r"^(unistd|pthread|poll|fcntl|dirent|sched|semaphore)\.h$|^(sys|netinet|arpa)/"),
    ),
]

# Allowed targets per includer unit: other units, external families, or "*" for anything.
ALLOW = {
    "core.pub": ("S-DEP-01", {"clib"}),
    "core.src": (
        "S-DEP-01",
        {"core.pub", "core.src", "core.third_party", "hal", "proto", "lvgl", "clib", "esp"},
    ),  # esp: only guarded, S-DEP-02
    "core.fonts": ("S-DEP-01", {"lvgl", "clib"}),
    "core.third_party": ("S-DEP-01", {"*"}),  # vendored: only S-DEP-09 applies
    "hal": ("S-DEP-03", {"hal", "clib", "lvgl"}),
    "proto": ("S-DEP-04", {"proto", "clib"}),
    "proto.test": ("S-DEP-04", {"proto", "clib", "posix"}),
    "board": ("S-DEP-05", {"board", "hal", "lvgl", "esp", "vendor", "clib"}),
    "main": ("S-DEP-06", {"core.pub", "hal", "lvgl", "esp", "clib"}),
    "web.main": ("S-DEP-06", {"core.pub", "hal", "web.hal", "lvgl", "emscripten", "clib"}),
    "sim.hal": ("S-DEP-07", {"sim.hal", "hal", "lvgl", "sdl", "posix", "clib"}),
    "web.hal": ("S-DEP-07", {"web.hal", "hal", "lvgl", "emscripten", "clib"}),
    "sim.harness": (
        "S-DEP-08",
        {"core.pub", "core.src", "hal", "proto", "lvgl", "sim.hal", "sdl", "posix", "clib"},
    ),
    "sim.tests": (
        "S-DEP-08",
        {"core.pub", "core.src", "hal", "proto", "lvgl", "sim.hal", "posix", "clib"},
    ),
    "lv_conf": ("S-DEP-07", {"clib"}),
}

FIX = {
    "S-DEP-01": "core reaches only hal.h, agentlink_proto, lvgl.h and the C library",
    "S-DEP-03": "hal.h is header-only: C library and lvgl.h only",
    "S-DEP-04": "agentlink_proto is the bottom layer: its own headers and the C library",
    "S-DEP-05": "a board is an adapter: hal.h, its own headers, ESP-IDF, vendor components",
    "S-DEP-06": "use only the public core.h, never core/src",
    "S-DEP-07": "a host HAL implements hal.h with its platform; it never includes core",
    "S-DEP-08": "harness and tests may reach core/src, never boards, main or atlas",
}

# Tiers inside core/src, by module stem (structure.md "Inside core"). debug.h is a leaf, debug.c glue.
TIERS = {
    "glue": {
        "app",
        "link",
        "link_rx",
        "link_tx",
        "link_internal",
        "ota",
        "power",
        "power_report",
        "bench",
        "debug.c",
    },
    "service": {"prefs", "sound", "feedback", "voice", "keys"},
    "pure": {
        "model",
        "nav",
        "gesture",
        "steps",
        "graph",
        "ring",
        "gauges",
        "detail",
        "done",
        "motion",
        "sha256",
        "sound_bank",
        "toy_gate",
        "nag",
        "persona",
        "temper",
        "replay",
    },
    "leaf": {"debug.h", "ui_icons"},
}
# What each tier may include inside core/src and outside it; rule ID for a breach.
TIER_ALLOW = {
    "leaf": ("S-CORE-01", set(), set()),
    "pure": ("S-CORE-01", {"pure", "leaf"}, set()),
    "service": ("S-CORE-02", {"pure", "leaf", "service", "link.h"}, {"hal"}),
    "ui": ("S-CORE-03", {"pure", "leaf", "service", "ui"}, {"hal", "lvgl", "proto"}),
    "glue": (None, None, None),  # may include anything in core
}

# Rust crates (S-CRATE-*): library crates, binary crates, and the allowed workspace edges.
CRATE_LIBS = {"agentlink-proto", "agentlink-keys", "agentlink-delta"}
CRATE_BINS = {"agentlinkd", "agentlink", "agentlink-hook"}
CRATE_EDGES = {
    "agentlinkd": {"agentlink-proto", "agentlink-keys", "agentlink-delta"},
    "agentlink": {"agentlink-proto"},
    "agentlink-hook": set(),
    "agentlink-proto": set(),
    "agentlink-keys": set(),
    "agentlink-delta": set(),
}

# Generated and vendored files (structure.md "Generated, vendored and derived files").
GENERATED = [
    f"{FW}/agentlink_proto/src/agentlink_codec_*.c",
    f"{FW}/agentlink_proto/include/agentlink_codec_*.h",
    "protocol/vectors/*.json",
    "protocol/fixtures/ota/*.bin",
    "protocol/fixtures/ota/*.patch",
    "atlas/data/facts.json",
    "atlas/css/tokens.generated.css",
    "atlas/vendor/watch.*",
    f"{CORE}/src/fonts/font_*.c",
    "daemon/crates/agentlink-keys/data/english.txt",
    "daemon/Cargo.lock",
    "firmware/tools/fonts/package-lock.json",
]
VENDORED = [
    f"{FW}/agentlink_proto/src/zcbor_*",
    f"{FW}/agentlink_proto/include/zcbor_*",
    f"{CORE}/third_party/*",
    "docs/StopWatch.md",
    "docs/arduino/*",
    "docs/esp_idf/*",
    "docs/github/*",
    "docs/guide/*",
    "docs/tools/*",
    "docs/uiflow2/*",
]

C_EXT = (".c", ".h", ".cpp", ".hpp", ".cc")
C_ROOTS = ("firmware/", "simulator/", "atlas/wasm/")

# ----------------------------------------------------------------------------------------------


@dataclass
class Finding:
    path: str
    line: int
    rule: str
    key: str  # the stable part after the rule ID, for the baseline
    msg: str

    def baseline_key(self) -> str:
        return f"{self.rule} {self.key}"


def git(*args: str) -> str:
    return subprocess.run(
        ["git", "-C", ROOT, *args], check=True, capture_output=True, text=True
    ).stdout


def read(rel: str) -> str:
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def files() -> list[str]:
    """Tracked files plus untracked ones not ignored, so a new file is checked before `git add`."""
    out = git("ls-files", "-z", "--cached", "--others", "--exclude-standard")
    return sorted(p for p in set(out.split("\0")) if p and os.path.isfile(os.path.join(ROOT, p)))


def strip_comments(src: str) -> str:
    """Blank out C comments, keeping newlines and string literals."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in src[i:j]))
            i = j
        elif c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            out.append(src[i : j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def unit_of(rel: str) -> str | None:
    for pat, unit in UNITS:
        if rel.startswith(pat) if pat.endswith("/") else rel == pat:
            return unit
    return None


def tier_of(rel: str) -> str:
    base = os.path.basename(rel)
    stem = os.path.splitext(base)[0]
    for tier, names in TIERS.items():
        if base in names or stem in names:
            return tier
    if stem == "ui" or stem.startswith("ui_"):
        return "ui"
    return "unknown"


# ---- 1. includes -----------------------------------------------------------------------------

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
COND_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)")
ESP_COND_RE = re.compile(r"^\s*(defined\s*\(?\s*ESP_PLATFORM\s*\)?|ESP_PLATFORM)\s*$")


def includes(rel: str):
    """Yield (line, kind, path, esp_guarded) for each #include in a C/C++ file."""
    # Per open #if, the state of the current branch: "esp" (ESP_PLATFORM is defined here),
    # "not-esp" (the #ifndef ESP_PLATFORM branch, whose #else is "esp") or "" (anything else).
    stack: list[str] = []
    for no, line in enumerate(strip_comments(read(rel)).splitlines(), 1):
        m = COND_RE.match(line)
        if m:
            d, arg = m.group(1), m.group(2).strip()
            if d in ("if", "ifdef"):
                stack.append("esp" if ESP_COND_RE.match(arg) else "")
            elif d == "ifndef":
                stack.append("not-esp" if arg == "ESP_PLATFORM" else "")
            elif d == "elif" and stack:
                stack[-1] = ""
            elif d == "else" and stack:
                stack[-1] = "esp" if stack[-1] == "not-esp" else ""
            elif d == "endif" and stack:
                stack.pop()
            continue
        m = INCLUDE_RE.match(line)
        if m:
            yield no, m.group(1), m.group(2).strip(), "esp" in stack


def resolve(includer: str, kind: str, path: str, exists: set[str]) -> str | None:
    """The first-party file an include names, or None for an external header."""
    cands = [os.path.normpath(os.path.join(os.path.dirname(includer), path))] if kind == '"' else []
    cands += [f"{d}/{path}" for d in INCLUDE_DIRS]
    for c in cands:
        if c in exists:
            return c
    return None


def family(path: str) -> str:
    for name, rx in EXTERNAL:
        if rx.match(path):
            return name
    return "unknown"


def check_includes(all_files: list[str], out: list[Finding]) -> None:
    exists = set(all_files)
    for rel in all_files:
        if not rel.endswith(C_EXT) or not rel.startswith(C_ROOTS):
            continue
        src_unit = unit_of(rel)
        if src_unit is None:
            continue
        rule, allowed = ALLOW[src_unit]
        for no, kind, path, guarded in includes(rel):
            inc = f"{kind}{path}{'>' if kind == '<' else chr(34)}"
            if ".." in path.split("/"):
                out.append(
                    Finding(
                        rel,
                        no,
                        "S-DEP-09",
                        f"{rel} {path}",
                        f"include {inc} uses '..' - add an include directory instead",
                    )
                )
                continue
            if src_unit == "core.third_party":
                continue
            target = resolve(rel, kind, path, exists)
            dst = unit_of(target) if target else family(path)
            if dst is None:
                dst = "unknown"
            # S-DEP-02: ESP-IDF/FreeRTOS in core only under #ifdef ESP_PLATFORM.
            if src_unit == "core.src" and dst == "esp":
                if not guarded:
                    out.append(
                        Finding(
                            rel,
                            no,
                            "S-DEP-02",
                            f"{rel} {path}",
                            f"include {inc} outside #ifdef ESP_PLATFORM - guard it and "
                            "give the simulator a fallback",
                        )
                    )
                continue
            if "*" not in allowed and dst not in allowed:
                what = target or f"{dst} header"
                out.append(
                    Finding(
                        rel,
                        no,
                        rule,
                        f"{rel} {path}",
                        f"{src_unit} includes {inc} ({what}) - {FIX.get(rule, 'see structure.md')}",
                    )
                )
                continue
            if src_unit == "core.src":
                check_tier(rel, no, inc, path, target, dst, out)


def check_tier(rel, no, inc, path, target, dst, out) -> None:
    tier = tier_of(rel)
    if tier == "unknown":
        out.append(
            Finding(
                rel,
                no,
                "S-CORE-01",
                f"{rel} tier",
                f"module has no tier - add it to TIERS in scripts/check.d/structure.py "
                "and to structure.md 'Inside core'",
            )
        )
        return
    rule, inside, outside = TIER_ALLOW[tier]
    if rule is None:
        return
    if dst == "core.src":
        ttier = tier_of(target)
        ok = ttier in inside or os.path.basename(target) in inside
        if ttier == "glue" and tier == "ui":
            ok = False
        if not ok:
            out.append(
                Finding(
                    rel,
                    no,
                    rule,
                    f"{rel} {path}",
                    f"{tier} module includes {inc} ({ttier}) - "
                    + (
                        "report the action up through ui_actions_t"
                        if tier == "ui"
                        else f"a {tier} module may not depend on the {ttier} tier"
                    ),
                )
            )
    elif dst in ("hal", "lvgl", "proto") and dst not in outside:
        out.append(
            Finding(
                rel,
                no,
                rule,
                f"{rel} {path}",
                f"{tier} module includes {inc} - {tier} modules stay free of {dst}"
                + (" (S-CORE-04: LVGL only in UI and glue)" if dst == "lvgl" else ""),
            )
        )


# ---- 2. HAL completeness ---------------------------------------------------------------------


def hal_decls() -> list[tuple[str, int]]:
    rel = f"{FW}/agentlink_hal/include/hal.h"
    text = strip_comments(read(rel))
    text = re.sub(r"^\s*#.*$", "", text, flags=re.M)
    decls, pos = [], 0
    for stmt in re.split(r"([;{}])", text):
        line = text.count("\n", 0, pos) + 1
        pos += len(stmt)
        s = " ".join(stmt.split())
        if not s or s in ";{}" or s.startswith(("typedef", "static", 'extern "C"')):
            continue
        m = re.search(r"\b(hal_\w+)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)$", s)
        if m and not re.search(r"\(\s*\*", s[: m.start()]):
            decls.append((m.group(1), line))
    return decls


def file_scope_defs(rel: str) -> set[str]:
    """Names of functions defined at file scope (extern "C" and namespace blocks are see-through)."""
    text = strip_comments(read(rel))
    text = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', text)
    text = re.sub(r"^\s*#.*$", "", text, flags=re.M)
    text = re.sub(r'extern\s*""\s*\{|namespace\s*\w*\s*\{', "\x01", text)
    flat, stack = [], []
    for ch in text:
        if ch == "\x01":
            stack.append(False)
        elif ch == "{":
            if not any(stack):
                flat.append("{")
            stack.append(True)
        elif ch == "}":
            if stack:
                stack.pop()
            if not any(stack):
                flat.append(";")
        elif not any(stack):
            flat.append(ch)
    return set(re.findall(r"\b(hal_\w+)\s*\([^;{}]*\)\s*(?:const\s*)?\{", "".join(flat)))


def check_hal(all_files: list[str], out: list[Finding]) -> None:
    hals: dict[str, list[str]] = {}
    for rel in all_files:
        m = re.match(r"firmware/boards/([^/]+)/.*\.(c|cpp|cc)$", rel)
        if m:
            hals.setdefault(f"firmware/boards/{m.group(1)}", []).append(rel)
        elif re.match(r"simulator/src/sim_\w+\.c$", rel) and unit_of(rel) == "sim.hal":
            hals.setdefault("simulator/src", []).append(rel)
        elif rel == "atlas/wasm/web_hal.c":
            hals.setdefault("atlas/wasm", []).append(rel)
    decls = hal_decls()
    for hal, srcs in sorted(hals.items()):
        defined: set[str] = set()
        for s in srcs:
            defined |= file_scope_defs(s)
        for name, line in decls:
            if name not in defined:
                out.append(
                    Finding(
                        f"{FW}/agentlink_hal/include/hal.h",
                        line,
                        "S-DEP-10",
                        f"{hal} {name}",
                        f"{name}() has no definition in {hal}/ - implement it (a no-op "
                        "plus a caps flag if the board lacks the feature)",
                    )
                )


# ---- 3. CORE_SRCS ----------------------------------------------------------------------------


def check_core_srcs(all_files: list[str], out: list[Finding]) -> None:
    cml = f"{CORE}/CMakeLists.txt"
    text = read(cml)
    m = re.search(r"set\(\s*CORE_SRCS(.*?)\)", text, re.S)
    listed = set(m.group(1).split()) if m else set()
    for rel in all_files:
        if re.match(rf"{CORE}/src/.*\.c$", rel):
            short = rel[len(CORE) + 1 :]
            if short not in listed:
                out.append(
                    Finding(
                        cml,
                        1,
                        "S-NEW-01",
                        short,
                        f"{short} is not in CORE_SRCS - add it, or the device, simulator "
                        "and atlas builds silently leave it out",
                    )
                )


# ---- 4. crates -------------------------------------------------------------------------------


def check_crates(all_files: list[str], out: list[Finding]) -> None:
    ws = tomllib.loads(read("daemon/Cargo.toml"))
    members = ws.get("workspace", {}).get("members", [])
    crates: dict[str, tuple[str, dict]] = {}
    for mem in members:
        rel = f"daemon/{mem}/Cargo.toml"
        if rel in all_files:
            data = tomllib.loads(read(rel))
            crates[data["package"]["name"]] = (rel, data)
    for name, (rel, data) in sorted(crates.items()):
        if name not in CRATE_EDGES:
            out.append(
                Finding(
                    rel,
                    1,
                    "S-CRATE-01",
                    f"{name} unclassified",
                    f"crate {name} is not in CRATE_EDGES - add it to the table in "
                    "scripts/check.d/structure.py and structure.md",
                )
            )
            continue
        tables = [data.get(k, {}) for k in ("dependencies", "build-dependencies")]
        for tgt in data.get("target", {}).values():
            tables += [tgt.get(k, {}) for k in ("dependencies", "build-dependencies")]
        lines = read(rel).splitlines()
        for table in tables:
            for dep, spec in table.items():
                real = spec.get("package", dep) if isinstance(spec, dict) else dep
                if real not in crates:
                    continue
                dep_re = re.compile(rf"^\s*(\[[\w.'\"()-]*\.)?{re.escape(dep)}\b")
                line = next((i for i, l in enumerate(lines, 1) if dep_re.match(l)), 1)
                if isinstance(spec, dict) and "path" in spec:
                    out.append(
                        Finding(
                            rel,
                            line,
                            "S-CRATE-04",
                            f"{name} {real}",
                            f"{dep} uses path = - declare it in [workspace.dependencies] "
                            f"and write `{dep}.workspace = true`",
                        )
                    )
                if real in CRATE_EDGES[name]:
                    continue
                if real in CRATE_BINS:
                    rule, why = "S-CRATE-02", "a binary crate is never a dependency; talk over IPC"
                elif name == "agentlink-hook":
                    rule, why = "S-CRATE-03", "agentlink-hook stays tiny and speaks JSON"
                elif name in CRATE_LIBS:
                    rule, why = "S-CRATE-01", "library crates depend on no workspace crate"
                else:
                    rule, why = "S-CRATE-02", "add the edge to CRATE_EDGES and structure.md"
                out.append(Finding(rel, line, rule, f"{name} {real}", f"{name} -> {real}: {why}"))


# ---- 5. generated files ----------------------------------------------------------------------


def check_attributes(all_files: list[str], out: list[Finding]) -> None:
    want: dict[str, str] = {}
    for pats, attr in ((GENERATED, "linguist-generated"), (VENDORED, "linguist-vendored")):
        for rel in all_files:
            if any(fnmatch.fnmatchcase(rel, p) for p in pats):
                want[rel] = attr
    if not want:
        return
    for attr in ("linguist-generated", "linguist-vendored"):
        paths = [p for p, a in want.items() if a == attr]
        res = subprocess.run(
            ["git", "-C", ROOT, "check-attr", "--stdin", "-z", attr],
            input="\0".join(paths),
            capture_output=True,
            text=True,
            check=True,
        )
        parts = res.stdout.split("\0")
        for i in range(0, len(parts) - 2, 3):
            path, _, val = parts[i : i + 3]
            if val not in ("set", "true"):
                out.append(
                    Finding(
                        ".gitattributes",
                        1,
                        "S-GEN-02",
                        f"{path} {attr}",
                        f"{path} is not marked {attr} - add a pattern to .gitattributes",
                    )
                )


# ---- 6. maps and READMEs ---------------------------------------------------------------------

TOKEN_RE = re.compile(r"`([^`]+)`")


def first_tokens(md: str, section: str | None = None) -> list[str]:
    """Path-like tokens that name a row: backticked tokens in a table row's first cell, or the
    backticked token a list item starts with."""
    toks, active = [], section is None
    for line in md.splitlines():
        if section is not None and line.startswith("#"):
            active = line.lstrip("#").strip().lower() == section.lower()
        if not active:
            continue
        s = line.strip()
        if s.startswith("|"):
            cells = s.strip("|").split("|")
            toks += TOKEN_RE.findall(cells[0])
        elif re.match(r"[-*] `", s):
            toks += TOKEN_RE.findall(s)[:1]
    return [t.strip().rstrip("/") for t in toks]


def top_dirs(all_files: list[str]) -> list[str]:
    return sorted({p.split("/")[0] for p in all_files if "/" in p and not p.startswith(".")})


def check_maps(all_files: list[str], out: list[Finding]) -> None:
    tops = top_dirs(all_files)
    readme_map = set(first_tokens(read("README.md"), "Repository map"))
    struct = set(first_tokens(read("docs/conventions/structure.md"), "Top level"))
    owners_rel = ".github/CODEOWNERS"
    owners = set()
    if os.path.isfile(os.path.join(ROOT, owners_rel)):
        for line in read(owners_rel).splitlines():
            if line.strip() and not line.lstrip().startswith("#"):
                owners.add(line.split()[0].strip("/").split("/")[0])
    for d in tops:
        if d not in readme_map:
            out.append(
                Finding(
                    "README.md",
                    1,
                    "S-TOP-01",
                    f"{d} README.md",
                    f"top-level {d}/ is not in the README.md repository map - add a row",
                )
            )
        if d not in struct:
            out.append(
                Finding(
                    "docs/conventions/structure.md",
                    1,
                    "S-TOP-01",
                    f"{d} structure.md",
                    f"top-level {d}/ is not in structure.md's Top level table - add a row",
                )
            )
        if d not in owners:
            out.append(
                Finding(
                    owners_rel,
                    1,
                    "S-OWN-01",
                    d,
                    f"top-level {d}/ has no entry in {owners_rel} - add one",
                )
            )

    # Build units (S-DOC-01).
    dirs = {os.path.dirname(p) for p in all_files}
    units = set()
    for p in all_files:
        parts = p.split("/")
        if len(parts) >= 4 and parts[:2] == ["firmware", "components"]:
            units.add("/".join(parts[:3]))
        if len(parts) >= 4 and parts[:2] == ["firmware", "boards"]:
            units.add("/".join(parts[:3]))
            if len(parts) >= 5 and f"{'/'.join(parts[:4])}/CMakeLists.txt" in all_files:
                units.add("/".join(parts[:4]))
        if len(parts) >= 3 and parts[:2] == ["firmware", "main"]:
            units.add("firmware/main")
        if len(parts) >= 4 and parts[:2] == ["daemon", "crates"]:
            units.add("/".join(parts[:3]))
        if len(parts) >= 3 and parts[0] == "daemon" and parts[1] in ("integrations", "tools"):
            units.add("daemon/" + parts[1])
        if len(parts) >= 3 and parts[0] == "tools":
            units.add("tools/" + parts[1])
        if len(parts) >= 3 and parts[:2] == ["atlas", "wasm"]:
            units.add("atlas/wasm")
    units |= set(tops)
    readmes = {os.path.dirname(p) for p in all_files if os.path.basename(p) == "README.md"}
    for u in sorted(units):
        if u in readmes or u not in dirs and not any(d.startswith(u + "/") for d in dirs):
            continue
        parent = os.path.dirname(u)
        while parent and parent not in readmes:
            parent = os.path.dirname(parent)
        readme = f"{parent}/README.md" if parent else "README.md"
        rel_name = u[len(parent) + 1 :] if parent else u
        toks = set(first_tokens(read(readme)))
        # A row may name the unit or, for a plain container like tools/linux/, a directory in it.
        if not any(t in (rel_name, u) or t.startswith((rel_name + "/", u + "/")) for t in toks):
            out.append(
                Finding(
                    u,
                    1,
                    "S-DOC-01",
                    u,
                    f"{u}/ has no README.md and no row naming `{rel_name}` in {readme} - "
                    "add one or the other",
                )
            )


# ---- driver ----------------------------------------------------------------------------------


def load_baseline() -> dict[str, int]:
    """Baseline key -> its line number."""
    if not os.path.isfile(BASELINE):
        return {}
    with open(BASELINE, encoding="utf-8") as f:
        return {l.strip(): no for no, l in enumerate(f, 1) if l.strip() and not l.startswith("#")}


def main(argv: list[str]) -> int:
    all_files = files()
    out: list[Finding] = []
    check_includes(all_files, out)
    check_hal(all_files, out)
    check_core_srcs(all_files, out)
    check_crates(all_files, out)
    check_attributes(all_files, out)
    check_maps(all_files, out)

    keys = sorted({f.baseline_key() for f in out})
    if "--update-baseline" in argv:
        os.makedirs(os.path.dirname(BASELINE), exist_ok=True)
        with open(BASELINE, "w", encoding="utf-8") as f:
            f.write(
                "# Known findings of scripts/check.d/structure.py: `RULE-ID key`, one per line.\n"
                "# This list only shrinks. Fix one, then delete its line (or --update-baseline).\n"
            )
            f.writelines(k + "\n" for k in keys)
        print(f"structure: baseline rewritten with {len(keys)} entries")
        return 0

    base = load_baseline()
    new = [f for f in out if f.baseline_key() not in base]
    stale = sorted(set(base) - set(keys))
    for f in sorted(new, key=lambda f: (f.path, f.line, f.rule)):
        print(f"{f.path}:{f.line}: {f.rule} {f.msg}")
    rel_base = os.path.relpath(BASELINE, ROOT)
    for k in stale:
        print(
            f"{rel_base}:{base[k]}: {k.split()[0]} baseline is stale: remove "
            f"'{k}' (it no longer occurs)"
        )
    ok = not new and not stale
    print(
        f"structure: {'ok' if ok else 'FAIL'}: {len(new)} new, {len(stale)} stale, "
        f"{len(out) - len(new)} baselined"
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
