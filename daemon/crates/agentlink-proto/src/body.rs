//! Message bodies, in CDDL order. Keys are scoped per map.

use crate::enums::*;
use crate::hex_ser;
use minicbor::{Decode, Encode};
use serde::{Serialize, Serializer};

macro_rules! body {
    ($(#[$m:meta])* $name:ident { $($(#[$fm:meta])* $f:ident : $t:ty),* $(,)? }) => {
        $(#[$m])*
        #[derive(Debug, Clone, PartialEq, Serialize, Encode, Decode)]
        #[cbor(map)]
        pub struct $name { $($(#[$fm])* pub $f: $t,)* }
    };
}

/// `[major, minor]`
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Encode, Decode)]
#[cbor(array)]
pub struct Version(#[n(0)] pub u64, #[n(1)] pub u64);

body!(
    /// Empty body (`ping`, `pong`, `dbg_*_req`).
    Empty {}
);

// ── handshake ──
body!(Hello {
    #[n(0)] proto: Version,
    #[n(1)] dev: DeviceInfo,
    #[n(2)] caps: Caps,
    #[n(3)] #[serde(skip_serializing_if = "Option::is_none")] batt_pct: Option<u64>,
});
body!(DeviceInfo {
    #[n(0)]
    class: DeviceClass,
    #[n(1)]
    board: String,
    #[n(2)]
    fw: String,
    #[n(3)]
    #[cbor(with = "minicbor::bytes")]
    #[serde(serialize_with = "hex_ser")]
    uid: [u8; 8],
});
body!(Caps {
    #[n(0)] #[serde(skip_serializing_if = "Option::is_none")] disp: Option<DisplayCaps>,
    #[n(1)] input: Vec<InputKind>,
    #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] nbtn: Option<u64>,
    #[n(3)] haptic: bool,
    #[n(4)] spk: bool,
    #[n(5)] mic: bool,
    #[n(6)] batt: bool,
    #[n(7)] max_msg: u64,
    #[n(8)] feats: Vec<Feature>,
});
body!(DisplayCaps {
    #[n(0)]
    shape: DispShape,
    #[n(1)]
    w: u64,
    #[n(2)]
    h: u64,
    #[n(3)]
    color: bool,
});
body!(Welcome {
    #[n(0)]
    proto: Version,
    #[n(1)]
    host: HostInfo,
    #[n(2)]
    time: TimeSync,
    #[n(3)]
    cfg: DeviceCfg,
    /// Quick replies (proto 0.4): present means this daemon types into terminals
    /// (`CmdOp::Type` with an index into this list, and `CmdOp::Cancel`).
    #[n(4)]
    #[serde(skip_serializing_if = "Option::is_none")]
    replies: Option<Vec<String>>,
    /// Voice dictation (proto 0.6): this daemon transcribes `audio_*` into a `transcript`.
    #[n(5)]
    #[serde(skip_serializing_if = "Option::is_none")]
    voice: Option<bool>,
});
body!(HostInfo {
    #[n(0)]
    #[cbor(with = "minicbor::bytes")]
    #[serde(serialize_with = "hex_ser")]
    id: [u8; 8],
    #[n(1)]
    name: String,
    #[n(2)]
    os: HostOs,
});
body!(TimeSync {
    #[n(0)]
    unix: u64,
    #[n(1)]
    tz_off: i64,
});
body!(
    /// Also used as a partial update (`cfg` message).
    DeviceCfg {
        #[n(0)] #[serde(skip_serializing_if = "Option::is_none")] privacy: Option<Privacy>,
        #[n(1)] #[serde(skip_serializing_if = "Option::is_none")] alert: Option<AlertMode>,
        #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] quiet_until: Option<u64>,
        #[n(3)] #[serde(skip_serializing_if = "Option::is_none")] brightness: Option<u64>,
        #[n(4)] #[serde(skip_serializing_if = "Option::is_none")] badge_handle: Option<String>,
    }
);

// ── state ──
body!(Snapshot {
    #[n(0)] sessions: Vec<Session>,
    #[n(1)] #[serde(skip_serializing_if = "Option::is_none")] away: Option<Vec<AwayEvent>>,
});
body!(AwayEvent {
    #[n(0)]
    sid: u64,
    #[n(1)]
    kind: AttKind,
    #[n(2)]
    ts: u64,
});
body!(
    /// Full object in `snapshot`; delta in `session_upd` (absent = unchanged).
    Session {
        #[n(0)] sid: u64,
        #[n(1)] #[serde(skip_serializing_if = "Option::is_none")] ag: Option<Agent>,
        #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] st: Option<SessState>,
        #[n(3)] #[serde(skip_serializing_if = "Option::is_none")] sub: Option<InputSub>,
        #[n(4)] #[serde(skip_serializing_if = "Option::is_none")] repo: Option<String>,
        #[n(5)] #[serde(skip_serializing_if = "Option::is_none")] br: Option<String>,
        #[n(6)] #[serde(skip_serializing_if = "Option::is_none")] tool: Option<String>,
        #[n(7)] #[serde(skip_serializing_if = "Option::is_none")] t0: Option<u64>,
        #[n(8)] #[serde(skip_serializing_if = "Option::is_none")] tst: Option<u64>,
        #[n(9)] #[serde(skip_serializing_if = "Option::is_none")] sum: Option<String>,
        #[n(10)] #[serde(skip_serializing_if = "Option::is_none")] seen: Option<bool>,
        #[n(11)] #[serde(skip_serializing_if = "Option::is_none")] host: Option<u64>,
        #[n(12)] #[serde(skip_serializing_if = "Option::is_none")] hn: Option<String>,
        #[n(13)] #[serde(skip_serializing_if = "Option::is_none")] snz: Option<u64>,
        #[n(14)] #[serde(skip_serializing_if = "Option::is_none")] ntf: Option<bool>,
        #[n(15)] #[serde(skip_serializing_if = "Option::is_none")] n: Option<u64>,
        #[n(16)] #[serde(skip_serializing_if = "Option::is_none")] ok: Option<bool>,
        /// The agent's closing question when its turn ended (proto 0.5); "" clears it.
        #[n(17)] #[serde(skip_serializing_if = "Option::is_none")] q: Option<String>,
        /// Answers to `q` offered on the watch's Reply list (proto 0.5); empty clears them.
        #[n(18)] #[serde(skip_serializing_if = "Option::is_none")] rp: Option<Vec<String>>,
    }
);
impl Session {
    /// A delta that only names the session.
    pub fn delta(sid: u64) -> Self {
        Session {
            sid,
            ag: None,
            st: None,
            sub: None,
            repo: None,
            br: None,
            tool: None,
            t0: None,
            tst: None,
            sum: None,
            seen: None,
            host: None,
            hn: None,
            snz: None,
            ntf: None,
            n: None,
            ok: None,
            q: None,
            rp: None,
        }
    }

    /// Applies a delta: keys present in `d` overwrite, absent keys are kept.
    pub fn merge(&mut self, d: &Session) {
        macro_rules! m { ($($f:ident),*) => { $(if d.$f.is_some() { self.$f = d.$f.clone(); })* }; }
        m!(ag, st, sub, repo, br, tool, t0, tst, sum, seen, host, hn, snz, ntf, n, ok);
    }
}
body!(Gone {
    #[n(0)]
    sid: u64
});
body!(Attention {
    #[n(0)] rid: u64,
    #[n(1)] sid: u64,
    #[n(2)] kind: AttKind,
    #[n(3)] #[serde(skip_serializing_if = "Option::is_none")] title: Option<String>,
    #[n(4)] #[serde(skip_serializing_if = "Option::is_none")] text: Option<String>,
    #[n(5)] #[serde(skip_serializing_if = "Option::is_none")] opts: Option<Vec<String>>,
    #[n(6)] acts: Vec<Action>,
    #[n(7)] #[serde(skip_serializing_if = "Option::is_none")] exp: Option<u64>,
    /// Bit `i` set: `opts[i]` grants permission (approve-class; the device commits it only by a hold).
    #[n(8)] #[serde(skip_serializing_if = "Option::is_none")] grant_mask: Option<u64>,
});
body!(Clear {
    #[n(0)]
    rid: u64,
    #[n(1)]
    why: ClearWhy,
});

// ── commands ──
body!(
    /// `cmd_op_scroll` argument (UI-V2 §6.1). `delta` < 0 is up (older), > 0 down (newer); with
    /// `ToEnd` its sign picks top or bottom.
    ScrollArg {
        #[n(0)] unit: ScrollUnit,
        #[n(1)] delta: i64,
        #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] seq: Option<u64>,
    }
);

/// The CDDL `cmd_arg` union (`uint / tstr / scroll_arg`): the typed members, plus `Int`, `Bool`
/// and raw CBOR so an unknown argument from a newer peer survives a round trip.
#[derive(Debug, Clone, PartialEq)]
pub enum Arg {
    Uint(u64),
    Int(i64),
    Text(String),
    Bool(bool),
    Scroll(ScrollArg),
    Raw(Vec<u8>),
}

impl Serialize for Arg {
    fn serialize<S: Serializer>(&self, s: S) -> Result<S::Ok, S::Error> {
        match self {
            Arg::Uint(v) => s.serialize_u64(*v),
            Arg::Int(v) => s.serialize_i64(*v),
            Arg::Text(v) => s.serialize_str(v),
            Arg::Bool(v) => s.serialize_bool(*v),
            Arg::Scroll(v) => v.serialize(s),
            Arg::Raw(v) => s.serialize_str(&format!("cbor:{}", hex::encode(v))),
        }
    }
}

impl<C> Encode<C> for Arg {
    fn encode<W: minicbor::encode::Write>(
        &self,
        e: &mut minicbor::Encoder<W>,
        _: &mut C,
    ) -> Result<(), minicbor::encode::Error<W::Error>> {
        match self {
            Arg::Uint(v) => e.u64(*v)?,
            Arg::Int(v) => e.i64(*v)?,
            Arg::Text(v) => e.str(v)?,
            Arg::Bool(v) => e.bool(*v)?,
            Arg::Scroll(v) => {
                v.encode(e, &mut ())?;
                e
            }
            Arg::Raw(v) => {
                e.writer_mut()
                    .write_all(v)
                    .map_err(minicbor::encode::Error::write)?;
                e
            }
        };
        Ok(())
    }
}

impl<'b, C> Decode<'b, C> for Arg {
    fn decode(d: &mut minicbor::Decoder<'b>, _: &mut C) -> Result<Self, minicbor::decode::Error> {
        use minicbor::data::Type;
        Ok(match d.datatype()? {
            Type::U8 | Type::U16 | Type::U32 | Type::U64 => Arg::Uint(d.u64()?),
            Type::I8 | Type::I16 | Type::I32 | Type::I64 => Arg::Int(d.i64()?),
            Type::String => Arg::Text(d.str()?.to_owned()),
            Type::Bool => Arg::Bool(d.bool()?),
            Type::Map => {
                // scroll_arg is the only map member today; anything else stays raw
                let start = d.position();
                match ScrollArg::decode(d, &mut ()) {
                    Ok(a) => Arg::Scroll(a),
                    Err(_) => {
                        d.set_position(start);
                        d.skip()?;
                        Arg::Raw(d.input()[start..d.position()].to_vec())
                    }
                }
            }
            _ => {
                let start = d.position();
                d.skip()?;
                Arg::Raw(d.input()[start..d.position()].to_vec())
            }
        })
    }
}

body!(Cmd {
    #[n(0)] op: CmdOp,
    #[n(1)] #[serde(skip_serializing_if = "Option::is_none")] sid: Option<u64>,
    #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] rid: Option<u64>,
    #[n(3)] #[serde(skip_serializing_if = "Option::is_none")] arg: Option<Arg>,
    #[n(4)] #[serde(skip_serializing_if = "Option::is_none")] hold_ms: Option<u64>,
});
body!(CmdResult {
    #[n(0)] ok: bool,
    #[n(1)] #[serde(skip_serializing_if = "Option::is_none")] err: Option<ResultErr>,
    #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] msg: Option<String>,
    /// Only in answers to `cmd_op_scroll`.
    #[n(3)] #[serde(skip_serializing_if = "Option::is_none")] pos: Option<ScrollPos>,
});
impl CmdResult {
    pub fn ok() -> Self {
        CmdResult {
            ok: true,
            err: None,
            msg: None,
            pos: None,
        }
    }
}
body!(
    /// Where the terminal's view is after a scroll. `preview` is terminal content: only sent when
    /// the device's privacy allows it, clipped to 48 chars, never logged.
    ScrollPos {
        #[n(0)] lines_above: u64,
        #[n(1)] at_bottom: bool,
        #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] total: Option<u64>,
        #[n(3)] #[serde(skip_serializing_if = "Option::is_none")] preview: Option<String>,
    }
);
body!(Detail {
    #[n(0)] #[serde(skip_serializing_if = "Option::is_none")] rid: Option<u64>,
    #[n(1)] #[serde(skip_serializing_if = "Option::is_none")] sid: Option<u64>,
    #[n(2)] text: String,
});
body!(Batt {
    #[n(0)]
    pct: u64,
    #[n(1)]
    chg: bool,
});
body!(Log {
    #[n(0)]
    lvl: LogLevel,
    #[n(1)]
    msg: String,
});

// ── audio ──
body!(AudioStart {
    #[n(0)] aid: u64,
    #[n(1)] codec: AudioCodec,
    #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] sid: Option<u64>,
});
body!(Audio {
    #[n(0)] aid: u64,
    #[n(1)] seq: u64,
    #[n(2)] #[cbor(with = "minicbor::bytes")] #[serde(serialize_with = "hex_ser")] data: Vec<u8>,
});
body!(AudioEnd {
    #[n(0)]
    aid: u64
});
body!(Transcript {
    #[n(0)]
    aid: u64,
    #[n(1)]
    text: String,
    #[n(2)]
    autosend_ms: u64,
});

// ── debug / observability ──
body!(DbgUiState {
    #[n(0)] screen: String,
    #[n(1)] #[serde(skip_serializing_if = "Option::is_none")] sel_sid: Option<u64>,
    #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] rid: Option<u64>,
    #[n(3)] visible: Vec<String>,
    #[n(4)] power: String,
    #[n(5)] #[serde(skip_serializing_if = "Option::is_none")] fps: Option<u64>,
});
body!(DbgScreenshotChunk {
    #[n(0)] w: u64,
    #[n(1)] h: u64,
    #[n(2)] offset: u64,
    #[n(3)] #[cbor(with = "minicbor::bytes")] #[serde(serialize_with = "hex_ser")] data: Vec<u8>,
    #[n(4)] last: bool,
});
body!(DbgInjectInput {
    #[n(0)] kind: InputEvent,
    #[n(1)] #[serde(skip_serializing_if = "Option::is_none")] x: Option<i64>,
    #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] y: Option<i64>,
});
body!(DbgSetLogLevel {
    #[n(0)]
    lvl: LogLevel
});
