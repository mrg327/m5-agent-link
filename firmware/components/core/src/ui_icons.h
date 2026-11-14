/* Phosphor Fill icons as UTF-8 strings (UI-V3 §2.2). The icon fonts (font_phosphor_20/28/44) are the
 * fallback of the text fonts, so an icon can sit in any label next to text. Add a code point to
 * firmware/tools/fonts/gen.mjs and regenerate before using a new one. */
#pragma once

#define ICON_WORKING "\xEE\x82\x94"    /* arrows-clockwise e094 */
#define ICON_QUESTION "\xEE\x85\xAC"   /* chat-circle-dots e16c */
#define ICON_PERMISSION "\xEE\x95\xBE" /* hand-palm e57e */
#define ICON_DONE "\xEE\x86\x84"       /* check-circle e184 */
#define ICON_ERROR "\xEE\x93\xA2"      /* warning-circle e4e2 */
#define ICON_IDLE "\xEE\x8C\xB0"       /* moon e330 */
#define ICON_DENIED "\xEE\x93\xB8"     /* x-circle e4f8 */
#define ICON_OFFLINE "\xEE\xAD\x96"    /* plugs eb56: reads as "unplugged" at 20 px */
#define ICON_BLUETOOTH "\xEE\x83\x9A"  /* bluetooth e0da */
#define ICON_USB "\xEE\xA5\x96"        /* usb e956 */
#define ICON_COMPUTER "\xEE\x95\xA0"   /* desktop e560: your computer (link kind unknown) */
#define ICON_BATTERY_FULL "\xEE\x83\x80"     /* battery-full e0c0 */
#define ICON_BATTERY_HIGH "\xEE\x83\x82"     /* battery-high e0c2 */
#define ICON_BATTERY_MED "\xEE\x83\x86"      /* battery-medium e0c6 */
#define ICON_BATTERY_LOW "\xEE\x83\x84"      /* battery-low e0c4 */
#define ICON_BATTERY_WARN "\xEE\x83\x88"     /* battery-warning e0c8 */
#define ICON_BATTERY_CHARGING "\xEE\x82\xBA" /* battery-charging e0ba */
#define ICON_MIC "\xEE\x8C\xA6"        /* microphone e326 */
#define ICON_KEYBOARD "\xEE\x8B\x98"   /* keyboard e2d8 */
#define ICON_BELL "\xEE\x83\x8E"       /* bell e0ce */
#define ICON_QUIET "\xEE\x83\x94"      /* bell-slash e0d4 */
#define ICON_SOUND "\xEE\x91\x8A"      /* speaker-high e44a */
#define ICON_MUTE "\xEE\x91\x9A"       /* speaker-slash e45a */
#define ICON_OPEN "\xEE\x97\x9E"       /* arrow-square-out e5de */
#define ICON_ACTIVITY "\xEE\x80\x80"   /* pulse e000 */
#define ICON_STOP "\xEE\x91\xAE"       /* stop-circle e46e */
#define ICON_REPLY "\xEE\x80\xA4"      /* arrow-bend-up-left e024 */
#define ICON_SENT "\xEE\x8E\x98"       /* paper-plane-tilt e398 */
#define ICON_INFO "\xEE\x8B\x8E"       /* info e2ce */
#define ICON_LIVE "\xEE\x81\x9C"       /* arrow-line-down e05c */
#define ICON_BACKSPACE "\xEE\x82\xAE"  /* backspace e0ae */
#define ICON_HOLD "\xEE\x86\x8A"       /* circle e18a: the hold ring is drawn as an outline, see ui_wheel.c */
#define ICON_GRAPH             "\xEE\x89\xBE" /* git-fork e27e: the AGENT wheel's Graph item */
/* The GRAPH screen's node and lane glyphs (GRAPH.md §4): in font_phosphor_20 only. */
#define ICON_G_DOT             "\xEE\xB3\x9E" /* dot ecde: other */
#define ICON_G_READ            "\xEE\x88\xA0" /* eye e220 */
#define ICON_G_EDIT            "\xEE\x8E\xB4" /* pencil-simple e3b4 */
#define ICON_G_SHELL           "\xEE\xAB\xA8" /* terminal-window eae8 */
#define ICON_G_SEARCH          "\xEE\x8C\x8C" /* magnifying-glass e30c */
#define ICON_G_WEB             "\xEE\x8A\x88" /* globe e288 */
#define ICON_G_ROBOT           "\xEE\x9D\xA2" /* robot e762: agent; general-purpose and unknown lanes */
#define ICON_G_PLAN            "\xEE\xAB\x9C" /* list-checks eadc */
#define ICON_G_GIT             "\xEE\x89\xBA" /* git-commit e27a */
#define ICON_G_TEST            "\xEE\x9E\x9E" /* flask e79e */
#define ICON_G_MCP             "\xEE\xA5\x86" /* plug e946 */
#define ICON_G_EXPLORE         "\xEE\xA9\xA4" /* binoculars ea64 */
#define ICON_G_TASK            "\xEE\x8B\x9E" /* lightning e2de */
#define ICON_G_REVIEW          "\xEE\x98\x86" /* seal-check e606 */
#define ICON_G_RESEARCH        "\xEE\x9D\x98" /* books e758 */
#define ICON_G_MAP             "\xEE\x8C\x9A" /* map-trifold e31a: the plan lane */

/* The glyph pass (GLYPHS.md): the glyphs that stand where a word used to. The graph glyphs above (ICON_G_*) are
 * in every size now, so a screen may reuse them (terminal, robot, eye, pencil, flask, globe). */
#define ICON_AG_CLAUDE "\xEE\x9A\xA2" /* sparkle e6a2 */
#define ICON_AG_COPILOT "\xEE\x97\x96" /* airplane-tilt e5d6 */
#define ICON_AG_CLOUD "\xEE\x86\xAA" /* cloud e1aa */
#define ICON_NEXT "\xEE\x84\xBA" /* caret-right e13a */
#define ICON_BACK "\xEE\x81\x98" /* arrow-left e058 */
#define ICON_CHECK "\xEE\x86\x82" /* check e182 */
#define ICON_X "\xEE\x93\xB6" /* x e4f6 */
#define ICON_MORE "\xEE\x88\x84" /* dots-three-outline e204 */
#define ICON_ALWAYS "\xEE\x94\xBA" /* checks e53a */
#define ICON_NOTIFY "\xEE\x97\xA8" /* bell-ringing e5e8 */
#define ICON_RERUN "\xEE\x80\xB6" /* arrow-clockwise e036 */
#define ICON_RETRY "\xEE\x80\xB8" /* arrow-counter-clockwise e038 */
#define ICON_READ "\xEE\x83\xA6" /* book-open e0e6 */
#define ICON_WAIT "\xEE\x8A\xB6" /* hourglass-low e2b6 */
#define ICON_UNSNOOZE "\xEE\x91\xB2" /* sun e472 */
#define ICON_EYE_HALF "\xEE\x88\xA2" /* eye-closed e222 */
#define ICON_EYE_OFF "\xEE\x88\xA4" /* eye-slash e224 */
#define ICON_SESSIONS "\xEE\x91\xA6" /* stack e466 */
#define ICON_WAVE "\xEE\xA0\x82" /* waveform e802 */
#define ICON_CLICK "\xEE\x9F\x88" /* cursor-click e7c8 */
#define ICON_RIGHT_CLICK "\xEE\x8C\xB6" /* mouse-right-click e336 */
#define ICON_WHEEL "\xEE\x8C\xB2" /* mouse-scroll e332 */
#define ICON_MOVE "\xEE\x82\xA4" /* arrows-out-cardinal e0a4 */
#define ICON_UPDATE "\xEE\x88\x8C" /* download-simple e20c */
#define ICON_WEEK "\xEE\x84\x8A" /* calendar-blank e10a */
#define ICON_SKIP "\xEE\x9A\xA6" /* fast-forward e6a6 */
#define ICON_BYPASS "\xEE\x90\x90" /* shield-slash e410 */
#define ICON_PR "\xEE\x8A\x82" /* git-pull-request e282 */
#define ICON_FILES "\xEE\x9C\x90" /* files e710 */
#define ICON_NO_FOLDER "\xEE\xA3\xB8" /* folder-dashed e8f8 */
#define ICON_REFUSED "\xEE\x8F\x9E" /* prohibit e3de */
#define ICON_WINDOW "\xEE\x97\x9A" /* app-window e5da */
#define ICON_UP "\xEE\x82\x8E" /* arrow-up e08e */
#define ICON_DOWN "\xEE\x80\xBE" /* arrow-down e03e */
#define ICON_LINK_BREAK "\xEE\x8B\xA4" /* link-break e2e4 */
#define ICON_QUEUED "\xEE\x9A\xAC" /* queue e6ac */
#define ICON_TAP "\xEE\xB2\x90" /* hand-tap ec90 */
#define ICON_FORWARD "\xEE\x81\xAC" /* arrow-right e06c */
#define ICON_CARET_DOWN "\xEE\x84\xB6" /* caret-down e136 */
