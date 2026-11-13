//! Integer-coded enums (CDDL "named enums"). Encoded as plain uints.

use minicbor::{Decode, Encode};
use serde::Serialize;

macro_rules! uint_enum {
    ($(#[$m:meta])* $name:ident { $($var:ident = $n:literal),+ $(,)? }) => {
        $(#[$m])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Encode, Decode)]
        #[serde(rename_all = "snake_case")]
        #[cbor(index_only)]
        pub enum $name { $(#[n($n)] $var),+ }
    };
}

uint_enum!(DeviceClass { Watch = 0, Desk = 1, Pad = 2, Sim = 3, Other = 15 });
uint_enum!(InputKind { Touch = 0, Btn = 1, Rotary = 2, Imu = 3 });
uint_enum!(Feature { Focus = 0, Ack = 1, Approve = 2, Reply = 3, Voice = 4, Badge = 5, Debug = 6, Scroll = 7, Keys = 8 });
uint_enum!(Privacy { Meta = 0, Preview = 1, Full = 2 });
uint_enum!(AlertMode { Full = 0, Quiet = 1, Silent = 2 });
uint_enum!(Agent { Copilot = 0, Claude = 1, Cloud = 2, Shell = 3, Other = 15 });
uint_enum!(SessState { Start = 0, Work = 1, Input = 2, Error = 3, Done = 4, Idle = 5, Ended = 6 });
uint_enum!(InputSub { Approval = 0, Question = 1 });
// The agent's permission mode (proto 0.10, GAUGES.md); Claude Code's names in `from_claude`.
uint_enum!(PermMode { Default = 0, Plan = 1, AcceptEdits = 2, Auto = 3, DontAsk = 4, Bypass = 5 });
// Review state of a session's open pull request (proto 0.10).
uint_enum!(PrState { None = 0, Pending = 1, Approved = 2, Changes = 3, Draft = 4 });

impl PermMode {
    /// Claude Code's `permission_mode` value (hook payloads); `None` for a name this build does not know.
    pub fn from_claude(s: &str) -> Option<Self> {
        Some(match s {
            "default" | "manual" => PermMode::Default,
            "plan" => PermMode::Plan,
            "acceptEdits" => PermMode::AcceptEdits,
            "auto" => PermMode::Auto,
            "dontAsk" => PermMode::DontAsk,
            "bypassPermissions" => PermMode::Bypass,
            _ => return None,
        })
    }
}

impl PrState {
    /// The status line's `pr.review_state`; absent or unknown is `None` (no state reported).
    pub fn from_claude(s: Option<&str>) -> Self {
        match s {
            Some("pending") => PrState::Pending,
            Some("approved") => PrState::Approved,
            Some("changes_requested") => PrState::Changes,
            Some("draft") => PrState::Draft,
            _ => PrState::None,
        }
    }
}
uint_enum!(AttKind { Approval = 0, Question = 1, Error = 2, Done = 3 });
uint_enum!(Action { Approve = 0, Deny = 1, Focus = 2, Reply = 3, Ack = 4, Dictate = 5 });
uint_enum!(CmdOp {
    Focus = 0, Ack = 1, Snooze = 2, Approve = 3, Deny = 4, Reply = 5, Cancel = 6,
    Detail = 7, NotifyDone = 8, Quiet = 9, Scroll = 10, Type = 11, Dictate = 12,
    KbPick = 13, KbDel = 14, KbSend = 15, KbEnd = 16, Rerun = 17, Unqueue = 18, Answer = 19,
});
uint_enum!(ScrollUnit { Lines = 0, Pages = 1, ToEnd = 2 });
uint_enum!(ResultErr {
    Stale = 0, Denied = 1, Unsupported = 2, NoWindow = 3, Timeout = 4, Internal = 5,
});
uint_enum!(DispShape { Round = 0, Rect = 1 });
uint_enum!(HostOs { Mac = 0, Win = 1, Linux = 2 });
// Where a standing rule from the watch is saved (proto 0.13, STANDING-RULES.md): Claude Code's `session`
// and `localSettings` destinations. Wider ones (`projectSettings`, `userSettings`) are never offered.
uint_enum!(RuleScope { Session = 0, Project = 1 });
uint_enum!(ClearWhy { AnsweredPc = 0, AnsweredDev = 1, Expired = 2, Cancelled = 3, HandedBack = 4 });
uint_enum!(LogLevel { Err = 0, Warn = 1, Info = 2, Debug = 3 });
// Firmware update (proto 0.15): the device's answer to fw_begin / fw_chunk.
uint_enum!(FwStatus { Go = 0, Done = 1, Busy = 2, Bad = 3, Fail = 4, Stopped = 5 });
uint_enum!(AudioCodec { Adpcm16k = 0, Opus16k = 1 });
uint_enum!(InputEvent {
    Tap = 0, DoubleTap = 1, LongPress = 2, SwipeUp = 3, SwipeDown = 4, SwipeLeft = 5,
    SwipeRight = 6, BtnA = 7, BtnB = 8, BtnALong = 9, BtnBLong = 10, Lift = 11, Shake = 12,
});
