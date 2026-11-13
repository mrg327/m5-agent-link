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
    /// KEYS swipe keyboard (proto 0.7): this daemon decodes `kb_stroke` into `kb_cands` and types them.
    #[n(6)]
    #[serde(skip_serializing_if = "Option::is_none")]
    keys: Option<bool>,
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
        /// A job's expected total run time in seconds (proto 0.9); 0 = unknown.
        #[n(19)] #[serde(skip_serializing_if = "Option::is_none")] eta: Option<u64>,
        /// A job's exit status, sent with the delta that enters done / error (proto 0.9).
        #[n(20)] #[serde(skip_serializing_if = "Option::is_none")] exit: Option<u64>,
        /// Context window used, percent 0-100; `CTX_UNKNOWN` (255) clears it (proto 0.10).
        #[n(21)] #[serde(skip_serializing_if = "Option::is_none")] ctx: Option<u64>,
        /// The model's short name, <= 16 chars; "" = unknown (proto 0.10).
        #[n(22)] #[serde(skip_serializing_if = "Option::is_none")] mdl: Option<String>,
        /// The agent's permission mode (proto 0.10).
        #[n(23)] #[serde(skip_serializing_if = "Option::is_none")] pm: Option<PermMode>,
        /// The branch's open pull request number; 0 = none (proto 0.10, withheld under privacy meta).
        #[n(24)] #[serde(skip_serializing_if = "Option::is_none")] pr: Option<u64>,
        /// Its review state, sent with `pr` (proto 0.10).
        #[n(25)] #[serde(skip_serializing_if = "Option::is_none")] prs: Option<PrState>,
        /// With the delta that enters done / error: do not buzz for it, the user is looking at its
        /// terminal (proto 0.12, PRESENCE.md). Never stored; absent = buzz as the device's rules say.
        #[n(26)] #[serde(skip_serializing_if = "Option::is_none")] hush: Option<bool>,
        /// Done summary (proto 0.14, DONE-SUMMARY.md): files the turn changed.
        #[n(30)] #[serde(skip_serializing_if = "Option::is_none")] fc: Option<u64>,
        /// Lines added / removed in the turn, sent together.
        #[n(31)] #[serde(skip_serializing_if = "Option::is_none")] la: Option<u64>,
        #[n(32)] #[serde(skip_serializing_if = "Option::is_none")] lr: Option<u64>,
        /// The turn's last recognised test run: true passed, false failed.
        #[n(33)] #[serde(skip_serializing_if = "Option::is_none")] tp: Option<bool>,
        /// The turn's length in seconds.
        #[n(34)] #[serde(skip_serializing_if = "Option::is_none")] dur: Option<u64>,
        /// The message queued for this session (proto 0.17): the start of its text, <= 48 chars; "" = none.
        #[n(35)] #[serde(skip_serializing_if = "Option::is_none")] qd: Option<String>,
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
            eta: None,
            exit: None,
            ctx: None,
            mdl: None,
            pm: None,
            pr: None,
            prs: None,
            hush: None,
            fc: None,
            la: None,
            lr: None,
            tp: None,
            dur: None,
            qd: None,
        }
    }

    /// Applies a delta: keys present in `d` overwrite, absent keys are kept.
    pub fn merge(&mut self, d: &Session) {
        macro_rules! m { ($($f:ident),*) => { $(if d.$f.is_some() { self.$f = d.$f.clone(); })* }; }
        m!(
            ag, st, sub, repo, br, tool, t0, tst, sum, seen, host, hn, snz, ntf, n, ok, eta, exit,
            ctx, mdl, pm, pr, prs, hush, fc, la, lr, tp, dur, qd
        );
    }
}
/// `session.ctx` value that clears the context fill (a delta cannot remove a key).
pub const CTX_UNKNOWN: u64 = 255;

body!(
    /// The plan's rate-limit windows (proto 0.10, GAUGES.md). A full replacement: an absent window is
    /// unknown, never zero.
    Usage {
        #[n(0)] at: u64,
        #[n(1)] #[serde(skip_serializing_if = "Option::is_none")] h5: Option<u64>,
        #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] h5r: Option<u64>,
        #[n(3)] #[serde(skip_serializing_if = "Option::is_none")] h5o: Option<u64>,
        #[n(4)] #[serde(skip_serializing_if = "Option::is_none")] wk: Option<u64>,
        #[n(5)] #[serde(skip_serializing_if = "Option::is_none")] wkr: Option<u64>,
        #[n(6)] #[serde(skip_serializing_if = "Option::is_none")] wko: Option<u64>,
    }
);

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
    /// Milliseconds left in the answer window when this message was sent (proto 0.11, ANSWER-WINDOW.md).
    #[n(9)] #[serde(skip_serializing_if = "Option::is_none")] left: Option<u64>,
    /// The whole answer window in milliseconds (proto 0.11).
    #[n(10)] #[serde(skip_serializing_if = "Option::is_none")] win: Option<u64>,
    /// The full text's length in bytes when `text` is a cut preview (proto 0.11): `detail` serves it.
    #[n(11)] #[serde(skip_serializing_if = "Option::is_none")] len: Option<u64>,
    /// No buzz, glow or reminders for this request: the user is looking at its terminal (proto 0.12,
    /// PRESENCE.md). The card is shown all the same.
    #[n(12)] #[serde(skip_serializing_if = "Option::is_none")] hush: Option<bool>,
    /// The standing rule the device may grant with this request, exactly as Claude Code will save it
    /// (`Bash(npm test:*)`; several rules one per line). Proto 0.13, STANDING-RULES.md: keys 13-15 only
    /// to devices whose hello says minor >= 13, always together.
    #[n(13)] #[serde(skip_serializing_if = "Option::is_none")] rule: Option<String>,
    /// Where it is saved: this session only, or this project (Claude Code's local settings).
    #[n(14)] #[serde(skip_serializing_if = "Option::is_none")] scope: Option<RuleScope>,
    /// The rule matches more than this exact request (it ends in a wildcard).
    #[n(15)] #[serde(skip_serializing_if = "Option::is_none")] wide: Option<bool>,
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
    /// With `approve` (proto 0.13): also save the request's standing rule. The hash of the rule as the device
    /// showed it (STANDING-RULES.md §5); the daemon grants only when it is the hash of its own rule.
    #[n(5)] #[serde(skip_serializing_if = "Option::is_none")] rh: Option<u64>,
    /// With `type` / `dictate` (proto 0.17): queue the message if the session cannot take input now.
    #[n(6)] #[serde(skip_serializing_if = "Option::is_none")] q: Option<bool>,
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
body!(
    /// A chunk of a request's full text (proto 0.11): the answer to `cmd detail {rid, arg: off}`.
    Detail {
        #[n(0)] #[serde(skip_serializing_if = "Option::is_none")] rid: Option<u64>,
        #[n(1)] #[serde(skip_serializing_if = "Option::is_none")] sid: Option<u64>,
        #[n(2)] text: String,
        /// Byte offset of this chunk in the full text.
        #[n(3)] #[serde(skip_serializing_if = "Option::is_none")] off: Option<u64>,
        /// The full text's length in bytes.
        #[n(4)] #[serde(skip_serializing_if = "Option::is_none")] total: Option<u64>,
    }
);
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

// ── firmware update (proto 0.15) ──
body!(FwBegin {
    /// The image's length in bytes; 0 stops the transfer in progress.
    #[n(0)] size: u64,
    /// SHA-256 of the whole image: a begin naming the image in progress resumes it.
    #[n(1)] #[cbor(with = "minicbor::bytes")] #[serde(serialize_with = "hex_ser")] sha: [u8; 32],
    #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] ver: Option<String>,
});
body!(FwChunk {
    #[n(0)] off: u64,
    #[n(1)] #[cbor(with = "minicbor::bytes")] #[serde(serialize_with = "hex_ser")] data: Vec<u8>,
});
body!(FwAck {
    #[n(0)] st: FwStatus,
    /// Bytes received and written so far: the offset the device wants next.
    #[n(1)] next: u64,
    #[n(2)] #[serde(skip_serializing_if = "Option::is_none")] msg: Option<String>,
});

// ── keyboard (proto 0.7) ──
body!(KbStart {
    #[n(0)] kid: u64,
    #[n(1)] sid: u64,
    /// Per key 10 B LE: u16 cp, i16 x, i16 y, u16 w, u16 h (top-left, px).
    #[n(2)] #[cbor(with = "minicbor::bytes")] #[serde(serialize_with = "hex_ser")] keys: Vec<u8>,
});
body!(KbStroke {
    #[n(0)] kid: u64,
    #[n(1)] seq: u64,
    /// Per point 6 B LE: i16 x, i16 y, u16 t_ms since touch-down.
    #[n(2)] #[cbor(with = "minicbor::bytes")] #[serde(serialize_with = "hex_ser")] pts: Vec<u8>,
});
body!(KbCands {
    #[n(0)] kid: u64,
    #[n(1)] seq: u64,
    #[n(2)] cands: Vec<String>,
    #[n(3)] #[serde(skip_serializing_if = "Option::is_none")] typed: Option<u64>,
    #[n(4)] #[serde(skip_serializing_if = "Option::is_none")] err: Option<ResultErr>,
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
