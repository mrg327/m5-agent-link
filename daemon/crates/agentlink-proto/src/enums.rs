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
uint_enum!(Feature { Focus = 0, Ack = 1, Approve = 2, Reply = 3, Voice = 4, Badge = 5, Debug = 6 });
uint_enum!(Privacy { Meta = 0, Preview = 1, Full = 2 });
uint_enum!(AlertMode { Full = 0, Quiet = 1, Silent = 2 });
uint_enum!(Agent { Copilot = 0, Claude = 1, Cloud = 2, Other = 15 });
uint_enum!(SessState { Start = 0, Work = 1, Input = 2, Error = 3, Done = 4, Idle = 5, Ended = 6 });
uint_enum!(InputSub { Approval = 0, Question = 1 });
uint_enum!(AttKind { Approval = 0, Question = 1, Error = 2, Done = 3 });
uint_enum!(Action { Approve = 0, Deny = 1, Focus = 2, Reply = 3, Ack = 4 });
uint_enum!(CmdOp {
    Focus = 0, Ack = 1, Snooze = 2, Approve = 3, Deny = 4, Reply = 5, Cancel = 6,
    Detail = 7, NotifyDone = 8, Quiet = 9,
});
uint_enum!(ResultErr {
    Stale = 0, Denied = 1, Unsupported = 2, NoWindow = 3, Timeout = 4, Internal = 5,
});
uint_enum!(DispShape { Round = 0, Rect = 1 });
uint_enum!(HostOs { Mac = 0, Win = 1, Linux = 2 });
uint_enum!(ClearWhy { AnsweredPc = 0, AnsweredDev = 1, Expired = 2, Cancelled = 3 });
uint_enum!(LogLevel { Err = 0, Warn = 1, Info = 2, Debug = 3 });
uint_enum!(AudioCodec { Adpcm16k = 0, Opus16k = 1 });
uint_enum!(InputEvent {
    Tap = 0, DoubleTap = 1, LongPress = 2, SwipeUp = 3, SwipeDown = 4, SwipeLeft = 5,
    SwipeRight = 6, BtnA = 7, BtnB = 8, BtnALong = 9, BtnBLong = 10, Lift = 11, Shake = 12,
});
