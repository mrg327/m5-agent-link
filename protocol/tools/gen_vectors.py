#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["cbor2>=5.6,<5.7"]
# ///
"""Generate shared protocol test vectors into protocol/vectors/.

Each vector is <name>.json: {"desc", "symbolic", "cbor_hex"}. `symbolic` is the
human-readable form (names from agentlink.cddl), and `cbor_hex` is the canonical
integer-keyed encoding. The daemon (Rust) and firmware (C/zcbor) must both
decode cbor_hex to the same meaning and re-encode it byte-identically.

Usage: uv run protocol/tools/gen_vectors.py
"""

from __future__ import annotations

import json
from pathlib import Path

import cbor2

OUT = Path(__file__).resolve().parent.parent / "vectors"

# message type ids (agentlink.cddl mt_*)
MT = dict(hello=1, welcome=2, snapshot=3, session_upd=4, gone=5, attention=6, clear=7,
          cmd=8, result=9, detail=10, cfg=11, batt=12, ping=13, pong=14, log=15,
          dbg_ui_state=65, dbg_inject_input=68)


def env(t: str, id_: int, body: dict, re_: int | None = None) -> dict:
    m = {0: MT[t], 1: id_}
    if re_ is not None:
        m[2] = re_
    m[3] = body
    return m


VECTORS = {
    "hello_sim": (
        "Simulator hello with round 466x466 display",
        {"t": "hello", "id": 1, "b": {"proto": [0, 1],
         "dev": {"class": "sim", "board": "sim", "fw": "0.1.0", "uid": "0102030405060708"},
         "caps": {"disp": {"shape": "round", "w": 466, "h": 466, "color": True},
                  "input": ["touch", "btn", "imu"], "nbtn": 2, "haptic": True, "spk": True,
                  "mic": True, "batt": True, "max_msg": 4096,
                  "feats": ["focus", "ack", "approve", "badge", "debug"]}}},
        env("hello", 1, {0: [0, 1],
            1: {0: 3, 1: "sim", 2: "0.1.0", 3: bytes.fromhex("0102030405060708")},
            2: {0: {0: 0, 1: 466, 2: 466, 3: True}, 1: [0, 1, 3], 2: 2, 3: True, 4: True,
                5: True, 6: True, 7: 4096, 8: [0, 1, 2, 5, 6]}}),
    ),
    "welcome": (
        "Daemon welcome from a mac host, preview privacy, full alerts",
        {"t": "welcome", "id": 1, "re": 1, "b": {"proto": [0, 1],
         "host": {"id": "a1a2a3a4a5a6a7a8", "name": "dev-mac", "os": "mac"},
         "time": {"unix": 1793995200, "tz_off": -480},
         "cfg": {"privacy": "preview", "alert": "full", "brightness": 160, "badge_handle": "@octo"}}},
        env("welcome", 1, {0: [0, 1], 1: {0: bytes.fromhex("a1a2a3a4a5a6a7a8"), 1: "dev-mac", 2: 0},
            2: {0: 1793995200, 1: -480}, 3: {0: 1, 1: 0, 3: 160, 4: "@octo"}}, re_=1),
    ),
    "welcome_replies": (
        "Welcome (proto 0.4) with quick replies: this daemon types into terminals",
        {"t": "welcome", "id": 1, "re": 1, "b": {"proto": [0, 4],
         "host": {"id": "a1a2a3a4a5a6a7a8", "name": "dev-linux", "os": "linux"},
         "time": {"unix": 1793995200, "tz_off": -480},
         "cfg": {"privacy": "preview", "alert": "full"},
         "replies": ["Continue", "Yes", "Run the tests"]}},
        env("welcome", 1, {0: [0, 4], 1: {0: bytes.fromhex("a1a2a3a4a5a6a7a8"), 1: "dev-linux", 2: 2},
            2: {0: 1793995200, 1: -480}, 3: {0: 1, 1: 0}, 4: ["Continue", "Yes", "Run the tests"]}, re_=1),
    ),
    "snapshot_two": (
        "Snapshot with one working Copilot session and one Claude session needing approval",
        {"t": "snapshot", "id": 2, "b": {"sessions": [
            {"sid": 1, "ag": "copilot", "st": "work", "repo": "m5-agent-link", "br": "main",
             "tool": "bash", "t0": 1793994000, "tst": 1793995100, "sum": "Running tests", "seen": False, "host": 0},
            {"sid": 2, "ag": "claude", "st": "input", "sub": "approval", "repo": "webapp", "br": "feat/login",
             "t0": 1793993000, "tst": 1793995150, "seen": False, "host": 0}]}},
        env("snapshot", 2, {0: [
            {0: 1, 1: 0, 2: 1, 4: "m5-agent-link", 5: "main", 6: "bash", 7: 1793994000, 8: 1793995100,
             9: "Running tests", 10: False, 11: 0},
            {0: 2, 1: 1, 2: 2, 3: 0, 4: "webapp", 5: "feat/login", 7: 1793993000, 8: 1793995150,
             10: False, 11: 0}]}),
    ),
    "session_remote": (
        "New session from remote host 1 (REMOTE.md): host index plus host name",
        {"t": "session_upd", "id": 4, "b": {"sid": 7, "ag": "copilot", "st": "work", "repo": "svc",
         "t0": 1793995200, "tst": 1793995200, "seen": False, "host": 1, "hn": "WIN-BUILD01"}},
        env("session_upd", 4, {0: 7, 1: 0, 2: 1, 4: "svc", 7: 1793995200, 8: 1793995200, 10: False,
            11: 1, 12: "WIN-BUILD01"}),
    ),
    "session_monitor": (
        "Delta (proto 0.3): step 12 of the turn finished ok; summary, snooze and notify state",
        {"t": "session_upd", "id": 5, "b": {"sid": 3, "tool": "Bash", "sum": "Bash · npm test",
         "snz": 0, "ntf": True, "n": 12, "ok": True}},
        env("session_upd", 5, {0: 3, 6: "Bash", 9: "Bash · npm test", 13: 0, 14: True, 15: 12, 16: True}),
    ),
    "session_ask": (
        "Delta (proto 0.5): session 1 finished on a question; the watch offers its answers under Reply",
        {"t": "session_upd", "id": 6, "b": {"sid": 1, "st": "done", "tst": 1793995300,
         "q": "Should I flash it now?", "rp": ["Yes, go ahead", "No"]}},
        env("session_upd", 6, {0: 1, 2: 4, 8: 1793995300, 17: "Should I flash it now?", 18: ["Yes, go ahead", "No"]}),
    ),
    "session_delta_done": (
        "Delta: session 1 finished",
        {"t": "session_upd", "id": 3, "b": {"sid": 1, "st": "done", "tst": 1793995300}},
        env("session_upd", 3, {0: 1, 2: 4, 8: 1793995300}),
    ),
    "attention_approval": (
        "Approval request for session 2 (agent-raised prompt)",
        {"t": "attention", "id": 4, "b": {"rid": 7, "sid": 2, "kind": "approval",
         "title": "Run shell command", "text": "npm test -- --watch=false",
         "acts": ["approve", "deny", "focus"], "exp": 1793995450}},
        env("attention", 4, {0: 7, 1: 2, 2: 0, 3: "Run shell command", 4: "npm test -- --watch=false",
            6: [0, 1, 2], 7: 1793995450}),
    ),
    "attention_question_opts": (
        "Question for session 1 with three choices (AskUserQuestion); none of them grants anything",
        {"t": "attention", "id": 12, "b": {"rid": 8, "sid": 1, "kind": "question",
         "title": "Which framework?", "text": "Pick the UI library for the new dashboard",
         "opts": ["React", "Vue", "Keep vanilla"], "acts": ["reply", "focus"], "exp": 1793995500,
         "grant_mask": 0}},
        env("attention", 12, {0: 8, 1: 1, 2: 1, 3: "Which framework?", 4: "Pick the UI library for the new dashboard",
            5: ["React", "Vue", "Keep vanilla"], 6: [3, 2], 7: 1793995500, 8: 0}),
    ),
    "attention_permission_grants": (
        "Permission for session 2 with the agent's own choices; bits 0 and 1 grant (hold to commit)",
        {"t": "attention", "id": 13, "b": {"rid": 9, "sid": 2, "kind": "approval",
         "title": "Run shell command", "text": "npm test -- --watch=false",
         "opts": ["Yes", "Yes, don't ask again", "No"], "acts": ["approve", "deny", "focus", "reply"],
         "exp": 1793995450, "grant_mask": 3}},
        env("attention", 13, {0: 9, 1: 2, 2: 0, 3: "Run shell command", 4: "npm test -- --watch=false",
            5: ["Yes", "Yes, don't ask again", "No"], 6: [0, 1, 2, 3], 7: 1793995450, 8: 3}),
    ),
    "cmd_reply": (
        "Device answers rid 9 with option 1 (\"Yes, don't ask again\")",
        {"t": "cmd", "id": 7, "b": {"op": "reply", "sid": 2, "rid": 9, "arg": 1}},
        env("cmd", 7, {0: 5, 1: 2, 2: 9, 3: 1}),
    ),
    "cmd_focus": (
        "Device asks daemon to focus session 2",
        {"t": "cmd", "id": 5, "b": {"op": "focus", "sid": 2}},
        env("cmd", 5, {0: 0, 1: 2}),
    ),
    "cmd_approve": (
        "Device approves rid 7 after a 1250 ms hold",
        {"t": "cmd", "id": 6, "b": {"op": "approve", "sid": 2, "rid": 7, "hold_ms": 1250}},
        env("cmd", 6, {0: 3, 1: 2, 2: 7, 4: 1250}),
    ),
    "cmd_scroll": (
        "Device scrolls session 2's terminal three lines up (older); seq 1 of the burst",
        {"t": "cmd", "id": 8, "b": {"op": "scroll", "sid": 2, "arg": {"unit": "lines", "delta": -3, "seq": 1}}},
        env("cmd", 8, {0: 10, 1: 2, 3: {0: 0, 1: -3, 2: 1}}),
    ),
    "cmd_type": (
        "Device types quick reply 2 (welcome.replies) into session 3's terminal (proto 0.4)",
        {"t": "cmd", "id": 9, "b": {"op": "type", "sid": 3, "arg": 2}},
        env("cmd", 9, {0: 11, 1: 3, 3: 2}),
    ),
    "cmd_cancel": (
        "Device interrupts session 3's running turn after a hold (Esc in its terminal)",
        {"t": "cmd", "id": 10, "b": {"op": "cancel", "sid": 3, "hold_ms": 1250}},
        env("cmd", 10, {0: 6, 1: 3, 4: 1250}),
    ),
    "result_scroll_pos": (
        "Daemon answers a scroll: 142 lines above the live bottom of 1200, with the top visible line (privacy >= preview)",
        {"t": "result", "id": 7, "re": 8, "b": {"ok": True, "pos": {"lines_above": 142, "at_bottom": False, "total": 1200,
         "preview": "$ npm test -- --watch=false"}}},
        env("result", 7, {0: True, 3: {0: 142, 1: False, 2: 1200, 3: "$ npm test -- --watch=false"}}, re_=8),
    ),
    "result_stale": (
        "Daemon rejects approve because the prompt was already answered on PC",
        {"t": "result", "id": 5, "re": 6, "b": {"ok": False, "err": "stale"}},
        env("result", 5, {0: False, 1: 0}, re_=6),
    ),
    "clear_answered_pc": (
        "Attention 7 cleared because the user answered in the terminal",
        {"t": "clear", "id": 6, "b": {"rid": 7, "why": "answered_pc"}},
        env("clear", 6, {0: 7, 1: 0}),
    ),
    "ping": ("Keepalive", {"t": "ping", "id": 9, "b": {}}, env("ping", 9, {})),
    "dbg_ui_state": (
        "Device reports it is showing the attention card",
        {"t": "dbg_ui_state", "id": 10, "re": 8, "b": {"screen": "attention", "sel_sid": 2, "rid": 7,
         "visible": ["webapp · feat/login", "Run shell command", "Approve", "Deny", "Open on PC"],
         "power": "active", "fps": 30}},
        env("dbg_ui_state", 10, {0: "attention", 1: 2, 2: 7,
            3: ["webapp · feat/login", "Run shell command", "Approve", "Deny", "Open on PC"],
            4: "active", 5: 30}, re_=8),
    ),
    "dbg_inject_tap": (
        "Debug: inject a double-tap at the screen centre",
        {"t": "dbg_inject_input", "id": 11, "b": {"kind": "double_tap", "x": 233, "y": 233}},
        env("dbg_inject_input", 11, {0: 1, 1: 233, 2: 233}),
    ),
}


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for name, (desc, symbolic, wire) in VECTORS.items():
        data = cbor2.dumps(wire, canonical=True)
        doc = {"desc": desc, "symbolic": symbolic, "cbor_hex": data.hex()}
        (OUT / f"{name}.json").write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n")
        print(f"{name}: {len(data)} B")


if __name__ == "__main__":
    main()
