#!/usr/bin/env python3
"""A stand-in for the watch's internal-RAM limit, checked on every simulator test run.

The simulator has no RAM limit, so a change that grows static tables passes every test and then
boot-loops the watch (2026-11-10: BLE could not start). This sums the core library's static .data
and .bss, leaving out what is tagged for PSRAM (MODEL_EXT_BSS puts it in .ext_ram.bss here too),
and fails over a budget. Numbers are for the 64-bit host build, larger than on the 32-bit watch,
so the budget is relative: today's size plus a little headroom. If you need more, move big tables
to PSRAM first; raise the budget only on purpose, after checking firmware/tools/check_dram.py.

    ram_budget.py LIBCORE.a MAX_BYTES        exit 77 (skipped) without GNU size (e.g. macOS)
"""
import shutil
import subprocess
import sys

lib, budget = sys.argv[1], int(sys.argv[2])
size = shutil.which("size")
if not size:
    print("ram_budget: no `size` tool; skipped")
    sys.exit(77)
try:
    out = subprocess.run([size, "-A", lib], capture_output=True, text=True, check=True).stdout
except (subprocess.CalledProcessError, OSError) as e:
    print(f"ram_budget: `size -A` unavailable ({e}); skipped")
    sys.exit(77)
internal = ext = 0
per_obj, obj = {}, None
for line in out.splitlines():
    if line.endswith(":") or "(ex " in line:
        obj = line.split()[0].rstrip(":")
        continue
    parts = line.split()
    if len(parts) < 2 or not parts[1].isdigit():
        continue
    name, n = parts[0], int(parts[1])
    if name == ".ext_ram.bss":
        ext += n
    elif name in (".bss", ".data") or name.startswith((".bss.", ".data.")) and not name.startswith(".data.rel.ro"):
        internal += n
        per_obj[obj] = per_obj.get(obj, 0) + n
print(f"ram_budget: core static RAM {internal} B internal (budget {budget} B), {ext} B in PSRAM")
if internal > budget:
    top = sorted(per_obj.items(), key=lambda kv: -kv[1])[:5]
    print("ram_budget: FAIL: over budget. Largest objects: " + ", ".join(f"{o} {n} B" for o, n in top))
    print("ram_budget: move large static tables to PSRAM (MODEL_EXT_BSS) or shrink them; see firmware/tools/check_dram.py")
    sys.exit(1)
