//! One connected device: handshake (hello → welcome → snapshot), then deltas out and
//! commands in.

use crate::broker::{Broker, Decision};
use crate::focus::{FocusDriver, FocusError, FocusTarget, ScrollDriver};
use crate::registry::{now_unix, Registry, Route, Update};
use crate::scroll::{Batch, Coalescer, RESULT_MIN_GAP};
use crate::trace::{Dir, Trace};
use crate::transport::Link;
use agentlink_proto::*;
use anyhow::{bail, Result};
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use tokio::sync::broadcast::error::RecvError;
use tokio::task::JoinHandle;
use tokio::time::Instant;

/// Minimum hold-to-confirm for approvals (PROTOCOL §5).
const MIN_APPROVE_HOLD_MS: u64 = 1000;

/// State shared by every link task.
pub struct Shared {
    pub registry: Arc<Registry>,
    pub broker: Arc<Broker>,
    pub trace: Trace,
    pub focus: Arc<dyn FocusDriver>,
    pub scroll: Arc<dyn ScrollDriver>,
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
        scroll: Arc<dyn ScrollDriver>,
    ) -> Self {
        Shared {
            registry,
            broker,
            trace,
            focus,
            scroll,
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
    /// The privacy setting this link's welcome gave the device: gates the scroll preview.
    privacy: Privacy,
    /// Protocol minor the device announced in its hello.
    peer_minor: u64,
}

/// Terminal content leaves the daemon only when the device may show it (UI-V2 §4.4).
fn preview_allowed(privacy: Privacy) -> bool {
    privacy != Privacy::Meta
}

/// A scroll batch running against the terminal, and the result throttle (UI-V2 §6.1: at most
/// one `result` per 250 ms per device, only when the position changed or the command failed).
type Inflight = JoinHandle<(Batch, std::result::Result<ScrollPos, FocusError>)>;

#[derive(Default)]
struct ScrollState {
    co: Coalescer,
    last_pos: Option<ScrollPos>,
    last_sent: Option<Instant>,
    /// A changed position waiting for the throttle window: (re, result, due).
    deferred: Option<(u64, CmdResult, Instant)>,
}

impl ScrollState {
    fn start(shared: &Shared, privacy: Privacy, target: FocusTarget, b: Batch) -> Inflight {
        let driver = shared.scroll.clone();
        let preview = preview_allowed(privacy);
        tokio::spawn(async move {
            let r = driver.scroll(&target, b.unit, b.delta, preview).await;
            (b, r)
        })
    }

    fn deferred_due(&self) -> Instant {
        self.deferred
            .as_ref()
            .map(|d| d.2)
            .unwrap_or_else(Instant::now)
    }
}

impl Conn {
    /// Devices reject unknown keys: `hn` needs proto 0.2, `snz`/`ntf`/`n`/`ok` 0.3. The step
    /// summary only goes to a device whose privacy allows content.
    fn for_peer(&self, mut s: Session) -> Session {
        if self.peer_minor < 2 {
            s.hn = None;
        }
        if self.peer_minor < 3 {
            (s.snz, s.ntf, s.n, s.ok) = (None, None, None, None);
        }
        if self.privacy == Privacy::Meta {
            s.sum = None;
        }
        s
    }

    fn sessions_for_peer(&self, v: Vec<Session>) -> Vec<Session> {
        v.into_iter().map(|s| self.for_peer(s)).collect()
    }

    async fn send(&mut self, re: Option<u64>, msg: Message) -> Result<()> {
        let env = Envelope {
            id: self.next_id,
            re,
            msg,
        };
        self.next_id += 1;
        let bytes = env.to_bytes()?;
        let private = matches!(&env.msg, Message::Result(r) if r.pos.as_ref().is_some_and(|p| p.preview.is_some()));
        if private {
            self.trace.proto_private(&self.src, Dir::Out, &env);
        } else {
            self.trace.proto(&self.src, Dir::Out, &env, &bytes);
        }
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
        privacy: Privacy::Meta,
        peer_minor: 0,
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
    c.peer_minor = hello.1.proto.1;

    // Subscribe before taking the snapshot so no update is lost in between.
    let (sessions, updates) = shared.registry.subscribe();
    let w = welcome(&shared.host);
    c.privacy = w.cfg.privacy.unwrap_or(Privacy::Meta);
    c.send(Some(hello.0), Message::Welcome(w)).await?;
    c.send(
        None,
        Message::Snapshot(Snapshot {
            sessions: c.sessions_for_peer(sessions),
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
    let mut sc = ScrollState::default();
    // The running scroll batch lives outside `sc` so the select! branches borrow separate things.
    let mut inflight: Option<Inflight> = None;
    loop {
        let deferred_due = sc.deferred_due();
        tokio::select! {
            env = c.recv() => match env? {
                None => return Ok(()),
                Some(Envelope { id, msg: Message::Hello(h), .. }) => {
                    shared.trace.record(&c.src, Dir::Internal, "rehandshake", None, None, json!({"peer": peer}), None);
                    *updates = handshake(shared, c, (id, h), link_id, transport, peer, dev_key).await?;
                    tracing::info!("device re-handshake: {peer} ({transport})");
                }
                Some(env) => handle(shared, c, env, &mut sc, &mut inflight).await?,
            },
            done = async { inflight.as_mut().expect("guarded by the branch condition").await }, if inflight.is_some() => {
                inflight = None;
                let (b, res) = done?;
                scroll_done(shared, c, &mut sc, &mut inflight, b, res).await?;
            },
            _ = tokio::time::sleep_until(deferred_due), if sc.deferred.is_some() => {
                if let Some((re, r, _)) = sc.deferred.take() {
                    sc.last_sent = Some(Instant::now());
                    c.send(Some(re), Message::Result(r)).await?;
                }
            },
            u = updates.recv() => match u {
                Ok(Update::Upsert(s)) => c.send(None, Message::SessionUpd(c.for_peer(s))).await?,
                Ok(Update::Gone(sid)) => c.send(None, Message::Gone(Gone { sid })).await?,
                Ok(Update::Attention(a)) => c.send(None, Message::Attention(a)).await?,
                Ok(Update::Clear(cl)) => c.send(None, Message::Clear(cl)).await?,
                // Missed updates: the device has drifted, so send a full replacement.
                Err(RecvError::Lagged(_)) => {
                    let sessions = c.sessions_for_peer(shared.registry.snapshot());
                    c.send(None, Message::Snapshot(Snapshot { sessions, away: None })).await?;
                    for att in shared.broker.pending() {
                        c.send(None, Message::Attention(att)).await?;
                    }
                }
                Err(RecvError::Closed) => return Ok(()),
            },
        }
    }
}

async fn handle(
    shared: &Shared,
    c: &mut Conn,
    env: Envelope,
    sc: &mut ScrollState,
    inflight: &mut Option<Inflight>,
) -> Result<()> {
    match env.msg {
        Message::Cmd(cmd) if cmd.op == CmdOp::Scroll => {
            scroll_cmd(shared, c, sc, inflight, env.id, &cmd).await
        }
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
        pos: None,
    }
}

/// The batch a scroll command asks for, or the result that refuses it.
fn scroll_batch(id: u64, cmd: &Cmd) -> std::result::Result<Batch, CmdResult> {
    let Some(sid) = cmd.sid else {
        return Err(fail(ResultErr::Denied, "sid required"));
    };
    let Some(Arg::Scroll(a)) = &cmd.arg else {
        return Err(fail(ResultErr::Denied, "scroll needs a scroll_arg"));
    };
    Ok(Batch {
        sid,
        unit: a.unit,
        delta: a.delta,
        re: id,
        n: 1,
    })
}

/// `cmd_op_scroll`: read-only on the PC, so no audit line; one trace line per batch, never
/// per command, and never the preview (UI-V2 §6.2).
async fn scroll_cmd(
    shared: &Shared,
    c: &mut Conn,
    sc: &mut ScrollState,
    inflight: &mut Option<Inflight>,
    id: u64,
    cmd: &Cmd,
) -> Result<()> {
    let b = match scroll_batch(id, cmd) {
        Ok(b) => b,
        Err(r) => return c.send(Some(id), Message::Result(r)).await,
    };
    let (session, ctx) = match shared.registry.route(b.sid) {
        Route::Ready(s, ctx) => (s, ctx),
        Route::Unknown => {
            let r = fail(ResultErr::Stale, "unknown session");
            return c.send(Some(id), Message::Result(r)).await;
        }
        Route::Unsupported => {
            let r = fail(ResultErr::Unsupported, "scroll unsupported here");
            return c.send(Some(id), Message::Result(r)).await;
        }
    };
    if let Some(b) = sc.co.push(b) {
        let target = FocusTarget {
            sid: session.sid,
            agent: session.ag,
            ctx,
        };
        *inflight = Some(ScrollState::start(shared, c.privacy, target, b));
    }
    Ok(())
}

async fn scroll_done(
    shared: &Shared,
    c: &mut Conn,
    sc: &mut ScrollState,
    inflight: &mut Option<Inflight>,
    b: Batch,
    res: std::result::Result<ScrollPos, FocusError>,
) -> Result<()> {
    let now = Instant::now();
    let mut line = json!({"unit": b.unit, "delta": b.delta, "cmds": b.n, "ok": res.is_ok()});
    let out = match res {
        Ok(pos) => {
            line["lines_above"] = pos.lines_above.into();
            line["at_bottom"] = pos.at_bottom.into();
            let changed = sc.last_pos.as_ref() != Some(&pos);
            sc.last_pos = Some(pos.clone());
            changed.then(|| CmdResult {
                pos: Some(pos),
                ..CmdResult::ok()
            })
        }
        Err(e) => {
            tracing::warn!("scroll sid={} failed: {e:?}", b.sid);
            line["err"] = format!("{e:?}").into();
            sc.deferred = None; // a stale position after a failure would mislead
            Some(match e {
                FocusError::NoWindow => fail(ResultErr::NoWindow, "no window found"),
                FocusError::Unsupported => fail(ResultErr::Unsupported, "scroll unsupported here"),
                FocusError::Internal(m) => fail(ResultErr::Internal, &m),
            })
        }
    };
    shared.trace.record(
        &c.src,
        Dir::Internal,
        "scroll",
        Some(b.sid),
        None,
        line,
        None,
    );
    if let Some(r) = out {
        let due = sc
            .last_sent
            .map(|t| t + RESULT_MIN_GAP)
            .filter(|d| *d > now);
        match due {
            Some(d) if r.ok => sc.deferred = Some((b.re, r, d)), // the newest position wins
            _ => {
                sc.deferred = None;
                sc.last_sent = Some(now);
                c.send(Some(b.re), Message::Result(r)).await?;
            }
        }
    }
    if let Some(next) = sc.co.done() {
        match shared.registry.route(next.sid) {
            Route::Ready(session, ctx) => {
                *inflight = Some(ScrollState::start(
                    shared,
                    c.privacy,
                    FocusTarget {
                        sid: session.sid,
                        agent: session.ag,
                        ctx,
                    },
                    next,
                ))
            }
            Route::Unknown | Route::Unsupported => {
                // the session ended (or lost its window) mid-burst: drop what was queued for it
                while sc.co.done().is_some() {}
            }
        }
    }
    Ok(())
}

async fn handle_cmd(shared: &Shared, cmd: &Cmd) -> CmdResult {
    let detail = serde_json::to_value(cmd).unwrap_or_default();
    match cmd.op {
        CmdOp::Focus => {
            let route = cmd
                .sid
                .map_or(Route::Unknown, |sid| shared.registry.route(sid));
            let (session, ctx) = match route {
                Route::Ready(s, ctx) => (s, ctx),
                Route::Unknown => return fail(ResultErr::Stale, "unknown session"),
                Route::Unsupported => {
                    return fail(ResultErr::Unsupported, "focus unsupported here")
                }
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
                Ok(()) => CmdResult::ok(),
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
                (Some(rid), Some(ref d))
                    if shared.broker.grants(rid, d)
                        && cmd.hold_ms.unwrap_or(0) < MIN_APPROVE_HOLD_MS =>
                {
                    fail(ResultErr::Denied, "hold-to-confirm too short")
                }
                (Some(rid), Some(d)) => match shared.broker.answer(rid, d) {
                    Ok(outcome) => {
                        tracing::info!("rid {rid} answered from device: {:?}", outcome);
                        CmdResult::ok()
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
        CmdOp::Ack | CmdOp::Snooze | CmdOp::NotifyDone => {
            let reg = &shared.registry;
            let arg = match cmd.arg {
                Some(Arg::Uint(n)) => Some(n),
                _ => None,
            };
            let done = cmd.sid.and_then(|sid| match cmd.op {
                CmdOp::Ack => reg.ack(sid),
                CmdOp::Snooze => reg.snooze(sid, arg.unwrap_or(30)),
                _ => reg.notify_done(sid, arg != Some(0)),
            });
            match done {
                Some(()) => CmdResult::ok(),
                None => fail(ResultErr::Stale, "unknown session"),
            }
        }
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

    /// Records every scroll call, takes 60 ms each (so a burst piles up behind it) and keeps a
    /// position of its own: `lines_above` grows by the lines scrolled up.
    #[derive(Default)]
    struct FakeScroll {
        calls: Mutex<Vec<(u64, ScrollUnit, i64, bool)>>,
        lines_above: Mutex<i64>,
        unsupported: bool,
    }
    #[async_trait]
    impl ScrollDriver for FakeScroll {
        async fn scroll(
            &self,
            t: &FocusTarget,
            unit: ScrollUnit,
            delta: i64,
            preview: bool,
        ) -> Result<ScrollPos, FocusError> {
            self.calls
                .lock()
                .unwrap()
                .push((t.sid, unit, delta, preview));
            tokio::time::sleep(std::time::Duration::from_millis(60)).await;
            if self.unsupported {
                return Err(FocusError::Unsupported);
            }
            let mut la = self.lines_above.lock().unwrap();
            *la = match unit {
                ScrollUnit::Lines => (*la - delta).max(0),
                ScrollUnit::Pages => (*la - delta * 40).max(0),
                ScrollUnit::ToEnd => 0,
            };
            Ok(ScrollPos {
                lines_above: *la as u64,
                at_bottom: *la == 0,
                total: Some(1200),
                preview: preview.then(|| "$ npm test".into()),
            })
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
        hello_with_minor(fw, 1)
    }

    fn hello_with_minor(fw: &str, minor: u64) -> Vec<u8> {
        let p = format!(
            "{}/../../../protocol/vectors/hello_sim.json",
            env!("CARGO_MANIFEST_DIR")
        );
        let v: Value = serde_json::from_slice(&std::fs::read(p).unwrap()).unwrap();
        let mut env =
            Envelope::from_bytes(&hex::decode(v["cbor_hex"].as_str().unwrap()).unwrap()).unwrap();
        if let Message::Hello(h) = &mut env.msg {
            h.dev.fw = fw.into();
            h.proto = Version(0, minor);
        }
        env.to_bytes().unwrap()
    }

    fn shared() -> Arc<Shared> {
        shared_with(Arc::new(FakeScroll::default()))
    }

    fn shared_with(scroll: Arc<dyn ScrollDriver>) -> Arc<Shared> {
        let trace = Trace::null();
        let registry = Arc::new(Registry::new(trace.clone()));
        Arc::new(Shared::new(
            registry.clone(),
            Arc::new(Broker::new(registry)),
            trace,
            Arc::new(NoFocus),
            scroll,
        ))
    }

    fn scroll_cmd(sid: u64, unit: ScrollUnit, delta: i64, seq: u64) -> Cmd {
        Cmd {
            op: CmdOp::Scroll,
            sid: Some(sid),
            rid: None,
            arg: Some(Arg::Scroll(ScrollArg {
                unit,
                delta,
                seq: Some(seq),
            })),
            hold_ms: None,
        }
    }

    fn add_session(shared: &Shared) -> u64 {
        use crate::registry::{SessionEvent, SessionPatch};
        let mut p = SessionPatch::new(Agent::Claude);
        p.state = Some(SessState::Work);
        shared.registry.apply(SessionEvent::Update {
            key: "claude:s".into(),
            via: crate::registry::Via::Local,
            patch: p,
            ctx: crate::registry::HostCtx::default(),
        });
        shared.registry.sid_of("claude:s").unwrap()
    }

    /// A burst of scroll commands is summed behind the running one, each batch answers with the
    /// position (only when it changed), the preview rides along under the welcome's privacy, and
    /// a terminal without a scroll API answers `unsupported`.
    #[tokio::test]
    async fn scroll_bursts_are_coalesced_and_answered_with_positions() {
        let drv = Arc::new(FakeScroll::default());
        let shared = shared_with(drv.clone());
        let sid = add_session(&shared);
        let mut d = spawn_link(&shared);
        d.to_daemon.send(hello_bytes("1.0.0")).unwrap();
        d.next().await; // welcome
        d.next().await; // snapshot (the session is in it)

        for i in 0..5u64 {
            d.to_daemon
                .send(cmd_bytes(10 + i, scroll_cmd(sid, ScrollUnit::Lines, -3, i)))
                .unwrap();
        }
        // first batch: the first command alone; second batch: the other four summed
        let r1 = d.next().await;
        assert_eq!(r1.re, Some(10));
        let Message::Result(r) = r1.msg else { panic!() };
        assert_eq!(
            (r.ok, r.pos.as_ref().map(|p| p.lines_above)),
            (true, Some(3))
        );
        let r2 = d.next().await;
        assert_eq!(
            r2.re,
            Some(14),
            "the result answers the newest command folded in"
        );
        let Message::Result(r) = r2.msg else { panic!() };
        let pos = r.pos.unwrap();
        assert_eq!(
            (pos.lines_above, pos.at_bottom, pos.total),
            (15, false, Some(1200))
        );
        assert_eq!(
            pos.preview.as_deref(),
            Some("$ npm test"),
            "welcome privacy is preview"
        );
        {
            let calls = drv.calls.lock().unwrap();
            assert_eq!(calls.len(), 2, "{calls:?}");
            assert_eq!(calls[0], (sid, ScrollUnit::Lines, -3, true));
            assert_eq!(calls[1], (sid, ScrollUnit::Lines, -12, true));
        }
        // back to the bottom: a position change, answered
        d.to_daemon
            .send(cmd_bytes(20, scroll_cmd(sid, ScrollUnit::ToEnd, 1, 9)))
            .unwrap();
        let Message::Result(r) = d.next().await.msg else {
            panic!()
        };
        assert!(r.pos.unwrap().at_bottom);
        // scrolling down while live changes nothing: no result at all
        d.to_daemon
            .send(cmd_bytes(21, scroll_cmd(sid, ScrollUnit::Lines, 3, 10)))
            .unwrap();
        let quiet =
            tokio::time::timeout(std::time::Duration::from_millis(400), d.from_daemon.recv()).await;
        assert!(quiet.is_err(), "unchanged position must not be answered");
        // unknown session and a malformed argument are refused at once
        d.to_daemon
            .send(cmd_bytes(22, scroll_cmd(4242, ScrollUnit::Lines, -3, 11)))
            .unwrap();
        assert_eq!(result_of(&mut d).await.err, Some(ResultErr::Stale));
        d.to_daemon
            .send(cmd_bytes(
                23,
                cmd(CmdOp::Scroll, None, Some(Arg::Uint(3)), None),
            ))
            .unwrap();
        assert_eq!(result_of(&mut d).await.err, Some(ResultErr::Denied));
    }

    #[tokio::test]
    async fn scroll_on_an_unsupported_terminal_fails_fast() {
        let drv = Arc::new(FakeScroll {
            unsupported: true,
            ..Default::default()
        });
        let shared = shared_with(drv);
        let sid = add_session(&shared);
        let mut d = spawn_link(&shared);
        d.to_daemon.send(hello_bytes("1.0.0")).unwrap();
        d.next().await;
        d.next().await;
        d.to_daemon
            .send(cmd_bytes(10, scroll_cmd(sid, ScrollUnit::Pages, -1, 0)))
            .unwrap();
        let r = result_of(&mut d).await;
        assert_eq!(
            (r.ok, r.err, r.pos),
            (false, Some(ResultErr::Unsupported), None)
        );
    }

    #[test]
    fn preview_needs_privacy_above_meta() {
        assert!(!preview_allowed(Privacy::Meta));
        assert!(preview_allowed(Privacy::Preview));
        assert!(preview_allowed(Privacy::Full));
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
            focus_only: false,
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
        // a granting option needs the full hold too, even though it travels as a reply
        for short in [None, Some(400)] {
            d.to_daemon
                .send(cmd_bytes(
                    4,
                    cmd(CmdOp::Reply, Some(rid), Some(Arg::Uint(1)), short),
                ))
                .unwrap();
            let r = result_of(&mut d).await;
            assert_eq!((r.ok, r.err), (false, Some(ResultErr::Denied)));
        }
        d.to_daemon
            .send(cmd_bytes(
                4,
                cmd(CmdOp::Reply, Some(rid), Some(Arg::Uint(1)), Some(1250)),
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

    fn add_remote_session(shared: &Shared) -> u64 {
        use crate::registry::{SessionEvent, SessionPatch, Via};
        let via = Via::Remote {
            host: "WIN-BUILD01".into(),
            os: "win".into(),
            attach: None,
        };
        let mut p = SessionPatch::new(Agent::Copilot);
        p.state = Some(SessState::Work);
        let key = via.key("copilot:r");
        shared.registry.apply(SessionEvent::Update {
            key: key.clone(),
            via,
            patch: p,
            ctx: crate::registry::HostCtx::default(),
        });
        shared.registry.sid_of(&key).unwrap()
    }

    #[tokio::test]
    async fn host_name_only_reaches_devices_speaking_proto_0_2() {
        let shared = shared();
        let sid = add_remote_session(&shared);
        for (minor, want) in [(1, None), (2, Some("WIN-BUILD01"))] {
            let mut d = spawn_link(&shared);
            d.to_daemon.send(hello_with_minor("1.0.0", minor)).unwrap();
            d.next().await;
            let Message::Snapshot(s) = d.next().await.msg else {
                panic!("snapshot")
            };
            assert_eq!(
                (s.sessions[0].host, s.sessions[0].hn.as_deref()),
                (Some(1), want)
            );

            let mut upd = Session::delta(sid);
            upd.hn = Some("WIN-BUILD01".into());
            shared.registry.publish(Update::Upsert(upd));
            let Message::SessionUpd(u) = d.next().await.msg else {
                panic!("session_upd")
            };
            assert_eq!(u.hn.as_deref(), want);
        }
    }

    #[test]
    fn for_peer_strips_keys_the_device_cannot_take() {
        let (_tx, rx) = mpsc::unbounded_channel();
        let (tx, _rx) = mpsc::unbounded_channel();
        let mut c = Conn {
            link: Box::new(ChanLink { rx, tx }),
            trace: Trace::null(),
            src: "t".into(),
            next_id: 1,
            privacy: Privacy::Preview,
            peer_minor: 2,
        };
        let mut s = Session::delta(1);
        (s.snz, s.ntf, s.n, s.ok, s.sum) = (
            Some(5),
            Some(true),
            Some(2),
            Some(true),
            Some("bash · ls".into()),
        );
        let out = c.for_peer(s.clone());
        assert_eq!((out.snz, out.ntf, out.n, out.ok), (None, None, None, None));
        assert_eq!(out.sum.as_deref(), Some("bash · ls"));
        c.peer_minor = 3;
        let out = c.for_peer(s.clone());
        assert_eq!((out.snz, out.n, out.ok), (Some(5), Some(2), Some(true)));
        c.privacy = Privacy::Meta;
        assert_eq!(c.for_peer(s).sum, None);
    }

    #[tokio::test]
    async fn ack_snooze_and_notify_commands_edit_the_session() {
        let shared = shared();
        let sid = add_remote_session(&shared);
        let mut d = spawn_link(&shared);
        d.to_daemon.send(hello_with_minor("1.0.0", 3)).unwrap();
        d.next().await; // welcome
        d.next().await; // snapshot
        let mut id = 1;
        for (op, arg) in [
            (CmdOp::Snooze, Some(Arg::Uint(0))),
            (CmdOp::NotifyDone, None),
            (CmdOp::NotifyDone, Some(Arg::Uint(0))),
            (CmdOp::Ack, None),
        ] {
            let c = Cmd {
                sid: Some(sid),
                ..cmd(op, None, arg, None)
            };
            d.to_daemon.send(cmd_bytes(id, c)).unwrap();
            id += 1;
            let mut ok = false;
            let mut upd = None;
            for _ in 0..2 {
                match d.next().await.msg {
                    Message::Result(r) => ok = r.ok,
                    Message::SessionUpd(u) => upd = Some(u),
                    m => panic!("{m:?}"),
                }
            }
            assert!(ok);
            let u = upd.unwrap();
            match op {
                CmdOp::Snooze => assert_eq!(u.snz, Some(0)),
                CmdOp::NotifyDone => assert_eq!(u.ntf, Some(id == 3)),
                _ => assert_eq!(u.seen, Some(true)),
            }
        }
        let c = Cmd {
            sid: Some(99),
            ..cmd(CmdOp::Ack, None, None, None)
        };
        d.to_daemon.send(cmd_bytes(id, c)).unwrap();
        assert_eq!(result_of(&mut d).await.err, Some(ResultErr::Stale));
    }

    #[tokio::test]
    async fn focus_on_a_remote_session_without_attach_never_reaches_the_driver() {
        struct Counting(Mutex<u32>);
        #[async_trait]
        impl FocusDriver for Counting {
            async fn focus(&self, _: &FocusTarget) -> Result<(), FocusError> {
                *self.0.lock().unwrap() += 1;
                Ok(())
            }
        }
        let drv = Arc::new(Counting(Mutex::new(0)));
        let trace = Trace::null();
        let registry = Arc::new(Registry::new(trace.clone()));
        let shared = Arc::new(Shared::new(
            registry.clone(),
            Arc::new(Broker::new(registry)),
            trace,
            drv.clone(),
            Arc::new(FakeScroll::default()),
        ));
        let remote = add_remote_session(&shared);
        let local = add_session(&shared);
        let c = |sid| Cmd {
            sid: Some(sid),
            ..cmd(CmdOp::Focus, None, None, None)
        };
        let r = handle_cmd(&shared, &c(remote)).await;
        assert_eq!((r.ok, r.err), (false, Some(ResultErr::Unsupported)));
        assert_eq!(*drv.0.lock().unwrap(), 0);
        assert!(handle_cmd(&shared, &c(local)).await.ok);
        assert_eq!(*drv.0.lock().unwrap(), 1);
    }
}
