#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["zcbor==0.9.1", "cbor2>=5.6,<5.7"]
# ///
"""Validate every protocol/vectors/*.json against protocol/agentlink.cddl (type `envelope`).

Usage: uv run protocol/tools/validate_vectors.py
"""

import json
import os
import sys
from pathlib import Path

import zcbor
from zcbor.zcbor.zcbor import DataTranslator

ROOT = Path(__file__).resolve().parent.parent
PRELUDE = Path(os.path.dirname(zcbor.__file__)) / "zcbor" / "prelude.cddl"


def main() -> int:
    cddl = (ROOT / "agentlink.cddl").read_text() + "\n" + PRELUDE.read_text()
    envelope = DataTranslator.from_cddl(cddl, 3).my_types["envelope"]
    failures = 0
    for f in sorted((ROOT / "vectors").glob("*.json")):
        data = bytes.fromhex(json.loads(f.read_text())["cbor_hex"])
        try:
            envelope.validate_str(data)
            print(f"ok    {f.name}")
        except Exception as e:  # zcbor raises plain Exceptions
            failures += 1
            print(f"FAIL  {f.name}: {str(e).splitlines()[0][:160]}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
