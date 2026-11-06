//! One connected device: handshake (hello → welcome → snapshot), then deltas out and
//! commands in.

use crate::focus::{FocusDriver, FocusError, FocusTarget};
use crate::registry::{now_unix, Registry, Update};
use crate::trace::{Dir, Trace};
use crate::transport::Link;
use agentlink_proto::*;
use anyhow::{bail, Result};
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use tokio::sync::broadcast::error::RecvError;

/// Minimum hold-to-confirm for approvals (PROTOCOL §5).
const MIN_APPROVE_HOLD_MS: u64 = 1000;

/// State shared by every link task.
pub struct Shared {
    pub registry: Arc<Registry>,
    pub trace: Trace,
    pub focus: Arc<dyn FocusDriver>,
    pub host: HostInfo,
    /// Connected, handshaken devices, for `agentlink status`.
    pub devices: Mutex<BTreeMap<u64, Value>>,
    next_link: AtomicU64,
}

impl Shared {
    pub fn new(registry: Arc<Registry>, trace: Trace, focus: Arc<dyn FocusDriver>) -> Self {
        Shared {
            registry,
            trace,
            focus,
            host: host_info(),
            devices: Mutex::default(),
            next_link: AtomicU64::new(1),
        }
    }
}

fn host_info() -> HostInfo {
    let name = std::process::Command::new("hostname")
        .output()
        .ok()
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .map(|s| s.trim().to_owned())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "unknown".into());
    // FNV-1a over the hostname: stable 8-byte host id without extra dependencies.
    let h = name.bytes().fold(0xcbf29ce484222325u64, |h, b| {
        (h ^ b as u64).wrapping_mul(0x100000001b3)
    });
    let os = if cfg!(target_os = "macos") {
        HostOs::Mac
    } else if cfg!(windows) {
        HostOs::Win
    } else {
        HostOs::Linux
    };
    HostInfo {
        id: h.to_be_bytes(),
        name,
        os,
    }
}

struct Conn {
    link: Box<dyn Link>,
    trace: Trace,
    src: String,
    next_id: u64,
}

impl Conn {
    async fn send(&mut self, re: Option<u64>, msg: Message) -> Result<()> {
        let env = Envelope {
            id: self.next_id,
            re,
            msg,
        };
        self.next_id += 1;
        let bytes = env.to_bytes()?;
        self.trace.proto(&self.src, Dir::Out, &env, &bytes);
        self.link.send(bytes).await
    }

    /// Next decodable envelope; undecodable frames are traced and skipped.
    async fn recv(&mut self) -> Result<Option<Envelope>> {
        while let Some(bytes) = self.link.recv().await? {
            match Envelope::from_bytes(&bytes) {
                Ok(env) => {
                    self.trace.proto(&self.src, Dir::In, &env, &bytes);
                    return Ok(Some(env));
                }
                Err(e) => {
                    let msg = json!({"error": e.to_string()});
                    self.trace.record(
                        &self.src,
                        Dir::In,
                        "decode_error",
                        None,
                        None,
                        msg,
                        Some(&bytes),
                    );
                }
            }
        }
        Ok(None)
    }
}

fn welcome(host: &HostInfo) -> Welcome {
    let tz = chrono::Local::now().offset().local_minus_utc() / 60;
    Welcome {
        proto: PROTO,
        host: host.clone(),
        time: TimeSync {
            unix: now_unix(),
            tz_off: tz as i64,
        },
        cfg: DeviceCfg {
            privacy: Some(Privacy::Preview),
            alert: Some(AlertMode::Full),
            quiet_until: None,
            brightness: None,
            badge_handle: None,
        },
    }
}

pub async fn run_link(
    shared: Arc<Shared>,
    link: Box<dyn Link>,
    transport: &'static str,
) -> Result<()> {
    let peer = link.peer();
    let mut c = Conn {
        link,
        trace: shared.trace.clone(),
        src: transport.to_owned(),
        next_id: 1,
    };

    let hello = loop {
        match c.recv().await? {
            Some(Envelope {
                id,
                msg: Message::Hello(h),
                ..
            }) => break (id, h),
            Some(_) => continue, // nothing is valid before hello
            None => return Ok(()),
        }
    };
    if hello.1.proto.0 != PROTO.0 {
        bail!(
            "{peer}: protocol major {} not supported (daemon speaks {})",
            hello.1.proto.0,
            PROTO.0
        );
    }

    // Subscribe before taking the snapshot so no update is lost in between.
    let (sessions, mut updates) = shared.registry.subscribe();
    c.send(Some(hello.0), Message::Welcome(welcome(&shared.host)))
        .await?;
    c.send(
        None,
        Message::Snapshot(Snapshot {
            sessions,
            away: None,
        }),
    )
    .await?;

    let link_id = shared.next_link.fetch_add(1, Ordering::Relaxed);
    let info = serde_json::to_value(&hello.1.dev).unwrap_or_default();
    shared.devices.lock().unwrap().insert(
        link_id,
        json!({"transport": transport, "peer": peer, "dev": info}),
    );
    tracing::info!("device connected: {peer} ({transport})");

    let result = serve(&shared, &mut c, &mut updates).await;
    shared.devices.lock().unwrap().remove(&link_id);
    tracing::info!("device disconnected: {peer}");
    result
}

async fn serve(
    shared: &Shared,
    c: &mut Conn,
    updates: &mut tokio::sync::broadcast::Receiver<Update>,
) -> Result<()> {
    loop {
        tokio::select! {
            env = c.recv() => match env? {
                None => return Ok(()),
                Some(env) => handle(shared, c, env).await?,
            },
            u = updates.recv() => match u {
                Ok(Update::Upsert(s)) => c.send(None, Message::SessionUpd(s)).await?,
                Ok(Update::Gone(sid)) => c.send(None, Message::Gone(Gone { sid })).await?,
                // Missed updates: the device has drifted, so send a full replacement.
                Err(RecvError::Lagged(_)) => {
                    let sessions = shared.registry.snapshot();
                    c.send(None, Message::Snapshot(Snapshot { sessions, away: None })).await?;
                }
                Err(RecvError::Closed) => return Ok(()),
            },
        }
    }
}

async fn handle(shared: &Shared, c: &mut Conn, env: Envelope) -> Result<()> {
    match env.msg {
        Message::Cmd(cmd) => {
            let res = handle_cmd(shared, &cmd).await;
            c.send(Some(env.id), Message::Result(res)).await
        }
        Message::Ping(_) => c.send(Some(env.id), Message::Pong(Empty {})).await,
        // Traced on receipt; nothing else to do yet.
        _ => Ok(()),
    }
}

fn fail(err: ResultErr, msg: &str) -> CmdResult {
    CmdResult {
        ok: false,
        err: Some(err),
        msg: Some(msg.into()),
    }
}

async fn handle_cmd(shared: &Shared, cmd: &Cmd) -> CmdResult {
    let detail = serde_json::to_value(cmd).unwrap_or_default();
    match cmd.op {
        CmdOp::Focus => {
            let Some((session, ctx)) = cmd.sid.and_then(|sid| shared.registry.get(sid)) else {
                return fail(ResultErr::Stale, "unknown session");
            };
            let target = FocusTarget {
                sid: session.sid,
                agent: session.ag,
                ctx,
            };
            let res = shared.focus.focus(&target).await;
            shared.trace.audit(
                "focus",
                cmd.sid,
                None,
                json!({"cmd": detail, "ok": res.is_ok()}),
            );
            match res {
                Ok(()) => CmdResult {
                    ok: true,
                    err: None,
                    msg: None,
                },
                Err(FocusError::NoWindow) => fail(ResultErr::NoWindow, "no window found"),
                Err(FocusError::Unsupported) => {
                    fail(ResultErr::Unsupported, "focus unsupported here")
                }
                Err(FocusError::Internal(m)) => fail(ResultErr::Internal, &m),
            }
        }
        CmdOp::Approve | CmdOp::Deny => {
            // No approval broker yet, so no rid is ever live: always stale (PROTOCOL §5).
            let hold_ok = cmd.op == CmdOp::Deny || cmd.hold_ms.unwrap_or(0) >= MIN_APPROVE_HOLD_MS;
            let res = if !hold_ok {
                fail(ResultErr::Denied, "hold-to-confirm too short")
            } else {
                fail(ResultErr::Stale, "no pending request for rid")
            };
            shared.trace.audit(
                "approval",
                cmd.sid,
                cmd.rid,
                json!({"cmd": detail, "result": res}),
            );
            res
        }
        CmdOp::Ack => CmdResult {
            ok: true,
            err: None,
            msg: None,
        },
        _ => fail(ResultErr::Unsupported, "command not implemented"),
    }
}
