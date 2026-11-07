#!/usr/bin/env python3
"""Extract the Atlas "living facts" from the firmware, protocol and daemon sources.

Writes atlas/data/facts.json and atlas/css/tokens.generated.css.
  python3 atlas/tools/extract.py            regenerate both files
  python3 atlas/tools/extract.py --check    exit 1 if the committed files differ

Every numeric/colour/UUID fact is read from source with a regex and recorded with a
`file:line` in `sources`. If a pattern is not found the script exits 1 with a message, so CI
catches drift. Only prose (message summaries, gesture descriptions, haptic meanings) is
editorial, and even that is cross-checked: a new message type or gesture case in the source
without an entry here also fails.
Python 3 stdlib only.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT_JSON = ROOT / "atlas/data/facts.json"
OUT_CSS = ROOT / "atlas/css/tokens.generated.css"

CORE = "firmware/components/core/src/"
SOURCES = {}
_cache = {}


class Drift(Exception):
    pass


def text(rel):
    if rel not in _cache:
        p = ROOT / rel
        if not p.exists():
            raise Drift(f"source file missing: {rel}")
        _cache[rel] = p.read_text(encoding="utf-8")
    return _cache[rel]


def line_of(rel, pos):
    return text(rel).count("\n", 0, pos) + 1


def find(rel, pattern, key=None, flags=0):
    """Return the first regex match in `rel`; record file:line under `key`."""
    m = re.search(pattern, text(rel), flags)
    if not m:
        raise Drift(f"pattern not found in {rel}: {pattern}")
    if key:
        SOURCES[key] = f"{rel}:{line_of(rel, m.start())}"
    return m


def find_all(rel, pattern, flags=0):
    ms = list(re.finditer(pattern, text(rel), flags))
    if not ms:
        raise Drift(f"pattern not found in {rel}: {pattern}")
    return ms


# ───────────────────────── palette ─────────────────────────
def extract_palette():
    rel = CORE + "ui_theme.h"
    names = {
        "bg": r"COL_BG\s+lv_color_hex\(0x([0-9a-fA-F]{6})\)",
        "surface": r"COL_SURFACE\s+lv_color_hex\(0x([0-9a-fA-F]{6})\)",
        "surface2": r"COL_SURFACE2\s+lv_color_hex\(0x([0-9a-fA-F]{6})\)",
        "border": r"COL_BORDER\s+lv_color_hex\(0x([0-9a-fA-F]{6})\)",
        "text": r"COL_TEXT\s+lv_color_hex\(0x([0-9a-fA-F]{6})\)",
        "muted": r"COL_MUTED\s+lv_color_hex\(0x([0-9a-fA-F]{6})\)",
        "amber": r"RGB_AMBER\s+0x([0-9a-fA-F]{6})",
        "danger": r"RGB_DANGER\s+0x([0-9a-fA-F]{6})",
        "success": r"RGB_SUCCESS\s+0x([0-9a-fA-F]{6})",
        "accent": r"RGB_ACCENT\s+0x([0-9a-fA-F]{6})",
        "idle": r"RGB_IDLE\s+0x([0-9a-fA-F]{6})",
        "cloud": r"RGB_CLOUD\s+0x([0-9a-fA-F]{6})",
    }
    pal = {}
    for k, pat in names.items():
        m = find(rel, pat, f"palette.{k}")
        pal[k] = "#" + m.group(1).lower()
    return pal


# ───────────────────────── protocol (CDDL) ─────────────────────────
CDDL = "protocol/agentlink.cddl"


def cddl_enum(prefix):
    """`prefix_name = N` lines → {N: name}."""
    out = {}
    for m in re.finditer(rf"^{prefix}_(\w+)\s*=\s*(\d+)\s*$", text(CDDL), re.M):
        out[int(m.group(2))] = m.group(1)
    if not out:
        raise Drift(f"no CDDL enum with prefix {prefix}_ in {CDDL}")
    return out


def extract_states(pal):
    cddl_states = cddl_enum("sess_state")
    src = CORE + "ui_theme.c"
    body = find(src, r"uint32_t theme_state_rgb\(sess_state_t s\)\s*\{(.*?)\n\}", None, re.S)
    base = line_of(src, body.start(1))
    # case SESS_X: [case SESS_Y:] return RGB_Z;  (fallthrough groups)
    colour_of, pending = {}, []
    for i, ln in enumerate(body.group(1).split("\n")):
        for c in re.findall(r"case SESS_(\w+):", ln):
            pending.append((c.lower(), base + i))
        r = re.search(r"(?:default:\s*)?return RGB_(\w+);", ln)
        if r:
            for st, _ in pending:
                colour_of[st] = (r.group(1).lower(), base + i)
            default = r.group(1).lower()
            pending = []
    labels_m = find(src, r"const char \*theme_state_label\(const session_t \*s\)\s*\{(.*?)\n\}", None, re.S)
    lbase = line_of(src, labels_m.start(1))
    label_of = {}
    for i, ln in enumerate(labels_m.group(1).split("\n")):
        m = re.search(r"case SESS_(\w+): return (.*);", ln)
        if m:
            label_of[m.group(1).lower()] = (m.group(2), lbase + i)
        m = re.search(r"default: return \"(\w+)\"", ln)
        if m:
            label_of["__default"] = (f'"{m.group(1)}"', lbase + i)
    # the `case X:` before `default` isn't present for ended; it is the default
    states = {}
    for sid, name in sorted(cddl_states.items()):
        colour_name, cline = colour_of.get(name, (default, None))
        lab = label_of.get(name) or label_of.get("__default")
        if not lab:
            raise Drift(f"no label for state {name} in {src}")
        raw, lline = lab
        words = re.findall(r'"([^"]+)"', raw)
        label = words[-1] if name != "input" else "needs approval"
        entry = {
            "id": sid,
            "color": pal[colour_name],
            "palette": colour_name,
            "label": label[:1].upper() + label[1:],
            "source": f"{src}:{cline or lline}",
        }
        if name == "input":
            entry["sub_labels"] = {"0": "Needs approval", "1": "Question"}
            if words != ["question", "needs approval"] and set(words) != {"question", "needs approval"}:
                raise Drift(f"unexpected input labels {words} in {src}")
            entry["label"] = "Needs you"
        states[name] = entry
    # cross-check the C enum matches the wire ids
    for m in find_all(CORE + "model.h", r"SESS_(\w+) = (\d+)"):
        if cddl_states.get(int(m.group(2))) != m.group(1).lower():
            raise Drift(f"model.h SESS_{m.group(1)} = {m.group(2)} disagrees with {CDDL}")
    SOURCES["states"] = f"{src}:{base}"
    return states


MSG_EDITORIAL = {
    # name: (direction, summary). Cross-checked against the CDDL message list.
    "hello": ("dev→pc", "Device introduces itself: protocol version, board, capabilities"),
    "welcome": ("pc→dev", "Daemon answers with host info, wall-clock time and device settings"),
    "snapshot": ("pc→dev", "Full list of sessions, plus events missed while disconnected"),
    "session_upd": ("pc→dev", "One session changed: a delta, only the changed keys"),
    "gone": ("pc→dev", "A session ended and left the list"),
    "attention": ("pc→dev", "An agent needs you: approval, question, error or done"),
    "clear": ("pc→dev", "An attention request was answered elsewhere or expired"),
    "cmd": ("dev→pc", "A user action: focus, approve, deny, reply, snooze..."),
    "result": ("pc→dev", "Outcome of a command: ok or a reason it failed"),
    "detail": ("pc→dev", "Full text for the detail screen"),
    "cfg": ("pc→dev", "Device settings: privacy, alert mode, brightness"),
    "batt": ("dev→pc", "Battery percentage and charging state"),
    "ping": ("both", "Link-liveness probe"),
    "pong": ("both", "Answer to a ping"),
    "log": ("dev→pc", "A device log line"),
    "audio_start": ("dev→pc", "Voice capture begins (reserved)"),
    "audio": ("dev→pc", "A chunk of encoded audio (reserved)"),
    "audio_end": ("dev→pc", "Voice capture ends (reserved)"),
    "transcript": ("pc→dev", "Speech-to-text result (reserved)"),
    "dbg_ui_state_req": ("pc→dev", "Debug: ask which screen is showing"),
    "dbg_ui_state": ("dev→pc", "Debug: the current screen and visible labels"),
    "dbg_screenshot_req": ("pc→dev", "Debug: ask for a screenshot"),
    "dbg_screenshot_chunk": ("dev→pc", "Debug: a slice of the RGB565 screenshot"),
    "dbg_inject_input": ("pc→dev", "Debug: inject a tap, swipe or button press"),
    "dbg_set_log_level": ("pc→dev", "Debug: change the device log verbosity"),
}


def parse_cddl_block(name):
    """Fields of `name = { ... }` as [{key, type, note}] using the trailing comments."""
    m = re.search(rf"^{name}\s*=\s*\{{(.*?)^\}}", text(CDDL), re.M | re.S)
    if not m:
        return []
    fields = []
    for ln in m.group(1).split("\n"):
        f = re.match(r"\s*(\?\s*)?(\d+)\s*=>\s*([^,;]+?)\s*,?\s*(?:;\s*(.*))?$", ln)
        if f:
            note = (f.group(4) or "").strip()
            fields.append({"key": int(f.group(2)), "optional": bool(f.group(1)),
                           "type": f.group(3).strip(), "note": note,
                           "name": note.split()[0] if note else ""})
    return fields


def extract_messages():
    ids = {}
    for m in re.finditer(r"^mt_(\w+)\s*=\s*(\d+)\s*$", text(CDDL), re.M):
        ids[m.group(1)] = (int(m.group(2)), line_of(CDDL, m.start()))
    if not ids:
        raise Drift(f"no mt_* message types in {CDDL}")
    missing = sorted(set(ids) - set(MSG_EDITORIAL))
    stale = sorted(set(MSG_EDITORIAL) - set(ids))
    if missing or stale:
        raise Drift(f"message list drift: CDDL has new {missing}, extract.py has stale {stale}")
    out = []
    for name, (t, ln) in sorted(ids.items(), key=lambda kv: kv[1][0]):
        d, s = MSG_EDITORIAL[name]
        body = {"hello": "hello", "welcome": "welcome", "snapshot": "snapshot", "session_upd": "session",
                "gone": None, "attention": "attention", "clear": "clear", "cmd": "cmd", "result": "result",
                "detail": "detail", "cfg": "device_cfg", "batt": "batt", "log": "log"}.get(name, name)
        out.append({"name": name, "type": t, "dir": d, "summary": s,
                    "fields": parse_cddl_block(body) if body else [], "source": f"{CDDL}:{ln}"})
    SOURCES["messages"] = f"{CDDL}:1"
    return out


# ───────────────────────── gestures ─────────────────────────
def extract_gesture():
    rel = CORE + "gesture.h"
    g = {}
    for key, macro in [("double_tap_ms", "GESTURE_DOUBLE_TAP_MS"), ("long_press_ms", "GESTURE_LONG_PRESS_MS"),
                       ("swipe_min_px", "GESTURE_SWIPE_MIN_PX"), ("swipe_max_ms", "GESTURE_SWIPE_MAX_MS"),
                       ("tap_slop_px", "GESTURE_TAP_SLOP_PX")]:
        g[key] = int(find(rel, rf"#define {macro}\s+(\d+)", f"gesture.{key}").group(1))
    slack = int(find(CORE + "app.c", r"#define DEFER_SLACK_MS\s+(\d+)", "gesture.defer_slack_ms").group(1))
    find(CORE + "app.c", r"now - s_deferred_at >= GESTURE_DOUBLE_TAP_MS \+ DEFER_SLACK_MS", "gesture.tap_defer_ms")
    g["tap_defer_ms"] = g["double_tap_ms"] + slack
    g["defer_slack_ms"] = slack
    g["second_tap_radius_px"] = int(find(
        CORE + "gesture.c", r"abs\(g->x0 - g->x_tap\) <= (\d+)", "gesture.second_tap_radius_px").group(1))
    g["hold_confirm_ms"] = int(find(CORE + "ui_internal.h", r"#define UI_HOLD_MS\s+(\d+)",
                                    "gesture.hold_confirm_ms").group(1))
    return g


# case labels in each screen's gesture() → description (editorial, cross-checked)
SCREEN_FILES = {"glance": "ui_glance.c", "list": "ui_list.c", "detail": "ui_detail.c"}
ACTION_TEXT = {
    ("glance", "G_TAP"): "Tap a session segment → open its detail. Tap elsewhere → list",
    ("glance", "G_SWIPE_UP"): "Open the list",
    ("glance", "G_BTN_A"): "Open the list",
    ("list", "G_SWIPE_UP"): "Select the next session",
    ("list", "G_SWIPE_DOWN"): "Select the previous session (or back to glance from the top)",
    ("list", "G_TAP"): "Tap a row to select it; tap the selected row to open detail (deferred)",
    ("list", "G_DOUBLE_TAP"): "Focus that session's terminal on the PC",
    ("list", "G_BTN_A"): "Open detail for the selected session",
    ("list", "G_SWIPE_LEFT"): "Back to glance",
    ("list", "G_BTN_B"): "Back to glance",
    ("detail", "G_PRESS"): "Finger down on Approve starts the hold-to-confirm",
    ("detail", "G_RELEASE"): "Lifting early cancels the hold",
    ("detail", "G_LONG_PRESS"): "Hold Approve long enough to confirm",
    ("detail", "G_TAP"): "Tap Deny to deny; tap Open to focus the PC (deferred)",
    ("detail", "G_DOUBLE_TAP"): "Focus that session's terminal on the PC",
    ("detail", "G_SWIPE_RIGHT"): "Acknowledge the session and go back to the list",
    ("detail", "G_SWIPE_LEFT"): "Back to the list",
    ("detail", "G_BTN_B"): "Back to the list",
    ("detail", "G_BTN_A"): "Focus that session's terminal on the PC",
}


def extract_gesture_map():
    out = {}
    seen = set()
    for screen, fn in SCREEN_FILES.items():
        rel = CORE + fn
        m = find(rel, r"static bool gesture\(const gesture_evt_t \*g\)\s*\{(.*?)\n\}\n", None, re.S)
        base = line_of(rel, m.start(1))
        cases = []
        for i, ln in enumerate(m.group(1).split("\n")):
            for c in re.findall(r"case (G_\w+):", ln):
                cases.append({"gesture": c, "source": f"{rel}:{base + i}"})
        if not cases:
            raise Drift(f"no gesture cases in {rel}")
        for c in cases:
            key = (screen, c["gesture"])
            if key not in ACTION_TEXT:
                raise Drift(f"{rel}: gesture {c['gesture']} on {screen} has no description in extract.py")
            c["action"] = ACTION_TEXT[key]
            seen.add(key)
        out[screen] = cases
    stale = sorted(set(ACTION_TEXT) - seen)
    if stale:
        raise Drift(f"extract.py describes gestures no longer in the source: {stale}")
    # app.c global routing + deferral rule
    find(CORE + "app.c", r"g->kind == G_SHAKE \|\| g->kind == G_BTN_B_LONG", "gesture_map.global")
    find(CORE + "ui.c", r"if \(s_current == SCR_DETAIL\) return true;", "gesture_map.defer_detail")
    find(CORE + "ui.c", r"return s_current == SCR_LIST && ui_list_tap_navigates", "gesture_map.defer_list")
    out["global"] = [
        {"gesture": "G_SHAKE", "action": "Jump to glance, from any screen", "source": SOURCES["gesture_map.global"]},
        {"gesture": "G_BTN_B_LONG", "action": "Jump to glance, from any screen", "source": SOURCES["gesture_map.global"]},
    ]
    out["deferred_taps"] = {"detail": "every tap", "list": "a tap on the already-selected row",
                            "source": SOURCES["gesture_map.defer_list"]}
    return out


# ───────────────────────── haptics ─────────────────────────
HAPTIC_MEANING = {
    # pattern name → (C function/array that defines it, meaning)
    "tick": ("feedback_tick", "tick", "A light acknowledgement of a touch or selection"),
    "confirm": ("feedback_confirm", "ok", "An action went through"),
    "reject": ("feedback_reject", "no", "An action was refused"),
    "hello": ("feedback_hello", "hello", "Link up: proves the motor path, even in silent mode"),
    "done": ("feedback_attention", "done", "An agent finished"),
    "error": ("feedback_attention", "error", "An agent hit an error"),
    "needs_input": ("feedback_attention", "needs_input", "An agent needs approval or an answer"),
}


def extract_haptics():
    rel = CORE + "feedback.c"
    inp = "firmware/boards/stopwatch/input.cpp"
    duty = int(find(inp, r"#define HAPTIC_DUTY\s+(\d+)", "haptics.duty").group(1))
    kick_duty = int(find(inp, r"#define HAPTIC_KICK_DUTY\s+(\d+)", "haptics.kick_duty").group(1))
    kick_ms = int(find(inp, r"#define HAPTIC_KICK_MS\s+(\d+)", "haptics.kick_ms").group(1))
    find(inp, r"ms >= 2 \* HAPTIC_KICK_MS", "haptics.kick_rule")
    patterns, meanings = {}, {}
    for name, (_, arr, meaning) in HAPTIC_MEANING.items():
        m = find(rel, rf"static const uint16_t {arr}\[\]\s*=\s*\{{([\d,\s]+)\}};", f"haptics.{name}")
        vals = [int(v) for v in m.group(1).replace(" ", "").strip(",").split(",") if v]
        steps = []
        for i in range(0, len(vals), 2):
            on = vals[i]
            off = vals[i + 1] if i + 1 < len(vals) else 0
            has_kick = on >= 2 * kick_ms
            steps.append({"on_ms": on, "off_ms": off, "duty": duty,
                          "kick_ms": kick_ms if has_kick else 0, "kick_duty": kick_duty if has_kick else duty})
        patterns[name] = steps
        meanings[name] = meaning
    return {"patterns": patterns, "meaning": meanings,
            "motor": {"hold_duty": duty, "kick_duty": kick_duty, "kick_ms": kick_ms,
                      "kick_min_pulse_ms": 2 * kick_ms}}


# ───────────────────────── link / usb / ble / display ─────────────────────────
def extract_link():
    return {
        "hello_retry_ms": int(find(CORE + "link.c", r"#define HELLO_RETRY_MS\s+(\d+)", "link.hello_retry_ms").group(1)),
        "idle_ping_ms": int(find(CORE + "link.c", r"#define IDLE_PING_MS\s+(\d+)", "link.idle_ping_ms").group(1)),
        "ping_interval_ms": 1000 * int(find("daemon/crates/agentlinkd/src/usb.rs",
                                            r"const PING_EVERY: Duration = Duration::from_secs\((\d+)\)",
                                            "link.ping_interval_ms").group(1)),
        "silence_timeout_ms": int(find("firmware/boards/stopwatch/link_usb.cpp", r"#define SILENCE_MS\s+(\d+)",
                                       "link.silence_timeout_ms").group(1)),
    }


def extract_usb():
    usb = "daemon/crates/agentlinkd/src/usb.rs"
    frame = "daemon/crates/agentlinkd/src/usb/frame.rs"
    find(frame, r"let mut crc = 0xFFFFu16", "usb.crc")
    find(frame, r"\(crc << 1\) \^ 0x1021", "usb.crc_poly")
    return {
        "vid": "0x" + find(usb, r"const ESP_VID: u16 = 0x([0-9A-Fa-f]{4})", "usb.vid").group(1).upper(),
        "pid": "0x" + find(usb, r"const ESP_PID: u16 = 0x([0-9A-Fa-f]{4})", "usb.pid").group(1).upper(),
        "baud": int(find(usb, r"const BAUD: u32 = ([\d_]+)", "usb.baud").group(1).replace("_", "")),
        "crc": "CRC-16/CCITT-FALSE",
        "max_payload": int(find(frame, r"pub const MAX_PAYLOAD: usize = (\d+)", "usb.max_payload").group(1)),
        "framing": "0x00 ‖ COBS(payload ‖ crc16_be) ‖ 0x00",
    }


def extract_ble():
    ble = "daemon/crates/agentlinkd/src/ble.rs"

    def uuid(const, key):
        h = find(ble, rf"const {const}: Uuid = Uuid::from_u128\(0x([0-9a-f_]+)\)", key).group(1).replace("_", "")
        return f"{h[:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:]}"

    return {
        "service": uuid("SERVICE_UUID", "ble.service"),
        "rx": uuid("RX_UUID", "ble.rx"),
        "tx": uuid("TX_UUID", "ble.tx"),
        "name_prefix": find(ble, r'name\.starts_with\("([^"]+)"\)', "ble.name_prefix").group(1),
        "default_write_len": int(find(ble, r"const DEFAULT_WRITE_LEN: usize = (\d+)", "ble.default_write_len").group(1)),
        "max_write_len": int(find(ble, r"const MAX_WRITE_LEN: usize = (\d+)", "ble.max_write_len").group(1)),
    }


def extract_display():
    p = "firmware/boards/stopwatch/board_priv.h"
    find("firmware/boards/stopwatch/display.cpp", r"466x466 round AMOLED", "display.shape")
    return {"width": int(find(p, r"#define SW_PANEL_W\s+(\d+)", "display.width").group(1)),
            "height": int(find(p, r"#define SW_PANEL_H\s+(\d+)", "display.height").group(1)),
            "shape": "round"}


# ───────────────────────── output ─────────────────────────
def build():
    SOURCES.clear()
    pal = extract_palette()
    facts = {
        "generated_from": "worktree",
        "palette": pal,
        "states": extract_states(pal),
        "gesture": extract_gesture(),
        "gesture_map": extract_gesture_map(),
        "haptics": None,
        "link": extract_link(),
        "usb": extract_usb(),
        "ble": extract_ble(),
        "display": extract_display(),
        "messages": extract_messages(),
    }
    h = extract_haptics()
    facts["haptics"] = h["patterns"]
    facts["haptic_meaning"] = h["meaning"]
    facts["haptic_motor"] = h["motor"]
    facts["sources"] = dict(sorted(SOURCES.items()))
    return facts


def css(facts):
    lines = ["/* GENERATED by atlas/tools/extract.py from firmware sources. Do not edit. */", ":root {"]
    for k, v in facts["palette"].items():
        lines.append(f"  --fw-{k}: {v};")
    for k, s in facts["states"].items():
        lines.append(f"  --fw-state-{k}: {s['color']};")
    for k, v in facts["gesture"].items():
        unit = "px" if k.endswith("_px") else "ms"
        lines.append(f"  --fw-{k.replace('_', '-')}: {v}{unit};")
    m = facts["haptic_motor"]
    lines.append(f"  --fw-haptic-kick-ms: {m['kick_ms']}ms;")
    lines.append(f"  --fw-display-size: {facts['display']['width']}px;")
    lines.append("}")
    return "\n".join(lines) + "\n"


def main():
    check = "--check" in sys.argv[1:]
    try:
        facts = build()
    except Drift as e:
        print(f"extract.py: DRIFT: {e}", file=sys.stderr)
        return 1
    j = json.dumps(facts, indent=2, ensure_ascii=False) + "\n"
    c = css(facts)
    if check:
        bad = [p for p, t in ((OUT_JSON, j), (OUT_CSS, c)) if not p.exists() or p.read_text(encoding="utf-8") != t]
        if bad:
            print("extract.py: generated files are stale: " + ", ".join(str(p.relative_to(ROOT)) for p in bad)
                  + "\nrun: python3 atlas/tools/extract.py", file=sys.stderr)
            return 1
        print("atlas facts up to date")
        return 0
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_CSS.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(j, encoding="utf-8")
    OUT_CSS.write_text(c, encoding="utf-8")
    print(f"wrote {OUT_JSON.relative_to(ROOT)} ({len(SOURCES)} sourced facts) and {OUT_CSS.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
