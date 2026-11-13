# Experiments (LABS) and the Trackpad

Issue 61ce. Experiments are features that sit outside the agent flow. They may change or go away.

## Entry

- A swipe up on HOME (a centre start) opens **LABS**, a wheel titled "Experiments". The swipe used to open
  AGENT; tap centre still does that (UI-V2 decision 35).
- In LABS, scrub or B selects; tap, double tap or A opens the selected item.
- To go back to HOME: a long swipe down or a swipe right, B long, the A+B chord, or 8 s idle
  (`NAV_LABS_TIMEOUT_MS`).
- An item that cannot run is still listed. Its description turns amber, the A badge hides, and opening it shows a
  toast and the reject haptic. Trackpad is blocked while offline, or when the daemon's welcome has no `pointer`
  (an older daemon or no backend).

## Trackpad

The watch drives the pointer of the machine `agentlinkd` runs on (protocol §6g, `cmd_op_pointer` = 20).

| Input | Result |
|---|---|
| Drag starting in the centre | moves the pointer |
| Tap | left click; a second tap within 400 ms and 40 px of the last one is sent with `n` = 2 (a double click), up to 3 |
| Hold still 600 ms | right click |
| Turn on the bezel (scrub) | scrolls: clockwise = down, one notch per 15° (24 sectors), with a detent haptic |
| A, B long | back to LABS |
| A+B chord | HOME |

- **Moves.** The gesture recognizer reports `G_MOVE` per touch sample once a centre-start press leaves the 18 px
  tap slop, and only while the trackpad has turned moves on (`gesture_set_moves`). Each step gets a gain of
  1.2 + 0.08 per px of the step, capped at 4.0 (`nav_pointer_gain10`), so slow drags are precise and flicks
  travel. Tenths are carried between steps. Moves are coalesced and sent every 30 ms
  (`NAV_POINTER_COALESCE_MS`).
- **Clicks.** The recognizer has no double tap here: a tap fires on release, so the watch counts double clicks
  itself and every click is sent at once.
- **Swipes** are ignored on the trackpad, so a fast drag never leaves it.
- The link is held interactive while the trackpad is open (`hal_link_interactive`).
- **Failures.** The daemon answers a failure at most once a second. The hint shows its message (on macOS, "Allow
  agentlinkd in Privacy & Security > Accessibility"), with a toast and the reject haptic.
- **Arrivals.** An arriving attention still takes the watch to HOME, as on every other screen.

## Daemon

`daemon/crates/agentlinkd/src/pointer.rs` has three backends:

- **macOS:** CoreGraphics events. Needs the Accessibility permission, which may have to be granted again after
  `launchd.sh install` re-signs the binary.
- **Windows:** `SendInput`.
- **Linux on X11:** `xdotool`. There is no backend under Wayland.

`welcome.pointer` (key 7) is set only when a backend exists.

## Not yet

- Drag (click and hold to drag) is deferred.
- There is no horizontal scroll.
- The pointer is always that of the daemon's own machine, not of a remote session's host.

## Tests

- `simulator/scripts/labs_trackpad.txt` covers entry, the blocked item, clicks, double click, right click, a
  move, a scroll and the back paths.
- `simulator/tests/test_gesture.c` covers `G_MOVE`.
- Protocol vectors: `cmd_pointer_*.json` and `welcome_pointer.json`.
