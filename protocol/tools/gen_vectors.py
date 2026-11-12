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
          audio_start=16, audio=17, audio_end=18, transcript=19,
          kb_start=20, kb_stroke=21, kb_cands=22, usage=23,
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
    "welcome_voice": (
        "Welcome (proto 0.6): quick replies and voice dictation (whisper.cpp installed on the PC)",
        {"t": "welcome", "id": 1, "re": 1, "b": {"proto": [0, 6],
         "host": {"id": "a1a2a3a4a5a6a7a8", "name": "dev-linux", "os": "linux"},
         "time": {"unix": 1793995200, "tz_off": -480},
         "cfg": {"privacy": "preview", "alert": "full"},
         "replies": ["Continue"], "voice": True}},
        env("welcome", 1, {0: [0, 6], 1: {0: bytes.fromhex("a1a2a3a4a5a6a7a8"), 1: "dev-linux", 2: 2},
            2: {0: 1793995200, 1: -480}, 3: {0: 1, 1: 0}, 4: ["Continue"], 5: True}, re_=1),
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
    "session_job_eta": (
        "Delta: session 4 (a shell job, agent shell) enters work with a 120 s estimate (proto 0.9)",
        {"t": "session_upd", "id": 15, "b": {"sid": 4, "ag": "shell", "st": "work", "tool": "cargo",
         "t0": 1793995200, "tst": 1793995200, "sum": "cargo build --release", "eta": 120}},
        env("session_upd", 15, {0: 4, 1: 3, 2: 1, 6: "cargo", 7: 1793995200, 8: 1793995200,
            9: "cargo build --release", 19: 120}),
    ),
    "session_job_exit": (
        "Delta: session 4's job failed with exit status 101 (proto 0.9)",
        {"t": "session_upd", "id": 16, "b": {"sid": 4, "st": "error", "tst": 1793995242, "seen": False, "exit": 101}},
        env("session_upd", 16, {0: 4, 2: 3, 8: 1793995242, 10: False, 20: 101}),
    ),
    "cmd_rerun": (
        "Device runs session 4's finished job again after a hold (proto 0.9)",
        {"t": "cmd", "id": 17, "b": {"op": "rerun", "sid": 4, "hold_ms": 1250}},
        env("cmd", 17, {0: 17, 1: 4, 4: 1250}),
    ),
    "session_gauges": (
        "Delta: session 2 (Claude) reports its context fill, model, permission mode and pull request (proto 0.10)",
        {"t": "session_upd", "id": 18, "b": {"sid": 2, "ctx": 72, "mdl": "Opus 4.7", "pm": "plan", "pr": 128,
         "prs": "approved"}},
        env("session_upd", 18, {0: 2, 21: 72, 22: "Opus 4.7", 23: 1, 24: 128, 25: 2}),
    ),
    "session_gauges_cleared": (
        "Delta: session 2 after /compact: context unknown (255), the pull request merged (0) (proto 0.10)",
        {"t": "session_upd", "id": 19, "b": {"sid": 2, "ctx": 255, "pr": 0, "prs": "none"}},
        env("session_upd", 19, {0: 2, 21: 255, 24: 0, 25: 0}),
    ),
    "usage_both": (
        "Plan usage: 5-hour window at 82 % and on pace to run out before it resets, weekly at 41 % (proto 0.10)",
        {"t": "usage", "id": 20, "b": {"at": 1793995200, "h5": 82, "h5r": 1794004200, "h5o": 1793998800,
         "wk": 41, "wkr": 1794340800}},
        env("usage", 20, {0: 1793995200, 1: 82, 2: 1794004200, 3: 1793998800, 4: 41, 5: 1794340800}),
    ),
    "usage_none": (
        "Plan usage unknown (no subscription data yet): only the reading time, no window (proto 0.10)",
        {"t": "usage", "id": 21, "b": {"at": 1793995200}},
        env("usage", 21, {0: 1793995200}),
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
    "attention_window": (
        "Approval with its answer window (proto 0.11): 42.5 s of 60 s left when sent, and a cut preview whose "
        "full text is 612 bytes (fetch it with cmd detail)",
        {"t": "attention", "id": 15, "b": {"rid": 10, "sid": 2, "kind": "approval",
         "title": "Run shell command", "text": "cd packages/frontend && npm run test -- --watch=false\u2026",
         "acts": ["approve", "deny", "focus"], "exp": 1793995260, "left": 42500, "win": 60000, "len": 612}},
        env("attention", 15, {0: 10, 1: 2, 2: 0, 3: "Run shell command",
            4: "cd packages/frontend && npm run test -- --watch=false\u2026",
            6: [0, 1, 2], 7: 1793995260, 9: 42500, 10: 60000, 11: 612}),
    ),
    "attention_hush": (
        "Approval the device need not buzz for (proto 0.12): the user is looking at the session's terminal, so no "
        "buzz, glow or reminders; the card is shown and answered as usual",
        {"t": "attention", "id": 16, "b": {"rid": 11, "sid": 2, "kind": "approval",
         "title": "Run shell command", "text": "npm test", "acts": ["approve", "deny", "focus"],
         "exp": 1793995262, "left": 2000, "win": 2000, "hush": True}},
        env("attention", 16, {0: 11, 1: 2, 2: 0, 3: "Run shell command", 4: "npm test", 6: [0, 1, 2],
            7: 1793995262, 9: 2000, 10: 2000, 12: True}),
    ),
    "session_hush": (
        "A session enters done and the device need not buzz (proto 0.12): the user is looking at its terminal",
        {"t": "session_upd", "id": 17, "b": {"sid": 3, "st": "done", "tst": 1793995300, "hush": True}},
        env("session_upd", 17, {0: 3, 2: 4, 8: 1793995300, 26: True}),
    ),
    "attention_standing_rule": (
        "Approval offering a standing rule (proto 0.13, STANDING-RULES.md): the rule exactly as Claude Code saves "
        "it, saved for this project, and wide (it ends in a wildcard)",
        {"t": "attention", "id": 18, "b": {"rid": 12, "sid": 2, "kind": "approval",
         "title": "Run shell command", "text": "npm test", "acts": ["approve", "deny", "focus"],
         "exp": 1793995262, "left": 60000, "win": 60000,
         "rule": "Bash(npm test:*)", "scope": "project", "wide": True}},
        env("attention", 18, {0: 12, 1: 2, 2: 0, 3: "Run shell command", 4: "npm test", 6: [0, 1, 2],
            7: 1793995262, 9: 60000, 10: 60000, 13: "Bash(npm test:*)", 14: 1, 15: True}),
    ),
    "cmd_approve_rule": (
        "Device approves rid 12 and saves its standing rule after a 1600 ms hold (proto 0.13): rh is FNV-1a 32 of "
        "'Bash(npm test:*)', 0x00, scope 1, wide 1",
        {"t": "cmd", "id": 19, "b": {"op": "approve", "sid": 2, "rid": 12, "hold_ms": 1600, "rh": 0x516A52ED}},
        env("cmd", 19, {0: 3, 1: 2, 2: 12, 4: 1600, 5: 0x516A52ED}),
    ),
    "cmd_detail": (
        "Device asks for request 10's full text from byte 0 (proto 0.11)",
        {"t": "cmd", "id": 13, "b": {"op": "detail", "sid": 2, "rid": 10, "arg": 0}},
        env("cmd", 13, {0: 7, 1: 2, 2: 10, 3: 0}),
    ),
    "detail_chunk": (
        "Second chunk of request 10's full text: bytes 512-611 of 612 (proto 0.11)",
        {"t": "detail", "id": 31, "b": {"rid": 10, "sid": 2, "text": "./scripts/deploy.sh --env prod --yes",
         "off": 512, "total": 612}},
        env("detail", 31, {0: 10, 1: 2, 2: "./scripts/deploy.sh --env prod --yes", 3: 512, 4: 612}),
    ),
    "cmd_focus_handback": (
        "Open on PC from the request itself: focus session 2 and hand request 10 back to the terminal (proto 0.11)",
        {"t": "cmd", "id": 14, "b": {"op": "focus", "sid": 2, "rid": 10}},
        env("cmd", 14, {0: 0, 1: 2, 2: 10}),
    ),
    "clear_handed_back": (
        "Request 10 went back to the terminal because the device's Open on PC asked for it (proto 0.11)",
        {"t": "clear", "id": 32, "b": {"rid": 10, "why": "handed_back"}},
        env("clear", 32, {0: 10, 1: 4}),
    ),
    "clear_expired": (
        "Request 10's answer window ran out: the terminal asks now",
        {"t": "clear", "id": 33, "b": {"rid": 10, "why": "expired"}},
        env("clear", 33, {0: 10, 1: 2}),
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
    "cmd_dictate": (
        "Device sends recording 4's transcript to session 3 after a hold (proto 0.6)",
        {"t": "cmd", "id": 11, "b": {"op": "dictate", "sid": 3, "arg": 4, "hold_ms": 1250}},
        env("cmd", 11, {0: 12, 1: 3, 3: 4, 4: 1250}),
    ),
    "cmd_dictate_answer": (
        "Device answers card 9 (a free-text form field) with recording 4's transcript (proto 0.8)",
        {"t": "cmd", "id": 12, "b": {"op": "dictate", "sid": 3, "rid": 9, "arg": 4, "hold_ms": 1250}},
        env("cmd", 12, {0: 12, 1: 3, 2: 9, 3: 4, 4: 1250}),
    ),
    "attention_question_dictate": (
        "Free-text question for session 3: no choices, answered by dictation or on the PC (proto 0.8)",
        {"t": "attention", "id": 14, "b": {"rid": 9, "sid": 3, "kind": "question",
         "title": "Your note", "text": "Describe the bug", "acts": ["focus", "dictate"], "exp": 1793995500}},
        env("attention", 14, {0: 9, 1: 3, 2: 1, 3: "Your note", 4: "Describe the bug", 6: [2, 5], 7: 1793995500}),
    ),
    "audio_start": (
        "Device starts recording 4 for session 3: IMA ADPCM blocks at 16 kHz (proto 0.6)",
        {"t": "audio_start", "id": 12, "b": {"aid": 4, "codec": "adpcm16k", "sid": 3}},
        env("audio_start", 12, {0: 4, 1: 0, 2: 3}),
    ),
    "audio_chunk": (
        "Second block of recording 4: header (predictor 0, index 0) and 4 bytes of nibbles",
        {"t": "audio", "id": 13, "b": {"aid": 4, "seq": 1, "data": "0000000012345678"}},
        env("audio", 13, {0: 4, 1: 1, 2: bytes.fromhex("0000000012345678")}),
    ),
    "audio_end": (
        "Device stops recording 4",
        {"t": "audio_end", "id": 14, "b": {"aid": 4}},
        env("audio_end", 14, {0: 4}),
    ),
    "transcript": (
        "Daemon answers recording 4 with its transcript; never sent automatically",
        {"t": "transcript", "id": 20, "b": {"aid": 4, "text": "run the tests again", "autosend_ms": 0}},
        env("transcript", 20, {0: 4, 1: "run the tests again", 2: 0}),
    ),
    "welcome_keys": (
        "Welcome (proto 0.7): quick replies and the 71002100a6002800300077004900a60028003000200099003601a0003000 swipe keyboard (the daemon decodes strokes)",
        {"t": "welcome", "id": 1, "re": 1, "b": {"proto": [0, 7],
         "host": {"id": "a1a2a3a4a5a6a7a8", "name": "dev-mac", "os": "mac"},
         "time": {"unix": 1793995200, "tz_off": -480},
         "cfg": {"privacy": "preview", "alert": "full"},
         "replies": ["Continue"], "keys": True}},
        env("welcome", 1, {0: [0, 7], 1: {0: bytes.fromhex("a1a2a3a4a5a6a7a8"), 1: "dev-mac", 2: 0},
            2: {0: 1793995200, 1: -480}, 3: {0: 1, 1: 0}, 4: ["Continue"], 6: True}, re_=1),
    ),
    "kb_start": (
        "Keyboard 5 opens for session 3 with three keys (q, w, space): u16 cp, i16 x, i16 y, u16 w, u16 h each",
        {"t": "kb_start", "id": 15, "b": {"kid": 5, "sid": 3, "keys": "71002100a6002800300077004900a60028003000200099003601a0003000"}},
        env("kb_start", 15, {0: 5, 1: 3, 2: bytes.fromhex("71002100a6002800300077004900a60028003000200099003601a0003000")}),
    ),
    "kb_stroke": (
        "Stroke 1 of keyboard 5: four points (i16 x, i16 y, u16 t_ms since touch-down)",
        {"t": "kb_stroke", "id": 16, "b": {"kid": 5, "seq": 1, "pts": "d500be0000008c00f00021004200ee0042003c00ec006300"}},
        env("kb_stroke", 16, {0: 5, 1: 1, 2: bytes.fromhex("d500be0000008c00f00021004200ee0042003c00ec006300")}),
    ),
    "kb_cands": (
        "Daemon answers stroke 1: four candidates, the first one typed into the terminal",
        {"t": "kb_cands", "id": 30, "b": {"kid": 5, "seq": 1, "cands": ["flash", "flask", "flashed", "slash"], "typed": 0}},
        env("kb_cands", 30, {0: 5, 1: 1, 2: ["flash", "flask", "flashed", "slash"], 3: 0}),
    ),
    "kb_cands_denied": (
        "Daemon decoded stroke 2 but typed nothing: the session waits on an approval",
        {"t": "kb_cands", "id": 31, "b": {"kid": 5, "seq": 2, "cands": ["yes", "tea"], "err": "denied"}},
        env("kb_cands", 31, {0: 5, 1: 2, 2: ["yes", "tea"], 4: 1}),
    ),
    "cmd_kb_pick": (
        "Device replaces the last typed word of session 3 with candidate 2",
        {"t": "cmd", "id": 17, "b": {"op": "kb_pick", "sid": 3, "arg": 2}},
        env("cmd", 17, {0: 13, 1: 3, 3: 2}),
    ),
    "cmd_kb_send": (
        "Device presses Enter for session 3 after a hold: the typed text goes to the agent",
        {"t": "cmd", "id": 18, "b": {"op": "kb_send", "sid": 3, "hold_ms": 1250}},
        env("cmd", 18, {0: 15, 1: 3, 4: 1250}),
    ),
    "cmd_kb_end": (
        "Keyboard closed; erase what it typed and did not send",
        {"t": "cmd", "id": 19, "b": {"op": "kb_end", "sid": 3, "arg": 1}},
        env("cmd", 19, {0: 16, 1: 3, 3: 1}),
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
