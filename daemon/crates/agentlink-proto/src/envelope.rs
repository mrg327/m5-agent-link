//! The envelope `{0 => type id, 1 => id, ?2 => re, 3 => body}` and the message union.

use crate::body::*;
use crate::hex_ser;
use minicbor::{data::Type, Decoder, Encoder};
use serde::Serialize;

pub type DecodeError = minicbor::decode::Error;
pub type EncodeError = minicbor::encode::Error<std::convert::Infallible>;

/// A message whose type id this version does not know; kept verbatim so it can be traced
/// (decoders MUST ignore unknown types).
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Unknown {
    pub t: u64,
    #[serde(serialize_with = "hex_ser")]
    pub body: Vec<u8>,
}

macro_rules! messages {
    ($($id:literal $var:ident($ty:ty)),+ $(,)?) => {
        /// Message type + body. Serializes as `{"t": "<name>", "b": {...}}`.
        #[derive(Debug, Clone, PartialEq, Serialize)]
        #[serde(tag = "t", content = "b", rename_all = "snake_case")]
        #[allow(clippy::large_enum_variant)] // a session object with every optional key; boxed would touch every user
        pub enum Message {
            $($var($ty),)+
            Unknown(Unknown),
        }

        impl Message {
            pub fn type_id(&self) -> u64 {
                match self { $(Message::$var(_) => $id,)+ Message::Unknown(u) => u.t }
            }

            fn encode_body(&self, e: &mut Encoder<&mut Vec<u8>>) -> Result<(), EncodeError> {
                match self {
                    $(Message::$var(b) => { e.encode(b)?; })+
                    Message::Unknown(u) => {
                        e.writer_mut().extend_from_slice(&u.body);
                    }
                }
                Ok(())
            }

            fn decode_body(t: u64, body: &[u8]) -> Result<Self, DecodeError> {
                Ok(match t {
                    $($id => Message::$var(minicbor::decode(body)?),)+
                    _ => Message::Unknown(Unknown { t, body: body.to_vec() }),
                })
            }
        }
    };
}

messages! {
    1 Hello(Hello), 2 Welcome(Welcome), 3 Snapshot(Snapshot), 4 SessionUpd(Session),
    5 Gone(Gone), 6 Attention(Attention), 7 Clear(Clear), 8 Cmd(Cmd), 9 Result(CmdResult),
    10 Detail(Detail), 11 Cfg(DeviceCfg), 12 Batt(Batt), 13 Ping(Empty), 14 Pong(Empty),
    15 Log(Log), 16 AudioStart(AudioStart), 17 Audio(Audio), 18 AudioEnd(AudioEnd),
    19 Transcript(Transcript), 20 KbStart(KbStart), 21 KbStroke(KbStroke), 22 KbCands(KbCands), 23 Usage(Usage),
    24 FwBegin(FwBegin), 25 FwChunk(FwChunk), 26 FwAck(FwAck), 27 Power(Power),
    64 DbgUiStateReq(Empty), 65 DbgUiState(DbgUiState),
    66 DbgScreenshotReq(Empty), 67 DbgScreenshotChunk(DbgScreenshotChunk),
    68 DbgInjectInput(DbgInjectInput), 69 DbgSetLogLevel(DbgSetLogLevel),
}

/// One protocol message on the wire.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Envelope {
    /// Sender's monotonic per-link counter.
    pub id: u64,
    /// The `id` this message replies to.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub re: Option<u64>,
    #[serde(flatten)]
    pub msg: Message,
}

impl Envelope {
    pub fn new(id: u64, msg: Message) -> Self {
        Envelope { id, re: None, msg }
    }

    pub fn reply(id: u64, re: u64, msg: Message) -> Self {
        Envelope {
            id,
            re: Some(re),
            msg,
        }
    }

    pub fn to_bytes(&self) -> Result<Vec<u8>, EncodeError> {
        let mut buf = Vec::new();
        let mut e = Encoder::new(&mut buf);
        e.map(if self.re.is_some() { 4 } else { 3 })?;
        e.u8(0)?.u64(self.msg.type_id())?;
        e.u8(1)?.u64(self.id)?;
        if let Some(re) = self.re {
            e.u8(2)?.u64(re)?;
        }
        e.u8(3)?;
        self.msg.encode_body(&mut e)?;
        Ok(buf)
    }

    pub fn from_bytes(bytes: &[u8]) -> Result<Self, DecodeError> {
        let mut d = Decoder::new(bytes);
        let len = d.map()?;
        let (mut t, mut id, mut re, mut body) = (None, None, None, None);
        let mut seen = 0u64;
        loop {
            match len {
                Some(n) if seen == n => break,
                None if d.datatype()? == Type::Break => break,
                _ => {}
            }
            seen += 1;
            match d.u64()? {
                0 => t = Some(d.u64()?),
                1 => id = Some(d.u64()?),
                2 => re = Some(d.u64()?),
                3 => {
                    let start = d.position();
                    d.skip()?;
                    body = Some(&bytes[start..d.position()]);
                }
                _ => d.skip()?,
            }
        }
        let t = t.ok_or_else(|| DecodeError::message("envelope: missing type (0)"))?;
        let id = id.ok_or_else(|| DecodeError::message("envelope: missing id (1)"))?;
        let body = body.ok_or_else(|| DecodeError::message("envelope: missing body (3)"))?;
        Ok(Envelope {
            id,
            re,
            msg: Message::decode_body(t, body)?,
        })
    }
}
