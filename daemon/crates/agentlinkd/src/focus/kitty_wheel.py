"""agentlinkd's scroll wheel for kitty (UI-V2 §6.2). Written by the daemon; edits are overwritten.

Run with `kitten @ kitten --match <window> agentlink_wheel.py UNIT DELTA END_NOTCHES PREVIEW`.
It turns the watch into the window's mouse wheel, doing what kitty does for a physical wheel
(kitty/mouse.c scroll_event): a program that tracks the mouse gets wheel button events, the main
screen scrolls its history, and the alternate screen gets up/down keys. One JSON line comes back
with where the window ended up, so a scroll batch is a single round trip.

Lines are paced, like a trackpad's smooth scroll: an idle wheel moves the first line at once and
spreads the rest of the batch over the time until the watch's next batch is due (estimated from
the gap between batches), one kitty timer per window. The pacing state lives on `boss` because
kitty re-executes this file on every call.

UNIT is `lines`, `pages` or `end`; DELTA < 0 is up (older); END_NOTCHES is how many wheel lines
the daemon knows it scrolled a program up by (used for `end` off the main screen); PREVIEW is 1
when the device's privacy allows one line of terminal text.
"""

import json
import math
import time

from kittens.tui.handler import result_handler

_STATE = '_agentlink_wheel_v1'
_TICK = 0.008  # s: the finest pacing step, about one frame at 120 Hz
_GAP = 0.04  # s: the watch's batch window, used when the wheel was idle
_GAP_MIN, _GAP_MAX, _IDLE = 0.015, 0.08, 0.15


def main(args):
    pass


def _visible(screen, y):
    try:
        return str(screen.visual_line(y)).rstrip()
    except Exception:
        return ''


def _preview(screen, main_screen):
    # main screen: the top visible line; a full-screen program: the line the wheel points at
    rows = range(screen.lines) if main_screen else range(screen.lines // 2, -1, -1)
    for y in rows:
        t = _visible(screen, y)
        if t.strip():
            return t
    return ''


def _step(w, up):
    """One wheel line, as kitty would do it for a physical wheel; returns the mode used."""
    from kitty.fast_data_types import PRESS, send_mouse_event

    s = w.screen
    if send_mouse_event(s, s.columns // 2, s.lines // 2, 4 if up else 5, PRESS, 0):
        return 'mouse'
    if s.is_main_linebuf():
        s.scroll(1, up)
        return 'main'
    w.send_key('up' if up else 'down')
    return 'keys'


def _drain(boss, wid, st, k):
    w = boss.window_id_map.get(wid)
    k = min(k, abs(st['pending']))
    if w is None:
        st['pending'] = 0
        return
    up = st['pending'] < 0
    for _ in range(k):
        st['mode'] = _step(w, up)
    st['pending'] += k if up else -k


def _pace(boss, wid, delta):
    """Queue `delta` wheel lines for window `wid` and (re)arm its timer."""
    from kitty.fast_data_types import add_timer, remove_timer

    states = getattr(boss, _STATE, None)
    if states is None:
        states = {}
        setattr(boss, _STATE, states)
    st = states.setdefault(wid, {'pending': 0, 'timer': None, 'last': 0.0, 'mode': None})
    now = time.monotonic()
    since = now - st['last']
    st['last'] = now
    gap = _GAP if since > _IDLE else min(max(since, _GAP_MIN), _GAP_MAX)
    if st['timer'] is not None:
        remove_timer(st['timer'])
        st['timer'] = None
    idle = st['pending'] == 0
    st['pending'] += delta
    if idle and st['pending']:
        _drain(boss, wid, st, 1)  # the first line moves at once
    n = abs(st['pending'])
    if not n:
        return st
    interval = gap / (n + 1)
    per_tick = 1
    if interval < _TICK:
        per_tick, interval = math.ceil(n * _TICK / gap), _TICK

    def tick(timer_id):
        _drain(boss, wid, st, per_tick)
        if not st['pending'] or boss.window_id_map.get(wid) is None:
            remove_timer(timer_id)
            if st['timer'] == timer_id:
                st['timer'] = None

    st['timer'] = add_timer(tick, interval, True)
    return st


def _flush(boss, wid):
    """Move any queued lines now (before a page or a jump to the end)."""
    from kitty.fast_data_types import remove_timer

    st = getattr(boss, _STATE, {}).get(wid)
    if not st:
        return
    if st['timer'] is not None:
        remove_timer(st['timer'])
        st['timer'] = None
    _drain(boss, wid, st, abs(st['pending']))


@result_handler(no_ui=True)
def handle_result(args, answer, target_window_id, boss):
    from kitty.fast_data_types import PRESS, SCROLL_FULL, SCROLL_PAGE, send_mouse_event

    w = boss.window_id_map.get(target_window_id)
    if w is None:
        return json.dumps({'error': 'no window'})
    try:
        unit, delta, end_notches, want_preview = args[1], int(args[2]), int(args[3]), args[4] == '1'
    except (IndexError, ValueError):
        return json.dumps({'error': 'bad arguments'})
    if unit not in ('lines', 'pages', 'end'):
        return json.dumps({'error': 'bad unit'})
    s = w.screen
    main_screen = s.is_main_linebuf()
    up = delta < 0
    x, y = s.columns // 2, s.lines // 2

    def wheel(n):
        sent = 0
        while sent < n and send_mouse_event(s, x, y, 4 if up else 5, PRESS, 0):
            sent += 1
        return sent

    if unit != 'lines':
        _flush(boss, target_window_id)
    mode, moved = ('main' if main_screen else 'alt'), 0
    if unit == 'end' and main_screen:
        s.scroll(SCROLL_FULL, up)
    elif unit == 'end':
        n = 0 if up else min(max(end_notches, 0), 500)
        if n and wheel(1):
            moved, mode = 1 + wheel(n - 1), 'mouse'
        elif n:
            for _ in range(n):
                w.send_key('down')
            moved, mode = n, 'keys'
    elif delta and unit == 'lines':
        n = min(abs(delta), 500)
        st = _pace(boss, target_window_id, -n if up else n)
        mode = st['mode'] or mode
        moved = n if mode != 'main' else 0
        if mode == 'main':
            # where the queued lines will leave the history view
            ahead = s.scrolled_by - st['pending']
            out = {'mode': mode, 'moved': 0, 'scrolled_by': min(max(ahead, 0), s.historybuf.count),
                   'history': s.historybuf.count, 'lines': s.lines}
            if want_preview:
                out['preview'] = _preview(s, main_screen)
            return json.dumps(out)
    elif delta:
        n = min(abs(delta), 20)
        if wheel(1):
            per_page = max(1, (s.lines - 1) // 3)  # a wheel notch moves most programs about 3 lines
            total = n if unit == 'lines' else n * per_page
            moved, mode = 1 + wheel(min(total, 500) - 1), 'mouse'
        elif main_screen:
            for _ in range(n if unit == 'pages' else 1):
                s.scroll(SCROLL_PAGE if unit == 'pages' else n, up)
        else:
            total = min(n if unit == 'lines' else n * max(1, s.lines - 1), 500)
            for _ in range(total):
                w.send_key('up' if up else 'down')
            moved, mode = total, 'keys'
    out = {
        'mode': mode,
        'moved': -moved if up else moved,
        'scrolled_by': s.scrolled_by,
        'history': s.historybuf.count,
        'lines': s.lines,
    }
    if want_preview:
        out['preview'] = _preview(s, main_screen)
    return json.dumps(out)
