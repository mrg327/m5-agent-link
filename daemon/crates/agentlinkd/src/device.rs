//! One connected device: handshake (hello → welcome → snapshot), then deltas out and
//! commands in.

use crate::broker::{Broker, Decision};
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
    pub broker: Arc<Broker>,
    pub trace: Trace,
    pub focus: Arc<dyn FocusDriver>,
    pub host: HostInfo,
    /// Connected, handshaken devices keyed by device uid, for `agentlink status`.
    pub devices: Mutex<BTreeMap<String, Value>>,
    next_link: AtomicU64,
}

impl Shared {
    pub fn new(
        registry: Arc<Registry>,
        broker: Arc<Broker>,
        trace: Trace,
        focus: Arc<dyn FocusDriver>,
    ) -> Self {
        Shared {
            registry,
            broker,
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
    let link_id = shared.next_link.fetch_add(1, Ordering::Relaxed);
    let mut dev_key = None;
    let mut updates = handshake(
        &shared,
        &mut c,
        hello,
        link_id,
        transport,
        &peer,
        &mut dev_key,
    )
    .await?;
    tracing::info!("device connected: {peer} ({transport})");

    let result = serve(
        &shared,
        &mut c,
        &mut updates,
        link_id,
        transport,
        &peer,
        &mut dev_key,
    )
    .await;
    if let Some(key) = dev_key {
        let mut devices = shared.devices.lock().unwrap();
        // A newer link for the same device may already have replaced our entry.
        if devices.get(&key).and_then(|d| d["link"].as_u64()) == Some(link_id) {
            devices.remove(&key);
        }
    }
    tracing::info!("device disconnected: {peer}");
    result
}

/// hello → welcome → snapshot. Also used for a hello on a live link (device reboot or
/// link timeout), which restarts the link's protocol state. Returns a fresh update
/// subscription.
async fn handshake(
    shared: &Shared,
    c: &mut Conn,
    hello: (u64, Hello),
    link_id: u64,
    transport: &'static str,
    peer: &str,
    dev_key: &mut Option<String>,
) -> Result<tokio::sync::broadcast::Receiver<Update>> {
    if hello.1.proto.0 != PROTO.0 {
        bail!(
            "{peer}: protocol major {} not supported (daemon speaks {})",
            hello.1.proto.0,
            PROTO.0
        );
    }
    c.next_id = 1;

    // Subscribe before taking the snapshot so no update is lost in between.
    let (sessions, updates) = shared.registry.subscribe();
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
    // Requests still waiting for an answer: a device that connects now can still give it.
    for att in shared.broker.pending() {
        c.send(None, Message::Attention(att)).await?;
    }

    // One entry per device id, so a re-handshake or a reconnect on a new link replaces it.
    let key = hex::encode(hello.1.dev.uid);
    let info = serde_json::to_value(&hello.1.dev).unwrap_or_default();
    let mut devices = shared.devices.lock().unwrap();
    if let Some(old) = dev_key.take().filter(|k| *k != key) {
        devices.remove(&old);
    }
    devices.insert(
        key.clone(),
        json!({"transport": transport, "peer": peer, "dev": info, "link": link_id}),
    );
    *dev_key = Some(key);
    Ok(updates)
}

async fn serve(
    shared: &Shared,
    c: &mut Conn,
    updates: &mut tokio::sync::broadcast::Receiver<Update>,
    link_id: u64,
    transport: &'static str,
    peer: &str,
    dev_key: &mut Option<String>,
) -> Result<()> {
    loop {
        tokio::select! {
            env = c.recv() => match env? {
                None => return Ok(()),
                Some(Envelope { id, msg: Message::Hello(h), .. }) => {
                    shared.trace.record(&c.src, Dir::Internal, "rehandshake", None, None, json!({"peer": peer}), None);
                    *updates = handshake(shared, c, (id, h), link_id, transport, peer, dev_key).await?;
                    tracing::info!("device re-handshake: {peer} ({transport})");
                }
                Some(env) => handle(shared, c, env).await?,
            },
            u = updates.recv() => match u {
                Ok(Update::Upsert(s)) => c.send(None, Message::SessionUpd(s)).await?,
                Ok(Update::Gone(sid)) => c.send(None, Message::Gone(Gone { sid })).await?,
                Ok(Update::Attention(a)) => c.send(None, Message::Attention(a)).await?,
                Ok(Update::Clear(cl)) => c.send(None, Message::Clear(cl)).await?,
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
            if let Err(e) = &res {
                tracing::warn!("focus sid={} failed: {e:?}", session.sid);
            }
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
        CmdOp::Approve | CmdOp::Deny | CmdOp::Reply => {
            // The broker holds the hook that is waiting for this rid; an unknown or settled rid
            // is stale, so a race against the terminal answer is safe (PROTOCOL §5).
            let decision = match cmd.op {
                CmdOp::Approve => Some(Decision::Approve),
                CmdOp::Deny => Some(Decision::Deny),
                _ => match cmd.arg {
                    Some(Arg::Uint(i)) => usize::try_from(i).ok().map(Decision::Reply),
                    _ => None,
                },
            };
            let res = match (cmd.rid, decision) {
                (None, _) => fail(ResultErr::Denied, "rid required"),
                (_, None) => fail(ResultErr::Denied, "reply needs an option index"),
                (Some(_), Some(Decision::Approve))
                    if cmd.hold_ms.unwrap_or(0) < MIN_APPROVE_HOLD_MS =>
                {
                    fail(ResultErr::Denied, "hold-to-confirm too short")
                }
                (Some(rid), Some(d)) => match shared.broker.answer(rid, d) {
                    Ok(outcome) => {
                        tracing::info!("rid {rid} answered from device: {:?}", outcome);
                        CmdResult {
                            ok: true,
                            err: None,
                            msg: None,
                        }
                    }
                    Err((err, msg)) => fail(err, msg),
                },
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

#[cfg(test)]
mod tests {
    use super::*;
    use async_trait::async_trait;
    use tokio::sync::mpsc;

    struct NoFocus;
    #[async_trait]
    impl FocusDriver for NoFocus {
        async fn focus(&self, _: &FocusTarget) -> Result<(), FocusError> {
            Err(FocusError::Unsupported)
        }
    }

    struct ChanLink {
        rx: mpsc::UnboundedReceiver<Vec<u8>>,
        tx: mpsc::UnboundedSender<Vec<u8>>,
    }
    #[async_trait]
    impl Link for ChanLink {
        fn peer(&self) -> String {
            "test".into()
        }
        async fn send(&mut self, frame: Vec<u8>) -> Result<()> {
            let _ = self.tx.send(frame);
            Ok(())
        }
        async fn recv(&mut self) -> Result<Option<Vec<u8>>> {
            Ok(self.rx.recv().await)
        }
    }

    struct Dev {
        to_daemon: mpsc::UnboundedSender<Vec<u8>>,
        from_daemon: mpsc::UnboundedReceiver<Vec<u8>>,
    }
    impl Dev {
        async fn next(&mut self) -> Envelope {
            let b =
                tokio::time::timeout(std::time::Duration::from_secs(5), self.from_daemon.recv())
                    .await
                    .expect("timeout")
                    .expect("link closed");
            Envelope::from_bytes(&b).unwrap()
        }
    }

    fn hello_bytes(fw: &str) -> Vec<u8> {
        let p = format!(
            "{}/../../../protocol/vectors/hello_sim.json",
            env!("CARGO_MANIFEST_DIR")
        );
        let v: Value = serde_json::from_slice(&std::fs::read(p).unwrap()).unwrap();
        let mut env =
            Envelope::from_bytes(&hex::decode(v["cbor_hex"].as_str().unwrap()).unwrap()).unwrap();
        if let Message::Hello(h) = &mut env.msg {
            h.dev.fw = fw.into();
        }
        env.to_bytes().unwrap()
    }

    fn shared() -> Arc<Shared> {
        let trace = Trace::null();
        let registry = Arc::new(Registry::new(trace.clone()));
        Arc::new(Shared::new(
            registry.clone(),
            Arc::new(Broker::new(registry)),
            trace,
            Arc::new(NoFocus),
        ))
    }

    fn cmd_bytes(id: u64, cmd: Cmd) -> Vec<u8> {
        Envelope::new(id, Message::Cmd(cmd)).to_bytes().unwrap()
    }

    fn cmd(op: CmdOp, rid: Option<u64>, arg: Option<Arg>, hold_ms: Option<u64>) -> Cmd {
        Cmd {
            op,
            sid: Some(1),
            rid,
            arg,
            hold_ms,
        }
    }

    async fn result_of(d: &mut Dev) -> CmdResult {
        match d.next().await.msg {
            Message::Result(r) => r,
            m => panic!("expected result, got {m:?}"),
        }
    }

    /// Approve, deny and reply reach the broker and release the hook; stale rids are refused;
    /// a device that connects late is told about the open request.
    #[tokio::test]
    async fn commands_answer_the_broker() {
        use crate::broker::{Meaning, Request};
        use std::time::Duration;
        let shared = shared();
        let req = |opts: Vec<(String, Meaning)>| Request {
            sid: 1,
            key: "claude:s".into(),
            tool: Some("Bash".into()),
            kind: AttKind::Approval,
            title: Some("Run shell command".into()),
            text: Some("npm test".into()),
            opts,
            ttl: Duration::from_secs(15),
        };
        let (rid, wait) = shared.broker.open(req(vec![]));
        let mut d = spawn_link(&shared);
        d.to_daemon.send(hello_bytes("1.0.0")).unwrap();
        d.next().await; // welcome
        d.next().await; // snapshot
        let Message::Attention(att) = d.next().await.msg else {
            panic!("pending attention after snapshot")
        };
        assert_eq!(att.rid, rid);

        // too short a hold is refused and the request stays open
        d.to_daemon
            .send(cmd_bytes(
                1,
                cmd(CmdOp::Approve, Some(rid), None, Some(400)),
            ))
            .unwrap();
        let r = result_of(&mut d).await;
        assert_eq!((r.ok, r.err), (false, Some(ResultErr::Denied)));
        // a proper hold approves: result ok, hook released, clear broadcast
        d.to_daemon
            .send(cmd_bytes(
                2,
                cmd(CmdOp::Approve, Some(rid), None, Some(1250)),
            ))
            .unwrap();
        assert!(result_of(&mut d).await.ok);
        assert_eq!(wait.await.unwrap().decision, "allow");
        let Message::Clear(c) = d.next().await.msg else {
            panic!()
        };
        assert_eq!((c.rid, c.why), (rid, ClearWhy::AnsweredDev));
        // the same rid again is stale
        d.to_daemon
            .send(cmd_bytes(3, cmd(CmdOp::Deny, Some(rid), None, None)))
            .unwrap();
        assert_eq!(result_of(&mut d).await.err, Some(ResultErr::Stale));

        // reply with an option index, delivered as an attention while connected
        let (rid, wait) = shared.broker.open(req(vec![
            ("Yes".into(), Meaning::Allow),
            ("Yes, don't ask again".into(), Meaning::AllowGrant),
            ("No".into(), Meaning::Deny),
        ]));
        let Message::Attention(att) = d.next().await.msg else {
            panic!()
        };
        assert_eq!((att.rid, att.grant_mask), (rid, Some(3)));
        d.to_daemon
            .send(cmd_bytes(
                4,
                cmd(CmdOp::Reply, Some(rid), Some(Arg::Uint(1)), None),
            ))
            .unwrap();
        assert!(result_of(&mut d).await.ok);
        let out = wait.await.unwrap();
        assert_eq!((out.decision, out.grant), ("allow", true));
        let _clear = d.next().await;
        // reply without an index, and deny of an unknown rid
        d.to_daemon
            .send(cmd_bytes(5, cmd(CmdOp::Reply, Some(rid), None, None)))
            .unwrap();
        assert_eq!(result_of(&mut d).await.err, Some(ResultErr::Denied));
        d.to_daemon
            .send(cmd_bytes(6, cmd(CmdOp::Deny, Some(4242), None, None)))
            .unwrap();
        assert_eq!(result_of(&mut d).await.err, Some(ResultErr::Stale));
    }

    fn spawn_link(shared: &Arc<Shared>) -> Dev {
        let (to_daemon, rx) = mpsc::unbounded_channel();
        let (tx, from_daemon) = mpsc::unbounded_channel();
        let shared = shared.clone();
        tokio::spawn(async move {
            let _ = run_link(shared, Box::new(ChanLink { rx, tx }), "test").await;
        });
        Dev {
            to_daemon,
            from_daemon,
        }
    }

    fn fws(shared: &Shared) -> Vec<String> {
        shared
            .devices
            .lock()
            .unwrap()
            .values()
            .map(|d| d["dev"]["fw"].as_str().unwrap().to_owned())
            .collect()
    }

    #[tokio::test]
    async fn second_hello_restarts_ids_and_updates_device() {
        let shared = shared();
        let mut d = spawn_link(&shared);
        for fw in ["1.0.0", "1.0.1"] {
            d.to_daemon.send(hello_bytes(fw)).unwrap();
            let w = d.next().await;
            assert!(matches!(w.msg, Message::Welcome(_)));
            assert_eq!(w.id, 1);
            let s = d.next().await;
            assert!(matches!(s.msg, Message::Snapshot(_)));
            assert_eq!(s.id, 2);
        }
        assert_eq!(fws(&shared), ["1.0.1"]);
    }

    #[tokio::test]
    async fn reconnect_on_new_link_replaces_device_entry() {
        let shared = shared();
        let mut a = spawn_link(&shared);
        a.to_daemon.send(hello_bytes("1.0.0")).unwrap();
        a.next().await;
        a.next().await;
        let mut b = spawn_link(&shared);
        b.to_daemon.send(hello_bytes("1.0.1")).unwrap();
        b.next().await;
        b.next().await;
        assert_eq!(fws(&shared), ["1.0.1"]);

        // The stale link ending must not remove the newer entry.
        drop(a);
        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
        assert_eq!(fws(&shared), ["1.0.1"]);
        drop(b);
        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
        assert!(fws(&shared).is_empty());
    }
}
