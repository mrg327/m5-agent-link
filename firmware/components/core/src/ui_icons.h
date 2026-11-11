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
