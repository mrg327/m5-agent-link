//! JSONL trace (DESIGN §0): one line per hook event, state transition and protocol message.
//!
//! `{ts_us, ts, src, dir, kind, sid?, rid?, msg, bytes?}` in `~/.agentlink/trace/YYYY-MM-DD.jsonl`.
//! Approvals and focus actions are also appended to `audit.jsonl`.

use agentlink_proto::Envelope;
use serde_json::{json, Value};
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

/// Replaces a session's step summary (`sum`: command lines, paths) in a serialized session,
/// `session_upd` body or snapshot body, like the scroll preview it never reaches the logs.
/// Returns whether anything was replaced.
pub fn redact_sum(v: &mut Value) -> bool {
    let mut hit = false;
    if let Some(o) = v.as_object_mut() {
        // sum: command lines and paths; q / rp: the agent's closing question and its answers
        for k in ["sum", "q", "rp"] {
            if o.contains_key(k) {
                o.insert(k.into(), "<private>".into());
                hit = true;
            }
        }
    }
    if let Some(list) = v.get_mut("sessions").and_then(Value::as_array_mut) {
        for s in list {
            hit |= redact_sum(s);
        }
    }
    hit
}

#[derive(Clone, Copy, Debug)]
pub enum Dir {
    In,
    Out,
    Internal,
}

impl Dir {
    fn as_str(self) -> &'static str {
        match self {
            Dir::In => "in",
            Dir::Out => "out",
            Dir::Internal => "internal",
        }
    }
}

#[derive(Clone)]
pub struct Trace {
    inner: Arc<Mutex<Inner>>,
}

struct Inner {
    dir: Option<PathBuf>,
    audit: Option<PathBuf>,
    day: String,
    file: Option<File>,
}

fn open_append(path: &Path) -> std::io::Result<File> {
    if let Some(p) = path.parent() {
        std::fs::create_dir_all(p)?;
    }
    OpenOptions::new().create(true).append(true).open(path)
}

impl Trace {
    pub fn new(dir: PathBuf, audit: PathBuf) -> Self {
        Trace {
            inner: Arc::new(Mutex::new(Inner {
                dir: Some(dir),
                audit: Some(audit),
                day: String::new(),
                file: None,
            })),
        }
    }

    #[cfg(test)]
    /// A trace that discards everything (unit tests).
    pub fn null() -> Self {
        Trace {
            inner: Arc::new(Mutex::new(Inner {
                dir: None,
                audit: None,
                day: String::new(),
                file: None,
            })),
        }
    }

    /// Logs one event. Failures to write are reported on stderr but never propagate.
    #[allow(clippy::too_many_arguments)] // flat on purpose: mirrors the JSONL fields
    pub fn record(
        &self,
        src: &str,
        dir: Dir,
        kind: &str,
        sid: Option<u64>,
        rid: Option<u64>,
        msg: Value,
        bytes: Option<&[u8]>,
    ) {
        if self.inner.lock().unwrap().dir.is_none() {
            return;
        }
        let now = chrono::Local::now();
        let mut line = json!({
            "ts_us": now.timestamp_micros(),
            "ts": now.to_rfc3339(),
            "src": src,
            "dir": dir.as_str(),
            "kind": kind,
            "msg": msg,
        });
        if let Some(s) = sid {
            line["sid"] = s.into();
        }
        if let Some(r) = rid {
            line["rid"] = r.into();
        }
        if let Some(b) = bytes {
            line["bytes"] = hex::encode(b).into();
        }
        let mut text = line.to_string();
        text.push('\n');

        let mut g = self.inner.lock().unwrap();
        let day = now.format("%Y-%m-%d").to_string();
        if g.day != day || g.file.is_none() {
            let dir = g.dir.clone().unwrap_or_default();
            match open_append(&dir.join(format!("{day}.jsonl"))) {
                Ok(f) => {
                    g.file = Some(f);
                    g.day = day;
                }
                Err(e) => return eprintln!("trace: cannot open trace file: {e}"),
            }
        }
        if let Some(f) = g.file.as_mut() {
            if let Err(e) = f.write_all(text.as_bytes()) {
                eprintln!("trace: write failed: {e}");
            }
        }
    }

    /// Logs a protocol message with its decoded JSON and raw bytes.
    pub fn proto(&self, src: &str, dir: Dir, env: &Envelope, bytes: &[u8]) {
        let mut msg = serde_json::to_value(env).unwrap_or(Value::Null);
        let mut private = msg.get_mut("b").is_some_and(redact_sum);
        let kind = msg["t"].as_str().unwrap_or("?").to_owned();
        // voice: the user's recorded speech and its transcript never reach the logs
        for (t, key) in [("audio", "data"), ("transcript", "text")] {
            if kind == t {
                if let Some(x) = msg.pointer_mut(&format!("/b/{key}")) {
                    *x = Value::String("<private>".into());
                    private = true;
                }
            }
        }
        let sid = msg["b"]["sid"].as_u64();
        let rid = msg["b"]["rid"].as_u64();
        self.record(src, dir, &kind, sid, rid, msg, (!private).then_some(bytes));
    }

    /// Like `proto`, for a message that carries terminal content (a scroll result's preview):
    /// the content is replaced before the line is written and the raw bytes are left out.
    pub fn proto_private(&self, src: &str, dir: Dir, env: &Envelope) {
        let mut msg = serde_json::to_value(env).unwrap_or(Value::Null);
        if let Some(p) = msg.pointer_mut("/b/pos/preview") {
            *p = Value::String("<private>".into());
        }
        let kind = msg["t"].as_str().unwrap_or("?").to_owned();
        let sid = msg["b"]["sid"].as_u64();
        self.record(src, dir, &kind, sid, None, msg, None);
    }

    /// Appends to the audit log (and the trace) for approvals and focus actions.
    pub fn audit(&self, kind: &str, sid: Option<u64>, rid: Option<u64>, msg: Value) {
        let line = json!({"ts": chrono::Local::now().to_rfc3339(), "kind": kind, "sid": sid, "rid": rid, "msg": msg});
        let path = self.inner.lock().unwrap().audit.clone();
        if let Some(path) = path {
            if let Err(e) = open_append(&path).and_then(|mut f| writeln!(f, "{line}")) {
                eprintln!("audit: write failed: {e}");
            }
        }
        self.record("audit", Dir::Internal, kind, sid, rid, msg, None);
    }
}
