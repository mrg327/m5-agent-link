//! agentlink wire protocol v0.1.
//!
//! The schema is normative in `protocol/agentlink.cddl`; these types mirror it
//! field-for-field (integer CBOR keys via `#[n(k)]`). Every type also implements
//! `serde::Serialize` using the *symbolic* names from the vectors, which is what
//! the trace log and `agentlink decode` print.

mod body;
mod enums;
mod envelope;

pub use body::*;
pub use enums::*;
pub use envelope::{DecodeError, EncodeError, Envelope, Message, Unknown};

/// Protocol version spoken by this crate: `[major, minor]`.
pub const PROTO: Version = Version(0, 16);

fn ser_hex<S: serde::Serializer, T: AsRef<[u8]>>(v: &T, s: S) -> Result<S::Ok, S::Error> {
    s.serialize_str(&hex::encode(v.as_ref()))
}
pub(crate) use ser_hex as hex_ser;
