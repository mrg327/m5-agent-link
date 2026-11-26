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
_cache: dict[str, str] = {}


class Drift(Exception):
    pass


def text(rel: str) -> str:
    if rel not in _cache:
        p = ROOT / rel
        if not p.exists():
            raise Drift(f"source file missing: {rel}")
        _cache[rel] = p.read_text(encoding="utf-8")
    return _cache[rel]


def line_of(rel: str, pos: int) -> int:
    return text(rel).count("\n", 0, pos) + 1


def find(rel: str, pattern: str, key: str | None = None, flags: int = 0) -> re.Match[str]:
    """Return the first regex match in `rel`; record file:line under `key`."""
    m = re.search(pattern, text(rel), flags)
    if not m:
        raise Drift(f"pattern not found in {rel}: {pattern}")
    if key:
        SOURCES[key] = f"{rel}:{line_of(rel, m.start())}"
    return m


def find_all(rel: str, pattern: str, flags: int = 0) -> list[re.Match[str]]:
    ms = list(re.finditer(pattern, text(rel), flags))
    if not ms:
        raise Drift(f"pattern not found in {rel}: {pattern}")
    return ms


# ───────────────────────── palette ─────────────────────────
def extract_palette():
    rel = CORE + "ui_theme.h"
    pal = {}

    def col(k):
        """COL_K as a hex colour; an alias (`COL_MUTED COL_TEXT2`) is followed."""
        m = find(
            rel,
            rf"#define COL_{k.upper()}\s+(?:lv_color_hex\(0x([0-9a-fA-F]{{6}})\)|COL_(\w+))",
            f"palette.{k}",
        )
        return "#" + m.group(1).lower() if m.group(1) else col(m.group(2).lower())

    # UI-V3 names (text2, text3, fill1, fill2) come with the old ones, which are aliases now
    for k in ("bg", "surface", "surface2", "text", "muted", "text2", "text3", "fill1", "fill2"):
        pal[k] = col(k)
    # State colours (Okabe-Ito). The old Primer names are aliases of them, e.g. `RGB_AMBER RGB_PERMISSION`.
    for k in ("working", "ask", "permission", "done", "error", "idle"):
        m = find(rel, rf"#define RGB_{k.upper()}\s+0x([0-9a-fA-F]{{6}})", f"palette.{k}")
        pal[k] = "#" + m.group(1).lower()
    for k, alias in (
        ("amber", "AMBER"),
        ("danger", "DANGER"),
        ("success", "SUCCESS"),
        ("accent", "ACCENT"),
    ):
        m = find(rel, rf"#define RGB_{alias}\s+RGB_(\w+)", f"palette.{k}")
        target = m.group(1).lower()
        if target not in pal:
            raise Drift(f"RGB_{alias} aliases unknown RGB_{target.upper()} in {rel}")
        pal[k] = pal[target]
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
    word_m = find(
        src, r"const char \*theme_state_word\(const session_t \*s\)\s*\{(.*?)\n\}", None, re.S
    )
    wbase = line_of(src, word_m.start(1))
    word_of = {}
    for i, ln in enumerate(word_m.group(1).split("\n")):
        m = re.search(r'case SESS_(\w+): return "([^"]+)";', ln)
        if m:
            word_of[m.group(1).lower()] = (m.group(2), wbase + i)
        m = re.search(r'default: return "([^"]+)";', ln)
        if m:
            word_of["__default"] = (m.group(1), wbase + i)
    inp = find(
        src, r'case SESS_INPUT: return s->sub == 1 \? "([^"]+)" : "([^"]+)";', "states.input_words"
    )
    sub_c = find(
        src,
        r"s->state == SESS_INPUT && s->sub == 0 \? RGB_(\w+) : theme_state_rgb",
        "states.permission_colour",
    )
    states = {}
    for sid, name in sorted(cddl_states.items()):
        colour_name, cline = colour_of.get(name, (default, None))
        word, wline = word_of.get(name) or word_of["__default"]
        entry = {
            "id": sid,
            "color": pal[colour_name],
            "palette": colour_name,
            "label": word,
            "source": f"{src}:{cline or wline}",
        }
        if name == "input":
            entry["label"] = inp.group(1)
            entry["sub_labels"] = {"0": inp.group(2), "1": inp.group(1)}
            entry["sub_colors"] = {"0": pal[sub_c.group(1).lower()], "1": pal[colour_name]}
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
    "kb_start": ("dev→pc", "KEYS: keyboard opens, with its key rectangles"),
    "kb_stroke": ("dev→pc", "KEYS: the touch points of one swiped word"),
    "kb_cands": ("pc→dev", "KEYS: decoded word candidates and which one was typed"),
    "usage": (
        "pc→dev",
        "Plan usage: the 5-hour and weekly windows, their resets and the pace (GAUGES.md)",
    ),
    "fw_begin": (
        "pc→dev",
        "Firmware update: opens (or resumes, or with size 0 stops) a signed image transfer",
    ),
    "fw_chunk": ("pc→dev", "Firmware update: 2 KB of the image at an offset"),
    "fw_ack": (
        "dev→pc",
        "Firmware update: go / done / busy / bad / fail / stopped, and the next offset wanted",
    ),
    "graph": (
        "pc→dev",
        "Agent graph: one session's recent tool calls as lanes of glyph nodes (GRAPH.md)",
    ),
    "power": (
        "dev→pc",
        "Power report: battery voltage and the time spent in each power state (POWER.md)",
    ),
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
            fields.append(
                {
                    "key": int(f.group(2)),
                    "optional": bool(f.group(1)),
                    "type": f.group(3).strip(),
                    "note": note,
                    "name": note.split()[0] if note else "",
                }
            )
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
        body = {
            "hello": "hello",
            "welcome": "welcome",
            "snapshot": "snapshot",
            "session_upd": "session",
            "gone": None,
            "attention": "attention",
            "clear": "clear",
            "cmd": "cmd",
            "result": "result",
            "detail": "detail",
            "cfg": "device_cfg",
            "batt": "batt",
            "log": "log",
        }.get(name, name)
        out.append(
            {
                "name": name,
                "type": t,
                "dir": d,
                "summary": s,
                "fields": parse_cddl_block(body) if body else [],
                "source": f"{CDDL}:{ln}",
            }
        )
    SOURCES["messages"] = f"{CDDL}:1"
    return out


# ───────────────────────── gestures ─────────────────────────
def extract_gesture():
    rel = CORE + "gesture.h"
    g = {}
    for key, macro in [
        ("long_press_ms", "GESTURE_LONG_PRESS_MS"),
        ("swipe_min_px", "GESTURE_SWIPE_MIN_PX"),
        ("swipe_max_ms", "GESTURE_SWIPE_MAX_MS"),
        ("tap_slop_px", "GESTURE_TAP_SLOP_PX"),
        ("btn_double_ms", "GESTURE_BTN_DOUBLE_MS"),
        ("cx_px", "GESTURE_CX"),
        ("cy_px", "GESTURE_CY"),
        ("rim_in_px", "GESTURE_RIM_IN_PX"),
        ("rim_out_px", "GESTURE_RIM_OUT_PX"),
        ("rim_track_in_px", "GESTURE_RIM_TRACK_IN_PX"),
        ("rim_track_out_px", "GESTURE_RIM_TRACK_OUT_PX"),
        ("scrub_start_deg", "GESTURE_SCRUB_START_DEG"),
        ("scrub_hyst_deg", "GESTURE_SCRUB_HYST_DEG"),
    ]:
        g[key] = int(find(rel, rf"#define {macro}\s+(\d+)", f"gesture.{key}").group(1))
    # the hysteresis is capped at a third of a sector; fail loudly if that rule changes
    find(
        CORE + "gesture.c",
        r"GESTURE_SCRUB_HYST_DEG < width / 3\.0f",
        "gesture.scrub_hysteresis_cap",
    )
    # the safety rules live in the interaction layer (nav.h), not in any drawing file
    nav = CORE + "nav.h"
    g["hold_confirm_ms"] = int(
        find(nav, r"#define NAV_HOLD_MS\s+(\d+)", "gesture.hold_confirm_ms").group(1)
    )
    g["centre_r_px"] = int(
        find(nav, r"#define NAV_CENTRE_R\s+(\d+)", "gesture.centre_r_px").group(1)
    )
    g["hold_show_ms"] = int(
        find(nav, r"#define NAV_HOLD_SHOW_MS\s+(\d+)", "gesture.hold_show_ms").group(1)
    )
    g["card_arm_ms"] = int(
        find(nav, r"#define NAV_CARD_ARM_MS\s+(\d+)", "gesture.card_arm_ms").group(1)
    )
    # where the primary hold disc (Approve) sits on ASK: the hero demo holds it there
    for key, macro in [("ask_hold_x_px", "P_X"), ("ask_hold_y_px", "P_Y")]:
        g[key] = int(
            find(CORE + "ui_ask.c", rf"#define {macro}\s+(\d+)", f"gesture.{key}").group(1)
        )
    g["long_swipe_px"] = int(
        find(nav, r"#define NAV_LONG_SWIPE_PX\s+(\d+)", "gesture.long_swipe_px").group(1)
    )
    g["deny_hold_ms"] = int(
        find(nav, r"#define NAV_DENY_HOLD_MS\s+(\d+)", "gesture.deny_hold_ms").group(1)
    )
    g["agent_timeout_ms"] = int(
        find(nav, r"#define NAV_AGENT_TIMEOUT_MS\s+(\d+)", "gesture.agent_timeout_ms").group(1)
    )
    # SCROLL (UI-V2 §4.4): the drag stream and the flick come from the recogniser, the rate from nav.h
    g["drag_step_px"] = int(
        find(rel, r"#define GESTURE_DRAG_STEP_PX\s+(\d+)", "gesture.drag_step_px").group(1)
    )
    g["flick_px_per_s"] = int(
        find(rel, r"#define GESTURE_FLICK_PX_PER_S\s+(\d+)", "gesture.flick_px_per_s").group(1)
    )
    g["flick_window_ms"] = int(
        find(rel, r"#define GESTURE_FLICK_WINDOW_MS\s+(\d+)", "gesture.flick_window_ms").group(1)
    )
    g["scroll_lines_per_step"] = int(
        find(
            nav, r"#define NAV_SCROLL_LINES_PER_STEP\s+(\d+)", "gesture.scroll_lines_per_step"
        ).group(1)
    )
    g["scroll_coalesce_ms"] = int(
        find(nav, r"#define NAV_SCROLL_COALESCE_MS\s+(\d+)", "gesture.scroll_coalesce_ms").group(1)
    )
    g["scroll_timeout_ms"] = int(
        find(nav, r"#define NAV_SCROLL_TIMEOUT_MS\s+(\d+)", "gesture.scroll_timeout_ms").group(1)
    )
    # STATUS (UI-V2 §4.5): its idle timeout and the quiet cycle's two steps
    g["status_timeout_ms"] = int(
        find(nav, r"#define NAV_STATUS_TIMEOUT_MS\s+(\d+)", "gesture.status_timeout_ms").group(1)
    )
    g["quiet_step1_s"] = 60 * int(
        find(nav, r"#define NAV_QUIET_STEP1_S \((\d+)u \* 60u\)", "gesture.quiet_step1_s").group(1)
    )
    g["quiet_step2_s"] = 60 * int(
        find(nav, r"#define NAV_QUIET_STEP2_S \((\d+)u \* 60u\)", "gesture.quiet_step2_s").group(1)
    )
    find(CORE + "gesture.c", r"if \(btn == 1\) \{\s*emit\(g, G_BTN_B", "gesture.b_immediate", re.S)
    return g


def extract_ring():
    # the deck is two files: ui_home.c (slots, selection, timers) and ui_home_card.c (the centre card);
    # the ring's constants live in its private header, shared by ui_ring.c and ui_ring_draw.c
    home, card, ring = CORE + "ui_home.c", CORE + "ui_home_card.c", CORE + "ui_ring_internal.h"
    r = {}
    for key, macro in [
        ("merge_at", "MERGE_AT"),
        ("ambient_ms", "AMBIENT_MS"),
        ("nag_ms", "NAG_MS"),
        ("nag_max", "NAG_MAX"),
    ]:
        r[key] = int(find(home, rf"#define {macro}\s+(\d+)", f"ring.{key}").group(1))
    r["summary_w_px"] = int(find(card, r"#define SUMMARY_W\s+(\d+)", "ring.summary_w_px").group(1))
    for key, macro in [("sigil_r_px", "SIGIL_R"), ("time_dots_r_px", "DOTS_R")]:
        r[key] = int(find(card, rf"#define {macro}\s+(\d+)", f"ring.{key}").group(1))
    # where the card's parts sit (centres, canvas px): the Atlas anchors its callouts there
    for key, macro in [
        ("sigil_y_px", "SIGIL_Y"),
        ("hero_y_px", "HERO_Y"),
        ("summary_y_px", "LINE1_Y"),
    ]:
        r[key] = int(find(card, rf"#define {macro}\s+(\d+)", f"ring.{key}").group(1))
    # the key hints beside the physical keys on the upper rim (GLYPHS.md §3.6, issue 20261125-d8b5)
    for key, macro in [
        ("key_a_x_px", "UI_KEY_A_X"),
        ("key_b_x_px", "UI_KEY_B_X"),
        ("key_y_px", "UI_KEY_Y"),
    ]:
        r[key] = int(find(CORE + "ui_kit.h", rf"#define {macro}\s+(\d+)", f"ring.{key}").group(1))
    r["hero_px"] = int(
        find(
            CORE + "ui_theme.h", r"#define FONT_ICON_HERO \(&font_phosphor_(\d+)\)", "ring.hero_px"
        ).group(1)
    )
    for key, macro in [
        ("band_out_px", "BAND_OUT"),
        ("band_w_px", "BAND_W"),
        ("sel_out_px", "SEL_OUT"),
        ("sel_w_px", "SEL_W"),
        ("seg_gap_deg", "SEG_GAP_DEG"),
        ("others_pct", "OTHERS_PCT"),
        ("prog_out_px", "PROG_OUT"),
        ("prog_w_px", "PROG_W"),
        ("work_period_ms", "WORK_PERIOD_MS"),
        ("ask_period_ms", "ASK_PERIOD_MS"),
        ("perm_period_ms", "PERM_PERIOD_MS"),
        ("done_sweep_ms", "DONE_SWEEP_MS"),
        ("error_flicker_ms", "ERROR_FLICKER_MS"),
    ]:
        m = re.search(rf"#define {macro}\s+(\d+)", text(ring))
        if not m and macro == "OTHERS_PCT":
            r[key] = int(find(ring, r"#define REST_PCT\s+(\d+)", f"ring.{key}").group(1))
        else:
            r[key] = int(find(ring, rf"#define {macro}\s+(\d+)", f"ring.{key}").group(1))
    r["max_segments"] = int(
        find(CORE + "ui_ring.h", r"#define RING_MAX_SEGMENTS\s+(\d+)", "ring.max_segments").group(1)
    )
    # shape + motion per kind, from the comments on ring_kind_t
    kinds = {}
    for m in find_all(CORE + "ui_ring.h", r"RING_(\w+),?\s*/\* (.*?) \*/"):
        kinds[m.group(1).lower()] = m.group(2)
    SOURCES["ring.kinds"] = (
        f"{CORE}ui_ring.h:{line_of(CORE + 'ui_ring.h', find(CORE + 'ui_ring.h', r'RING_WORK,').start())}"
    )
    r["kinds"] = kinds
    find(home, r"Nothing else needs you", "ring.nothing_else")
    return r


# The gesture map is one table in nav.c (interaction layer). Each nav action has a hand-written
# description here; a new action in the source without one, or a stale one here, fails the check.
# The page shows these to readers as "what each gesture does", so they use the words of the page
# (the ring, the request screen, the agent menu, Status, Scroll, the terminal), not mode or G_* names.
ACTION_TEXT = {
    "NAV_HOLD_PRESS": "Starts a hold on the selected item, if it needs one",
    "NAV_HOLD_RELEASE": "Lifting the finger cancels a hold",
    "NAV_FOCUS": "Brings the session's terminal to the front on the PC; on the request screen it never approves",
    "NAV_OPEN_AGENT": "Opens the agent menu for the selected session",
    "NAV_OPEN_ASK": "Opens the request screen. Opening answers nothing",
    "NAV_OPEN_STATUS": "Opens Status: link, battery, quiet mode, sounds",
    "NAV_PICK_RIM": "Selects the session under your finger",
    "NAV_HOLD_LONG": "Holding still away from the items goes back",
    "NAV_SCRUB": "Sliding round the rim moves the selection, one tick per step",
    "NAV_NEXT_ATTENTION": "Jumps to the next session that needs you",
    "NAV_NEXT": "Selects the next session, item or page",
    "NAV_PREV": "Selects the previous session",
    "NAV_COMMIT": "Chooses the selected item. Approve, deny and stop need a hold instead",
    "NAV_BACK": "Goes back one screen. A request stays open: going back never denies",
    "NAV_HOME": "Goes straight back to the ring",
    "NAV_SCROLL_DRAG": "Moves the list with your finger; in Scroll, each 10 px is one line of the terminal",
    "NAV_SCROLL_FLING": "A quick flick keeps the terminal scrolling, slowing down like a trackpad",
    "NAV_SCROLL_CATCH": "A new touch stops the scrolling",
    "NAV_PAGE_NEWER": "Scrolls one page toward newer output",
    "NAV_TO_BOTTOM": "Jumps to the newest output and leaves Scroll",
    "NAV_ERASE": "Deletes the last typed word",
    "NAV_OPEN_LABS": "Opens Experiments; the first is the Trackpad",
    "NAV_POINTER": "Moves the PC's pointer; tap clicks, hold right-clicks",
    "NAV_TOY_KEY": "In a toy, a key acts the moment it goes down or up",
}
WHEN_TEXT = {
    "WHEN_ALWAYS": "",
    "WHEN_REQUEST": "when a request waits for approval",
    "WHEN_NO_REQUEST": "when no request waits for approval",
    "WHEN_ASKABLE": "when the session waits for your answer",
    "WHEN_NOT_ASKABLE": "when nothing waits for you",
}


def extract_gesture_map():
    rel = CORE + "nav.c"
    m = find(rel, r"const nav_binding_t nav_bindings\[\] = \{(.*?)\n\};", None, re.S)
    base = line_of(rel, m.start(1))
    cases, seen = [], set()
    for i, ln in enumerate(m.group(1).split("\n")):
        row = re.search(r"\{MODE_(\w+), (G_\w+), ZONE_(\w+), (WHEN_\w+), (NAV_\w+)\}", ln)
        if not row:
            continue
        mode, g, zone, when, act = row.groups()
        if act not in ACTION_TEXT:
            raise Drift(f"{rel}: action {act} has no description in extract.py")
        seen.add(act)
        cases.append(
            {
                "gesture": g,
                "mode": mode.lower(),
                "zone": zone.lower(),
                "when": WHEN_TEXT[when],
                "nav": act,
                "action": ACTION_TEXT[act],
                "source": f"{rel}:{base + i}",
            }
        )
    if not cases:
        raise Drift(f"no gesture bindings in {rel}")
    stale = sorted(set(ACTION_TEXT) - seen)
    if stale:
        raise Drift(f"extract.py describes nav actions no longer in the source: {stale}")
    find(CORE + "app.c", r"if \(power_activity\(app_now_ms\(\)\)\) \{", "gesture_map.wake_swallow")
    find(CORE + "app.c", r'case G_CHORD: dbg_event\("input", "chord"', "gesture_map.chord")
    find(
        CORE + "ui_home.c",
        r"if \(s->state == SESS_DONE\) g_ui_actions->ack\(s->sid\);",
        "gesture_map.done_acks",
    )
    # the deck has no hold binding at all, and a tap on a request only opens its options
    find(
        rel,
        r"\{MODE_HOME, G_TAP, ZONE_CENTRE, WHEN_ASKABLE, NAV_OPEN_ASK\}",
        "gesture_map.tap_never_approves",
    )
    if re.search(r"\{MODE_HOME, G_PRESS,", text(rel)):
        raise Drift(f"{rel}: the deck must not arm holds (UI-V2 §3)")
    if re.search(r"\bNAV_DENY\b", text(rel)):
        raise Drift(f"{rel}: no swipe denies; deny is only its hold (UI-V3 §5)")
    find(
        rel,
        r"\{MODE_HOME, G_SWIPE_DOWN, ZONE_CENTRE, WHEN_ALWAYS, NAV_OPEN_STATUS\}",
        "gesture_map.swipe_down_status",
    )
    # quiet mode holds the arrival buzz and glow, and the nag, in app.c
    find(
        CORE + "app.c",
        r"if \(model_quiet_active\(hal_rtc_get_unix\(\)\)\) \{",
        "gesture_map.quiet_holds_alerts",
    )
    find(CORE + "nav.c", r"bool nav_hold_may_start\(", "gesture_map.hold_bound_to_rid")
    find(
        CORE + "ui_ask.c",
        r"if \(it->hold_ms\) \{ /\* a tap never commits an approve-class or deny item \*/",
        "gesture_map.ask_tap_never_commits_hold",
    )
    find(
        CORE + "ui_ask.c",
        r"if \(a->rid != s_hold_rid \|\| a->answer_sent",
        "gesture_map.ask_hold_cancels_on_change",
    )
    return {
        "cases": cases,
        "chord": "A + B held together: Home from any mode (logged on the deck, no action there)",
        "first_touch_wakes_only": True,
    }


def extract_screens():
    """Debug `screen` values the UI reports."""
    vals = {}
    for rel in (CORE + "ui.c", CORE + "ui_home.c"):
        for m in re.finditer(
            r'"screen\\?":\\?"(\w+)\\?"|"%s", a && !a->answer_sent \? "(\w+)" : "(\w+)"', text(rel)
        ):
            for v in m.groups():
                if v:
                    vals.setdefault(v, f"{rel}:{line_of(rel, m.start())}")
    ag = find(
        CORE + "ui_agent.c", r'snprintf\(r->screen, sizeof r->screen, "agent"\)', "screens.agent"
    )
    vals["agent"] = f"{CORE}ui_agent.c:{line_of(CORE + 'ui_agent.c', ag.start())}"
    ask = find(CORE + "ui_ask.c", r'snprintf\(r->screen, sizeof r->screen, "ask"\)', "screens.ask")
    vals["ask"] = f"{CORE}ui_ask.c:{line_of(CORE + 'ui_ask.c', ask.start())}"
    sc = find(
        CORE + "ui_scroll.c", r'snprintf\(r->screen, sizeof r->screen, "scroll"\)', "screens.scroll"
    )
    vals["scroll"] = f"{CORE}ui_scroll.c:{line_of(CORE + 'ui_scroll.c', sc.start())}"
    stt = find(
        CORE + "ui_status.c", r'snprintf\(r->screen, sizeof r->screen, "status"\)', "screens.status"
    )
    vals["status"] = f"{CORE}ui_status.c:{line_of(CORE + 'ui_status.c', stt.start())}"
    if set(vals) != {"ring", "attention", "agent", "ask", "scroll", "status"}:
        raise Drift(f"unexpected screen values {sorted(vals)}")
    # the preview line is terminal content: it is only drawn when the privacy setting allows it
    find(
        CORE + "ui_scroll.c",
        r"static bool preview_allowed\(void\) \{ return model_settings\(\)->privacy != PRIVACY_META; \}",
        "screens.scroll_preview_privacy",
    )
    SOURCES["screens"] = vals["attention"]
    return [
        {"id": "ring", "title": "Deck", "source": vals["ring"]},
        {"id": "attention", "title": "Deck · a request is waiting", "source": vals["attention"]},
        {"id": "agent", "title": "Agent wheel", "source": vals["agent"]},
        {"id": "ask", "title": "Ask · the request and its options", "source": vals["ask"]},
        {"id": "scroll", "title": "Scroll · the terminal's scrollback", "source": vals["scroll"]},
        {"id": "status", "title": "Status · link, battery, quiet mode", "source": vals["status"]},
    ]


# ───────────────────────── haptics ─────────────────────────
HAPTIC_MEANING = {
    # pattern name → (C array that defines it, meaning)
    "detent": ("detent", "A click each time the rim scrub crosses into another segment"),
    "hold_ramp": ("ramp", "Pulses grow longer and the gaps shorter while the approve hold fills"),
    "deny_ramp": ("deny_ramp", "Pulses shrink and the gaps grow while the deny hold fills"),
    "reply_sent": (
        "sent",
        "An answer went out that neither grants nor denies: short then long",
    ),
    "tick": ("tick", "A light acknowledgement of a button selection"),
    "approved": ("approved", "Approve was sent: one long pulse"),
    "denied": ("denied", "Deny was sent: two medium pulses"),
    "reject": ("no", "A command could not be sent: three short pulses"),
    "hello": ("hello", "Link up: proves the motor path, even in silent mode"),
    "done": ("done", "An agent finished"),
    "error": ("error", "An agent hit an error"),
    "question": ("question", "An agent is asking a question"),
    "permission": ("permission", "An agent needs approval"),
    "scroll_stop": ("stop", "SCROLL reached the bottom or the top of the scrollback: one thud"),
    "quiet_toggled": ("quiet", "Quiet mode turned on, extended or off: two spaced pulses"),
}


def extract_haptics():
    rel = CORE + "feedback.c"
    inp = "firmware/boards/stopwatch/input.cpp"
    duty = int(find(inp, r"#define HAPTIC_DUTY\s+(\d+)", "haptics.duty").group(1))
    kick_duty = int(find(inp, r"#define HAPTIC_KICK_DUTY\s+(\d+)", "haptics.kick_duty").group(1))
    kick_ms = int(find(inp, r"#define HAPTIC_KICK_MS\s+(\d+)", "haptics.kick_ms").group(1))
    find(inp, r"ms >= 2 \* HAPTIC_KICK_MS", "haptics.kick_rule")
    find(inp, r"ms <= HAPTIC_KICK_MS \? HAPTIC_KICK_DUTY : HAPTIC_DUTY", "haptics.kick_only_rule")
    detent_gap = int(
        find(rel, r"#define DETENT_MIN_GAP_MS\s+(\d+)", "haptics.detent_min_gap_ms").group(1)
    )
    patterns, meanings = {}, {}
    for name, (arr, meaning) in HAPTIC_MEANING.items():
        m = find(
            rel, rf"static const uint16_t {arr}\[\]\s*=\s*\{{([\d,\s]+)\}};", f"haptics.{name}"
        )
        vals = [int(v) for v in m.group(1).replace(" ", "").strip(",").split(",") if v]
        steps = []
        for i in range(0, len(vals), 2):
            on = vals[i]
            off = vals[i + 1] if i + 1 < len(vals) else 0
            has_kick = on >= 2 * kick_ms
            # a pulse no longer than the kick is all kick: a crisp click at kick duty
            only_kick = on <= kick_ms
            steps.append(
                {
                    "on_ms": on,
                    "off_ms": off,
                    "duty": kick_duty if only_kick else duty,
                    "kick_ms": kick_ms if has_kick else 0,
                    "kick_duty": kick_duty if has_kick or only_kick else duty,
                }
            )
        patterns[name] = steps
        meanings[name] = meaning
    return {
        "patterns": patterns,
        "meaning": meanings,
        "motor": {
            "hold_duty": duty,
            "kick_duty": kick_duty,
            "kick_ms": kick_ms,
            "kick_min_pulse_ms": 2 * kick_ms,
            "detent_min_gap_ms": detent_gap,
        },
    }


# ───────────────────────── link / usb / ble / display ─────────────────────────
def extract_link():
    return {
        "hello_retry_ms": int(
            find(CORE + "link.c", r"#define HELLO_RETRY_MS\s+(\d+)", "link.hello_retry_ms").group(1)
        ),
        "idle_ping_ms": int(
            find(CORE + "link.c", r"#define IDLE_PING_MS\s+(\d+)", "link.idle_ping_ms").group(1)
        ),
        "ping_interval_ms": 1000
        * int(
            find(
                "daemon/crates/agentlinkd/src/usb.rs",
                r"const PING_EVERY: Duration = Duration::from_secs\((\d+)\)",
                "link.ping_interval_ms",
            ).group(1)
        ),
        "silence_timeout_ms": int(
            find(
                "firmware/boards/stopwatch/link_usb.cpp",
                r"#define SILENCE_MS\s+(\d+)",
                "link.silence_timeout_ms",
            ).group(1)
        ),
    }


def extract_usb():
    usb = "daemon/crates/agentlinkd/src/usb.rs"
    frame = "daemon/crates/agentlinkd/src/usb/frame.rs"
    find(frame, r"let mut crc = 0xFFFFu16", "usb.crc")
    find(frame, r"\(crc << 1\) \^ 0x1021", "usb.crc_poly")
    return {
        "vid": "0x"
        + find(usb, r"const ESP_VID: u16 = 0x([0-9A-Fa-f]{4})", "usb.vid").group(1).upper(),
        "pid": "0x"
        + find(usb, r"const ESP_PID: u16 = 0x([0-9A-Fa-f]{4})", "usb.pid").group(1).upper(),
        "baud": int(find(usb, r"const BAUD: u32 = ([\d_]+)", "usb.baud").group(1).replace("_", "")),
        "crc": "CRC-16/CCITT-FALSE",
        "max_payload": int(
            find(frame, r"pub const MAX_PAYLOAD: usize = (\d+)", "usb.max_payload").group(1)
        ),
        "framing": "0x00 ‖ COBS(payload ‖ crc16_be) ‖ 0x00",
    }


def extract_ble():
    ble = "daemon/crates/agentlinkd/src/ble.rs"

    def uuid(const, key):
        h = (
            find(ble, rf"const {const}: Uuid = Uuid::from_u128\(0x([0-9a-f_]+)\)", key)
            .group(1)
            .replace("_", "")
        )
        return f"{h[:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:]}"

    return {
        "service": uuid("SERVICE_UUID", "ble.service"),
        "rx": uuid("RX_UUID", "ble.rx"),
        "tx": uuid("TX_UUID", "ble.tx"),
        "name_prefix": find(ble, r'name\.starts_with\("([^"]+)"\)', "ble.name_prefix").group(1),
        "default_write_len": int(
            find(ble, r"const DEFAULT_WRITE_LEN: usize = (\d+)", "ble.default_write_len").group(1)
        ),
        "max_write_len": int(
            find(ble, r"const MAX_WRITE_LEN: usize = (\d+)", "ble.max_write_len").group(1)
        ),
    }


def extract_display():
    p = "firmware/boards/stopwatch/board_priv.h"
    find("firmware/boards/stopwatch/display.cpp", r"466x466 round AMOLED", "display.shape")
    return {
        "width": int(find(p, r"#define SW_PANEL_W\s+(\d+)", "display.width").group(1)),
        "height": int(find(p, r"#define SW_PANEL_H\s+(\d+)", "display.height").group(1)),
        "shape": "round",
    }


# ───────────────────────── sounds ─────────────────────────
def extract_sounds():
    hdr, src = CORE + "sound_bank.h", CORE + "sound_bank.c"
    waves = [
        w.lower()
        for w in re.findall(
            r"SND_(\w+)", find(hdr, r"typedef enum \{([^}]*)\} snd_wave_t", "sounds.waves").group(1)
        )
    ]
    rate = int(find(hdr, r"#define SND_RATE\s+(\d+)", "sounds.rate").group(1))
    body = text(src)
    roles = [
        {
            "id": m.group(1),
            "when": m.group(2),
            "pick": m.group(3),
            "repeat": max(1, int(m.group(4))),
            "gap_ms": int(m.group(5)),
        }
        for m in re.finditer(
            r'\{"(\w+)",\s*"([^"]+)",\s*"([\w-]+)",\s*(\d+),\s*(\d+)\}',
            body.split("snd_roles[] = {", 1)[1].split("};", 1)[0],
        )
    ]
    if not roles:
        raise Drift(f"{src}: no snd_roles")
    step_re = r"S\((\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\)"
    arrays = {}
    for m in re.finditer(r"static const snd_step_t (\w+)\[\] = \{(.*?)\};", body, re.S):
        arrays[m.group(1)] = [
            {
                "wave": w.lower(),
                "note": int(n),
                "slide": int(sl),
                "vol": int(v),
                "ms": int(ms),
                "decay": int(d),
            }
            for w, n, sl, v, ms, d in re.findall(step_re, m.group(2))
        ]
    sounds = []
    bank_re = r'\{"([\w-]+)",\s*"(\w+)",\s*"([^"]+)",\s*\{([^{}]*)\},\s*(\d+),\s*(\d+)\}'
    for m in re.finditer(bank_re, body):
        sid, role, name, trk, echo_ms, echo_pct = m.groups()
        names = re.findall(r"T\((\w+)\)", trk)
        if not names or any(a not in arrays for a in names) or role not in {r["id"] for r in roles}:
            raise Drift(f"{src}: sound {sid} refers to unknown steps or role")
        SOURCES[f"sounds.{sid}"] = (
            f"{src}:{line_of(src, body.index('static const snd_step_t ' + names[0]))}"
        )
        sounds.append(
            {
                "id": sid,
                "role": role,
                "name": name,
                "tracks": [arrays[a] for a in names],
                "echo": {"ms": int(echo_ms), "pct": int(echo_pct)},
            }
        )
    if not sounds:
        raise Drift(f"{src}: no snd_bank entries")
    for r in roles:
        if not any(x["id"] == r["pick"] and x["role"] == r["id"] for x in sounds):
            raise Drift(f"{src}: role {r['id']} picks unknown sound {r['pick']}")
    return {"rate": rate, "waves": waves, "roles": roles, "bank": sounds}


# ───────────────────────── glyphs ─────────────────────────
# FONT_ICON_L: the largest icon font that has every glyph the Atlas's glyph key shows
GLYPH_FONT = "font_phosphor_44"


def switch_returns(rel: str, signature: str, enum: str, ret: str) -> tuple[dict[str, str], int]:
    """Map `case ENUM_X: return RET;` lines of one C function to {x: RET}, `default` as `*`."""
    m = find(rel, signature + r"\s*\{(.*?)\n\}", None, re.S)
    out = {}
    for c in re.finditer(rf"case {enum}_(\w+): return ({ret});", m.group(1)):
        out[c.group(1).lower()] = c.group(2)
    d = re.search(rf"default: return ({ret});", m.group(1))
    if not out or not d:
        raise Drift(f"{rel}: no case/default returns in {signature}")
    out["*"] = d.group(1)
    return out, line_of(rel, m.start(1))


def icon_codepoints() -> dict[str, int]:
    """Map each single-glyph ICON_* of ui_icons.h to its code point."""
    rel = CORE + "ui_icons.h"
    out = {}
    for m in find_all(rel, r'#define (ICON_\w+) "((?:\\x[0-9A-Fa-f]{2})+)"'):
        raw = bytes(int(h, 16) for h in re.findall(r"\\x([0-9A-Fa-f]{2})", m.group(2)))
        s = raw.decode("utf-8")
        if len(s) == 1:
            out[m.group(1)] = ord(s)
            SOURCES[f"glyphs.icons.{m.group(1)}"] = f"{rel}:{line_of(rel, m.start())}"
    return out


def font_glyphs(name: str) -> tuple[int, int, dict[int, dict[str, object]]]:
    """Read an lv_font_conv C font (4 bpp, uncompressed, sparse cmaps).

    Returns the line height, the base line and {code point: glyph}; a glyph's `a` is one hex
    digit (alpha 0-15) per pixel, row by row.
    """
    rel = f"{CORE}fonts/{name}.c"
    find(rel, r"\* Bpp: 4\n", "glyphs.font")
    if "--no-compress" not in text(rel):
        raise Drift(f"{rel}: compressed bitmaps are not read")
    bm = find(rel, r"glyph_bitmap\[\] = \{(.*?)\n\};", None, re.S).group(1)
    data = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-f]+)", re.sub(r"/\*.*?\*/", "", bm)))
    dsc = find_all(
        rel,
        r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), "
        r"\.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}",
    )
    glyphs: dict[int, dict[str, object]] = {}
    for cm in find_all(
        rel,
        r"\.range_start = (\d+), \.range_length = \d+, \.glyph_id_start = (\d+),\s*"
        r"\.unicode_list = (\w+), \.glyph_id_ofs_list = NULL, \.list_length = (\d+), "
        r"\.type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY",
    ):
        lst = find(rel, rf"{cm.group(3)}\[\] = \{{(.*?)\}};", None, re.S).group(1)
        offs = [int(v, 16) for v in re.findall(r"0x([0-9a-f]+)", lst)]
        if len(offs) != int(cm.group(4)):
            raise Drift(f"{rel}: {cm.group(3)} has {len(offs)} entries, not {cm.group(4)}")
        for i, off in enumerate(offs):
            # dsc[0] is the reserved id 0, so glyph id k is dsc[k]
            idx, adv, w, h, ox, oy = (int(g) for g in dsc[int(cm.group(2)) + i].groups())
            n = w * h  # pixels, two to a byte, rows not padded
            glyphs[int(cm.group(1)) + off] = {
                "w": w,
                "h": h,
                "x": ox,
                "y": oy,
                "adv": adv // 16,
                "a": data[idx : idx + (n + 1) // 2].hex()[:n],
            }
    lh = int(find(rel, r"\.line_height = (\d+),").group(1))
    bl = int(find(rel, r"\.base_line = (\d+),").group(1))
    return lh, bl, glyphs


def extract_glyphs() -> dict[str, object]:
    """Return the glyphs of the Atlas's glyph key as the firmware's own bitmaps (GLYPHS.md §3.2)."""
    theme, card, kit = CORE + "ui_theme.c", CORE + "ui_home_card.c", CORE + "ui_kit.h"
    st, line = switch_returns(
        theme, r"const char \*theme_state_glyph\(const session_t \*s\)", "SESS", r"ICON_\w+"
    )
    SOURCES["glyphs.states"] = f"{theme}:{line}"
    inp = find(
        theme,
        r"case SESS_INPUT: return s->sub == 0 \? (ICON_\w+) : (ICON_\w+);",
        "glyphs.states.input",
    )
    states: dict[str, object] = dict(st)
    states["input"] = {"0": inp.group(1), "1": inp.group(2)}
    ag, line = switch_returns(
        theme, r"const char \*theme_agent_glyph\(agent_t a\)", "AGENT", r"ICON_\w+"
    )
    SOURCES["glyphs.agents"] = f"{theme}:{line}"
    names, line = switch_returns(
        theme, r"const char \*theme_agent_name\(agent_t a\)", "AGENT", r'"[^"]+"'
    )
    SOURCES["glyphs.agents.name"] = f"{theme}:{line}"
    agents = []
    for m in find_all(CORE + "model.h", r"AGENT_(\w+) = \d+"):
        a = m.group(1).lower()
        name = names.get(a, names["*"]).strip('"')
        agents.append({"id": a, "name": name, "icon": ag.get(a, ag["*"])})
    # the deck's key hints (ui_home_card.c): A is set to a glyph, B is its word's glyph (kGlyphs)
    words = find(card, r'static const char \*const words\[2\] = \{"(\w+)", "(\w+)"\};')
    a_icon = find(card, r"ui_key_hint_set\(s_badge\[0\], (ICON_\w+)\);", "glyphs.keys.a").group(1)
    b_icon = find(theme, rf'\{{"{words.group(2)}", (ICON_\w+)\}}', "glyphs.keys.b").group(1)
    keys: dict[str, object] = {}
    for k, word, icon in (("a", words.group(1), a_icon), ("b", words.group(2), b_icon)):
        rgb = find(
            kit, rf"#define RGB_KEY_{k.upper()} 0x([0-9a-fA-F]{{6}})", f"glyphs.keys.{k}.color"
        )
        keys[k] = {"word": word, "icon": icon, "color": "#" + rgb.group(1).lower()}
    keys["r_px"] = int(find(kit, r"#define UI_DISC_R_KEY\s+(\d+)", "glyphs.keys.r_px").group(1))
    keys["ring_w_px"] = int(
        find(CORE + "ui_kit.c", r"#define KEY_RING_W (\d+)", "glyphs.keys.ring_w_px").group(1)
    )
    find(CORE + "ui_theme.h", rf"#define FONT_ICON_L \(&{GLYPH_FONT}\)", "glyphs.px")
    used = sorted(
        set(st.values())
        | {inp.group(1), inp.group(2), a_icon, b_icon}
        | {a["icon"] for a in agents}
    )
    cps = icon_codepoints()
    lh, bl, bitmaps = font_glyphs(GLYPH_FONT)
    icons = {}
    for name in used:
        if name not in cps:
            raise Drift(f"{name} is not a single-glyph ICON_* in ui_icons.h")
        g = bitmaps.get(cps[name])
        if g is None:
            raise Drift(f"{GLYPH_FONT} has no glyph for {name} (U+{cps[name]:04X})")
        icons[name] = {"cp": f"{cps[name]:04x}", **g}
    return {
        "font": GLYPH_FONT,
        "px": int(GLYPH_FONT.rsplit("_", 1)[1]),
        "line_height": lh,
        "base_line": bl,
        "states": states,
        "keys": keys,
        "agents": agents,
        "icons": icons,
    }


# ───────────────────────── output ─────────────────────────
def build():
    SOURCES.clear()
    pal = extract_palette()
    facts = {
        "generated_from": "worktree",
        "palette": pal,
        "states": extract_states(pal),
        "gesture": extract_gesture(),
        "ring": extract_ring(),
        "gesture_map": extract_gesture_map(),
        "screens": extract_screens(),
        "haptics": None,
        "link": extract_link(),
        "usb": extract_usb(),
        "ble": extract_ble(),
        "display": extract_display(),
        "messages": extract_messages(),
        "sounds": extract_sounds(),
        "glyphs": extract_glyphs(),
    }
    h = extract_haptics()
    facts["haptics"] = h["patterns"]
    facts["haptic_meaning"] = h["meaning"]
    facts["haptic_motor"] = h["motor"]
    facts["sources"] = dict(sorted(SOURCES.items()))
    return facts


def css(facts):
    lines = [
        "/* GENERATED by atlas/tools/extract.py from firmware sources. Do not edit. */",
        ":root {",
    ]
    for k, v in facts["palette"].items():
        lines.append(f"  --fw-{k}: {v};")
    for k, s in facts["states"].items():
        lines.append(f"  --fw-state-{k}: {s['color']};")
    for k, v in facts["gesture"].items():
        unit = (
            "px"
            if k.endswith("_px")
            else "ms"
            if k.endswith("_ms")
            else "deg"
            if k.endswith("_deg")
            else ""
        )
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
        bad = [
            p
            for p, t in ((OUT_JSON, j), (OUT_CSS, c))
            if not p.exists() or p.read_text(encoding="utf-8") != t
        ]
        if bad:
            print(
                "extract.py: generated files are stale: "
                + ", ".join(str(p.relative_to(ROOT)) for p in bad)
                + "\nrun: python3 atlas/tools/extract.py",
                file=sys.stderr,
            )
            return 1
        print("atlas facts up to date")
        return 0
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_CSS.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(j, encoding="utf-8")
    OUT_CSS.write_text(c, encoding="utf-8")
    print(
        f"wrote {OUT_JSON.relative_to(ROOT)} ({len(SOURCES)} sourced facts) and {OUT_CSS.relative_to(ROOT)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
