"""agentlinkd's scroll wheel for kitty (UI-V2 §6.2). Written by the daemon; edits are overwritten.

Run with `kitten @ kitten --match <window> agentlink_wheel.py UNIT DELTA END_NOTCHES PREVIEW`.
It turns the watch into the window's mouse wheel, doing what kitty does for a physical wheel
(kitty/mouse.c scroll_event): a program that tracks the mouse gets wheel button events, the main
screen scrolls its history, and the alternate screen gets up/down keys. One JSON line comes back
with where the window ended up, so a scroll batch is a single round trip.

UNIT is `lines`, `pages` or `end`; DELTA < 0 is up (older); END_NOTCHES is how many wheel lines
the daemon knows it scrolled a program up by (used for `end` off the main screen); PREVIEW is 1
when the device's privacy allows one line of terminal text.
"""

import json

from kittens.tui.handler import result_handler


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
    elif delta:
        n = min(abs(delta), 500 if unit == 'lines' else 20)
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
