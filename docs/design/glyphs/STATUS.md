# Sigil as built: STATUS, LABS, TRACKPAD, the update overlay

Part of [GLYPHS.md](../GLYPHS.md) (§5). What each of these screens draws, where, and what every glyph means.
The words are in `ui_report()` as before (rule 9); renders of every state come from
`simulator/scripts/sigil_status.txt` and the `ota_*.txt` scripts.

## STATUS (`ui_status.c`)

The clock in digits is the subject. The four toggles orbit it on r 132, which puts them on a grid with the key
hints: Quiet at 9 o'clock (above A's hint), Mute at 12, Volume at 3 (above B's hint), About at 6 (on the
hints' row). The selected toggle is the large disc in the accent, as in every orbit.

| On glass | Means | Report |
|---|---|---|
| bell / crossed-out bell | alerts sound / quiet runs | `icons bell …` / `icons bell-slash …`, `> Quiet: off` / `> Quiet until 12:30` |
| arc round the bell | the quiet time left, a whole turn for an hour (or more); it drains | `> Quiet until 12:30` |
| speaker / crossed-out speaker | sounds on / off | `> Sounds: on` / `> Sounds: off`, `icons … speaker-slash …` |
| waveform with five arc steps | the volume, 0 to 5; grey steps while nothing would be heard | `> Volume 3 of 5`, `icons … volume-3 …` |
| info | About | `> 2 sessions · 1 waiting`, `Privacy: preview · 0.1.0` |
| digits | the clock; dashes until it is set | `12:00`, or the computer's name while unset |
| transport glyph and a sigil | the link (USB, Bluetooth) and the computer: the sigil of its host name (rule 2) | `dev-mac` |
| amber transport glyph | connecting | `Connecting…` |
| amber plug and a tally | offline, and for how long (§3.3) | `Offline · 2m` |
| ring round a battery glyph | the battery: the ring is the level, the glyph also says charging; vermillion at 15 % or less | `80%` |
| A's hint | what a press does to the selected toggle: the glyph it will show (crossed-out bell, bell, crossed-out speaker, speaker, waveform) | |
| B's hint, a caret | the next toggle | |

A value arc keeps one radius (36): round the small disc while its toggle rests, inside the rim of the large disc
once it is selected, where it is drawn in the dark ink of the disc's glyph.

About selected: the centre becomes its face. Three glyphs with their counts under them (stack: sessions; hand:
waiting for you; warning: failed; grey and without a number at zero), then the privacy eye (open, half closed,
crossed out) and the firmware version. The version is the screen's only text.

The usage gauges (`ui_gauges.c`) keep their two arcs inside the bezel (they begin at r 180; a selected disc ends
at r 172) and their two rows above the clock (y 139 to 193, between the disc at 12 o'clock and the clock). The
link then moves down beside the battery, and About's face starts under the rows.

Gestures are unchanged: a tap on a disc selects and commits it (also where the disc reaches past the centre
zone), a tap elsewhere and A commit the selected one, the dial and B move the selection, a hold off the discs,
a swipe up or right, B long and the chord leave, 8 s idle closes it. With the clock unset Quiet does not start
and the toast is the sign "clock, cross".

## LABS (`ui_labs.c`)

The atom at the centre is the subject; the experiments orbit it (r 150), one for now: the pointer, Trackpad.
One that cannot start is dim, A's hint (the tick) is gone, and a sign between the key hints says why: the plug
(offline) or "computer, download" (the daemon is too old). A refused tap shows the same sign as a toast.

## TRACKPAD (`ui_trackpad.c`)

The screen is the pad: a large faint pointer in the middle. The how-to is three pictograms, each where its
gesture is made:

- the mouse with its wheel, under an arc along the bezel: turn the bezel to scroll;
- the mouse with its left button: a tap is a click (two are a double click);
- the mouse with its right button inside a partly drawn hold ring: a hold is a right click.

A's hint is the back arrow. When the pad cannot work the pictograms go and a sign stands between the key hints:
the plug (offline), "computer, download" (the daemon has no trackpad), "computer, pointer, cross" (this PC has no
pointer to drive), "computer, refused" (the PC refused; its own message, when it sent one, is in the report).

## The update overlay (`ui_ota.c`, the toasts of `ota.c`)

One hero glyph inside a progress arc (r 112): the download glyph in the working blue while the image arrives;
the amber plug inside a grey arc while the link is down; the working glyph in green inside the closed ring while
it restarts. B's key hint carries the cross and a partly drawn hold ring: hold B to cancel. The version being
installed is small digits under the arc. A percent step repaints only the piece of arc it adds.

The toasts are signs (`ui_toast_sign()` in `ui.c`: words for the report, glyphs and a short content for the
glass): "download, cross" (cancelled), "download, hourglass" (timed out), "download, tick" and the version
(updated). A failed update is the error glyph and a number (rule 8):

| Number | Words (ack, log, report) |
|---|---|
| 11 | Checksum mismatch |
| 12 | Signature not trusted |
| 13 | Not an app image |
| 14 | Built for another chip |
| 15 | Not agentlink firmware |
| 16 | Image incomplete |
| 17 | Image too big |
| 18 | Image too small |
| 19 | Image longer than announced |
| 21 | Too much data (patch) |
| 22 | Bad patch |
| 31 | No spare app slot |
| 32 | Flash error |
| 33 | No update open |
| 34 | Out of memory |
| 35 | Update refused |
| 36 | This watch can't update itself |
| 3, 4 | any other words: the ack's status (3 the update was bad, 4 the flash failed) |

## Added for these screens

- Icons: atom (the experiments), cursor (the pointer), clock, mouse-left-click; atom and cursor also in the hero
  sizes.
- Vocabulary: "Set the clock first" is "clock, cross"; "Update agentlinkd" is "computer, download"; the volume
  is the waveform.
- Primitives: `ui_arc_set_steps()`, `ui_arc_set_geometry()` (GLYPHS.md §3.9); `ui_toast_sign()`.
