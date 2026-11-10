#!/usr/bin/env python3
"""Fail the firmware build when internal RAM (DIRAM) left after static allocation drops below a floor.

The BLE controller allocates from internal RAM at boot. On 2026-11-10 a commit that grew the session
tables left ~89 KB free and the watch boot-looped (BLE_INIT: Malloc failed); with 109 KB free it
booted. The floor sits between the two. Big static tables belong in PSRAM (MODEL_EXT_BSS).

    . $IDF_PATH/export.sh && idf.py -C firmware build && python3 firmware/tools/check_dram.py
"""
import json
import os
import subprocess
import sys

FLOOR = 100 * 1024
here = os.path.dirname(os.path.abspath(__file__))
mapfile = os.path.join(here, "..", "build", "agentlink.map")
if len(sys.argv) > 1:
    mapfile = sys.argv[1]
if not os.path.exists(mapfile):
    sys.exit(f"check_dram: no map file at {mapfile}; build the firmware first")
out = subprocess.run([sys.executable, "-m", "esp_idf_size", "--format", "json", mapfile],
                     capture_output=True, text=True, check=True).stdout
size = json.loads(out)
free = size["diram_remain"]
used = size["used_diram"]
print(f"check_dram: DIRAM used {used} B, free {free} B (floor {FLOOR} B)")
if free < FLOOR:
    sys.exit(f"check_dram: FAIL: only {free} B of internal RAM left; BLE needs room at boot. "
             "Move large static tables to PSRAM (MODEL_EXT_BSS) or shrink them.")
