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
        // sum: command lines and paths; q / rp: the agent's closing question and its answers;
        // qd: the start of a queued message; lp: the start of the agent's last message
        for k in ["sum", "q", "rp", "qd", "lp"] {
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

/// A shell job's hook line carries the command line the user typed (`payload.cmd`), Claude Code's
/// `Stop` the agent's last message (`payload.last_assistant_message`): replaced before the line is
/// traced, like `sum`. Returns whether anything was replaced.
pub fn redact_hook(v: &mut Value) -> bool {
    let key = if v["agent"].as_str() == Some("shell") {
        "/payload/cmd"
    } else {
        "/payload/last_assistant_message"
    };
    match v.pointer_mut(key) {
        Some(c) => {
            *c = Value::String("<private>".into());
            true
        }
        None => false,
    }
}

/// Keyboard messages carry the user's touch strokes and the words decoded from them
/// (issue 20261110-1e57): replaced like speech. Returns whether anything was replaced.
pub fn redact_keys(kind: &str, msg: &mut Value) -> bool {
    let keys: &[&str] = match kind {
        "kb_start" => &["keys"],
        "kb_stroke" => &["pts"],
        "kb_cands" => &["cands"],
        _ => return false,
    };
    let mut hit = false;
    for k in keys {
        if let Some(x) = msg.pointer_mut(&format!("/b/{k}")) {
            *x = Value::String("<private>".into());
            hit = true;
        }
    }
    hit
}

/// Firmware chunks are the image, not log material: only their length is traced.
pub fn redact_fw(kind: &str, msg: &mut Value) -> bool {
    if kind != "fw_chunk" {
        return false;
    }
    let Some(b) = msg.get_mut("b").and_then(Value::as_object_mut) else {
        return false;
    };
    let len = b
        .get("data")
        .and_then(Value::as_str)
        .map_or(0, |h| h.len() / 2);
    b.insert("data".into(), "<private>".into());
    b.insert("len".into(), len.into());
    true
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
        // the agent's last message, read on the watch (LAST-MESSAGE.md §2.3)
        if kind == "detail" && msg["b"].get("lm").is_some() {
            if let Some(x) = msg.pointer_mut("/b/text") {
                *x = Value::String("<private>".into());
                private = true;
            }
        }
        private |= redact_keys(&kind, &mut msg);
        private |= redact_fw(&kind, &mut msg);
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
        let text = format!("{line}\n");
        {
            // one write per line, under the lock: concurrent audits must not interleave
            let g = self.inner.lock().unwrap();
            if let Some(path) = g.audit.as_ref() {
                if let Err(e) = open_append(path).and_then(|mut f| f.write_all(text.as_bytes())) {
                    eprintln!("audit: write failed: {e}");
                }
            }
        }
        self.record("audit", Dir::Internal, kind, sid, rid, msg, None);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn concurrent_audits_stay_whole_lines() {
        let d = std::env::temp_dir().join(format!("agentlink-audit-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        let t = Trace::new(d.join("trace"), d.join("audit.jsonl"));
        let big = "x".repeat(4000);
        std::thread::scope(|s| {
            for i in 0..8 {
                let (t, big) = (t.clone(), big.clone());
                s.spawn(move || {
                    for j in 0..25 {
                        t.audit("approval", Some(i), Some(j), json!({"pad": big}));
                    }
                });
            }
        });
        let text = std::fs::read_to_string(d.join("audit.jsonl")).unwrap();
        let _ = std::fs::remove_dir_all(&d);
        assert_eq!(text.lines().count(), 200);
        for l in text.lines() {
            serde_json::from_str::<Value>(l).expect("a whole JSON line");
        }
    }

    #[test]
    fn shell_hook_lines_lose_the_command() {
        let mut v = json!({"agent": "shell", "event": "start", "payload": {"cmd": "psql -W secret", "pid": 7}});
        assert!(redact_hook(&mut v));
        assert_eq!(v["payload"]["cmd"], "<private>");
        assert_eq!(v["payload"]["pid"], 7);
        assert!(!v.to_string().contains("secret"));
        let mut other = json!({"agent": "claude", "payload": {"cmd": "x"}});
        assert!(!redact_hook(&mut other));
        assert_eq!(other["payload"]["cmd"], "x");
    }

    #[test]
    fn the_agents_last_message_never_reaches_the_trace() {
        let mut v = json!({"agent": "claude", "event": "Stop",
            "payload": {"session_id": "s", "last_assistant_message": "secret plan"}});
        assert!(redact_hook(&mut v));
        assert_eq!(v["payload"]["last_assistant_message"], "<private>");
        assert_eq!(v["payload"]["session_id"], "s");
        let mut s = json!({"sid": 1, "lm": 7, "ll": 11, "lp": "secret plan"});
        assert!(redact_sum(&mut s));
        assert_eq!(
            (s["lp"].as_str(), s["lm"].as_u64()),
            (Some("<private>"), Some(7))
        );
        let mut snap = json!({"sessions": [{"sid": 1, "lp": "secret plan"}]});
        assert!(redact_sum(&mut snap));
        assert!(!snap.to_string().contains("secret"));
    }

    #[test]
    fn a_last_message_chunk_is_traced_without_its_text_or_bytes() {
        use agentlink_proto::{Detail, Message};
        let d = std::env::temp_dir().join(format!("agentlink-lm-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        let t = Trace::new(d.join("trace"), d.join("audit.jsonl"));
        let chunk = |lm: Option<u64>, rid: Option<u64>| {
            Envelope::new(
                3,
                Message::Detail(Detail {
                    rid,
                    sid: Some(1),
                    text: "secret plan".into(),
                    off: Some(0),
                    total: Some(11),
                    lm,
                }),
            )
        };
        let env = chunk(Some(9), None);
        t.proto("ws", Dir::Out, &env, &env.to_bytes().unwrap());
        let env = chunk(None, Some(4));
        t.proto("ws", Dir::Out, &env, &env.to_bytes().unwrap());
        let text = std::fs::read_dir(d.join("trace"))
            .unwrap()
            .map(|f| std::fs::read_to_string(f.unwrap().path()).unwrap())
            .collect::<String>();
        let _ = std::fs::remove_dir_all(&d);
        let lines: Vec<Value> = text
            .lines()
            .map(|l| serde_json::from_str(l).unwrap())
            .collect();
        assert_eq!(lines.len(), 2);
        assert_eq!(lines[0]["msg"]["b"]["text"], "<private>");
        assert!(lines[0].get("bytes").is_none(), "nor the raw bytes");
        assert_eq!(lines[0]["msg"]["b"]["lm"], 9);
        // a request's own text (by rid) is traced as before
        assert_eq!(lines[1]["msg"]["b"]["text"], "secret plan");
    }

    #[test]
    fn firmware_chunks_show_only_their_length() {
        let mut m = json!({"t": "fw_chunk", "b": {"off": 0, "data": "deadbeef"}});
        assert!(redact_fw("fw_chunk", &mut m));
        assert_eq!(m["b"]["data"], "<private>");
        assert_eq!(m["b"]["len"], 4);
        assert!(!redact_fw("fw_ack", &mut json!({"b": {}})));
    }

    #[test]
    fn keyboard_strokes_keys_and_candidates_are_private() {
        for (kind, key) in [
            ("kb_start", "keys"),
            ("kb_stroke", "pts"),
            ("kb_cands", "cands"),
        ] {
            let mut m = json!({"t": kind, "b": {"kid": 1, "seq": 2, key: ["secret"]}});
            assert!(redact_keys(kind, &mut m));
            assert_eq!(m["b"][key], "<private>");
            assert_eq!(m["b"]["kid"], 1);
        }
        let mut other = json!({"t": "ping", "b": {"pts": 1}});
        assert!(!redact_keys("ping", &mut other));
    }
}
