//! One connected device: handshake (hello → welcome → snapshot), then deltas out and
//! commands in.

use crate::broker::{Broker, Decision};
use crate::deltas::DeltaQueue;
use crate::focus::{FocusDriver, FocusError, FocusTarget, ScrollDriver, SystemTerminal, Terminal};
use crate::registry::{now_unix, Registry, Route, Update};
use crate::scroll::{Batch, Coalescer, RESULT_MIN_GAP};
use crate::trace::{Dir, Trace};
use crate::transport::Link;
use agentlink_proto::*;
use anyhow::{bail, Result};
use serde_json::{json, Value};
use std::collections::{BTreeMap, HashMap, HashSet};
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
    /// The agent's terminal as a keyboard, for quick replies and interrupt (typing.rs).
    pub term: Arc<dyn Terminal>,
    /// Quick replies offered in the welcome; `CmdOp::Type` names one by index.
    pub replies: Vec<String>,
    /// Speech to text for voice dictation (voice.rs); None when whisper.cpp is not installed.
    pub stt: Option<crate::voice::Stt>,
    /// Transcripts waiting for the user's hold (`CmdOp::Dictate`), by recording id.
    pub transcripts: Mutex<crate::voice::Transcripts>,
    pub host: HostInfo,
    /// Terminal commands shown as sessions (jobs.rs).
    pub jobs: Arc<crate::jobs::Jobs>,
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
            jobs: Arc::new(crate::jobs::Jobs::new(
                registry.clone(),
                crate::jobs::Config::from_env(),
            )),
            registry,
            broker,
            trace,
            focus,
            scroll,
            term: Arc::new(SystemTerminal),
            replies: crate::typing::load_replies(&crate::paths::home()),
            stt: crate::voice::Stt::find(),
            transcripts: Mutex::default(),
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
    /// The device can answer a card by dictation (`Action::Dictate`).
    dictate: bool,
    /// `session_upd` deltas waiting for the coalescing window.
    deltas: DeltaQueue,
    /// Job sessions (proto 0.9) withheld from a device that cannot decode them.
    hidden: HashSet<u64>,
    /// Counts this device as linked for the answer window (window.rs) once it has handshaken.
    linked: Option<crate::broker::Linked>,
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
        // the closing question and its answers: proto 0.5, and agent content like the summary
        if self.peer_minor < ASK_MINOR || self.privacy == Privacy::Meta {
            (s.q, s.rp) = (None, None);
        }
        // a job's estimate and exit status: proto 0.9
        if self.peer_minor < JOBS_MINOR {
            (s.eta, s.exit) = (None, None);
        }
        // context fill, model, permission mode, pull request: proto 0.10 (GAUGES.md)
        if self.peer_minor < GAUGES_MINOR {
            (s.ctx, s.mdl, s.pm, s.pr, s.prs) = (None, None, None, None, None);
        }
        if self.privacy == Privacy::Meta {
            s.sum = None;
            // which pull request a session works on says what it works on
            (s.pr, s.prs) = (None, None);
        }
        s
    }

    /// The plan's usage windows, to a device that can decode them (proto 0.10).
    async fn send_usage(&mut self, u: Option<Usage>) -> Result<()> {
        match u {
            Some(u) if self.peer_minor >= GAUGES_MINOR => self.send(None, Message::Usage(u)).await,
            _ => Ok(()),
        }
    }

    /// A full replacement: the job sessions an old device cannot decode are left out.
    fn sessions_for_peer(&mut self, v: Vec<Session>) -> Vec<Session> {
        self.hidden.clear();
        let old = self.peer_minor < JOBS_MINOR;
        let mut out = Vec::with_capacity(v.len());
        for s in v {
            if old && s.ag == Some(Agent::Shell) {
                self.hidden.insert(s.sid);
            } else {
                out.push(self.for_peer(s));
            }
        }
        out
    }

    /// Whether a delta or `gone` for `sid` is withheld (it names a job session this device
    /// cannot decode: its first full object carries `ag`, later deltas do not).
    fn withheld(&mut self, s: &Session) -> bool {
        if self.peer_minor >= JOBS_MINOR {
            return false;
        }
        if s.ag == Some(Agent::Shell) {
            self.hidden.insert(s.sid);
        }
        self.hidden.contains(&s.sid)
    }

    /// Sends `msg`, after the held deltas so the device sees everything in order.
    async fn send(&mut self, re: Option<u64>, mut msg: Message) -> Result<()> {
        if let Message::Attention(a) = &mut msg {
            if !self.dictate {
                a.acts.retain(|x| *x != Action::Dictate);
            }
            // the answer window and the full text's length: proto 0.11 (ANSWER-WINDOW.md)
            if self.peer_minor < WINDOW_MINOR {
                (a.left, a.win, a.len) = (None, None, None);
            }
            // no full text for a device that may not show content
            if !preview_allowed(self.privacy) {
                a.len = None;
            }
        }
        if let Message::Clear(cl) = &mut msg {
            if cl.why == ClearWhy::HandedBack && self.peer_minor < WINDOW_MINOR {
                cl.why = ClearWhy::Cancelled;
            }
        }
        if !self.deltas.is_empty() {
            for s in self.deltas.drain() {
                self.send_now(None, Message::SessionUpd(s)).await?;
            }
        }
        self.send_now(re, msg).await
    }

    /// Holds a session delta for coalescing; sends what it cannot be merged with.
    async fn send_delta(&mut self, s: Session) -> Result<()> {
        if self.withheld(&s) {
            return Ok(());
        }
        let s = self.for_peer(s);
        if s == Session::delta(s.sid) {
            return Ok(()); // every key it had is one this device does not take
        }
        match self.deltas.push(s, Instant::now()) {
            Some(out) => self.send_now(None, Message::SessionUpd(out)).await,
            None => Ok(()),
        }
    }

    async fn send_due_deltas(&mut self) -> Result<()> {
        for s in self.deltas.take_due(Instant::now()) {
            self.send_now(None, Message::SessionUpd(s)).await?;
        }
        Ok(())
    }

    async fn send_now(&mut self, re: Option<u64>, msg: Message) -> Result<()> {
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

/// Welcome keys a device must know before it is sent them: `replies` arrived in proto 0.4, and an
/// older decoder would reject the whole message.
const REPLIES_MINOR: u64 = 4;
/// `session.q` / `session.rp` (the agent's closing question for the Reply list) need proto 0.5.
const ASK_MINOR: u64 = 5;
/// `cmd type` arguments from this value up name the session's own answers (`session.rp`); below
/// it, the welcome's fixed replies.
pub const SESSION_REPLY_BASE: u64 = 100;

/// `welcome.voice` and the audio messages need proto 0.6.
const VOICE_MINOR: u64 = 6;

/// `welcome.keys` and the keyboard messages need proto 0.7 and the device's `feature_keys`.
const KEYS_MINOR: u64 = 7;

/// `action_dictate` on an attention, and `dictate` naming its `rid`, need proto 0.8.
const DICTATE_MINOR: u64 = 8;

/// Job sessions (`ag` = shell), `session.eta` / `session.exit` and `cmd rerun` need proto 0.9.
const JOBS_MINOR: u64 = 9;

/// `session.ctx` / `mdl` / `pm` / `pr` / `prs` and the `usage` message need proto 0.10 (GAUGES.md).
const GAUGES_MINOR: u64 = 10;

/// `attention.left` / `win` / `len`, `detail` chunks, `clear` handed_back and the hand-back by
/// `focus` with a rid need proto 0.11 (ANSWER-WINDOW.md).
const WINDOW_MINOR: u64 = 11;

fn keys_offered(minor: u64, feats: &[Feature]) -> Option<bool> {
    (minor >= KEYS_MINOR && feats.contains(&Feature::Keys)).then_some(true)
}

fn welcome(
    host: &HostInfo,
    replies: Option<Vec<String>>,
    voice: Option<bool>,
    keys: Option<bool>,
) -> Welcome {
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
        replies,
        voice,
        keys,
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
        dictate: false,
        deltas: DeltaQueue::default(),
        hidden: HashSet::new(),
        linked: None,
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
    c.linked.get_or_insert_with(|| shared.broker.link());
    c.deltas.clear(); // the snapshot below carries them
    c.hidden.clear();

    // Subscribe before taking the snapshot so no update is lost in between.
    let (sessions, updates) = shared.registry.subscribe();
    let replies = (c.peer_minor >= REPLIES_MINOR).then(|| shared.replies.clone());
    let voice = (c.peer_minor >= VOICE_MINOR && shared.stt.is_some()).then_some(true);
    let keys = keys_offered(c.peer_minor, &hello.1.caps.feats);
    c.dictate = voice.is_some()
        && c.peer_minor >= DICTATE_MINOR
        && hello.1.caps.feats.contains(&Feature::Voice);
    let w = welcome(&shared.host, replies, voice, keys);
    c.privacy = w.cfg.privacy.unwrap_or(Privacy::Meta);
    c.send(Some(hello.0), Message::Welcome(w)).await?;
    let sessions = c.sessions_for_peer(sessions);
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
    c.send_usage(shared.registry.usage()).await?;

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
    let (vtx, mut vrx) = tokio::sync::mpsc::unbounded_channel();
    let mut vs = VoiceState {
        recs: HashMap::new(),
        done: vtx,
        keys: crate::keys::KeysLink::default(),
    };
    loop {
        let deferred_due = sc.deferred_due();
        let deltas_due = c.deltas.next_due();
        tokio::select! {
            env = c.recv() => match env? {
                None => return Ok(()),
                Some(Envelope { id, msg: Message::Hello(h), .. }) => {
                    shared.trace.record(&c.src, Dir::Internal, "rehandshake", None, None, json!({"peer": peer}), None);
                    *updates = handshake(shared, c, (id, h), link_id, transport, peer, dev_key).await?;
                    tracing::info!("device re-handshake: {peer} ({transport})");
                }
                Some(env) => handle(shared, c, env, &mut sc, &mut inflight, &mut vs).await?,
            },
            Some((aid, sid, text)) = vrx.recv() => {
                // the transcript waits for the user's hold; the device shows it meanwhile
                shared.transcripts.lock().unwrap().put(aid, sid, text.clone());
                c.send(None, Message::Transcript(Transcript { aid, text, autosend_ms: 0 })).await?;
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
            _ = tokio::time::sleep_until(deltas_due.unwrap_or_else(Instant::now)), if deltas_due.is_some() => {
                c.send_due_deltas().await?;
            },
            u = updates.recv() => match u {
                Ok(Update::Upsert(s)) => c.send_delta(s).await?,
                Ok(Update::Gone(sid)) => {
                    if !c.hidden.remove(&sid) {
                        c.send(None, Message::Gone(Gone { sid })).await?
                    }
                }
                Ok(Update::Attention(a)) => c.send(None, Message::Attention(a)).await?,
                Ok(Update::Clear(cl)) => c.send(None, Message::Clear(cl)).await?,
                Ok(Update::Usage(u)) => c.send_usage(Some(u)).await?,
                // Missed updates: the device has drifted, so send a full replacement.
                Err(RecvError::Lagged(_)) => {
                    c.deltas.clear();
                    let sessions = c.sessions_for_peer(shared.registry.snapshot());
                    c.send(None, Message::Snapshot(Snapshot { sessions, away: None })).await?;
                    for att in shared.broker.pending() {
                        c.send(None, Message::Attention(att)).await?;
                    }
                    c.send_usage(shared.registry.usage()).await?;
                }
                Err(RecvError::Closed) => return Ok(()),
            },
        }
    }
}

/// Recordings being received on one link, and where finished transcripts come back (voice.rs).
struct VoiceState {
    recs: HashMap<u64, crate::voice::Recording>,
    done: tokio::sync::mpsc::UnboundedSender<(u64, Option<u64>, String)>,
    /// The swipe keyboard open on this link (keys.rs); dropped with it.
    keys: crate::keys::KeysLink,
}

/// The session a keyboard action types into, with the refusals of quick replies (typing.rs).
fn keys_target(
    shared: &Shared,
    sid: Option<u64>,
) -> std::result::Result<crate::keys::Target, ResultErr> {
    match sid.map_or(Route::Unknown, |sid| shared.registry.route(sid)) {
        Route::Ready(session, ctx) => {
            let refusal = crate::typing::refusal(&session, shared.broker.open_for(session.sid));
            Ok(crate::keys::Target {
                session,
                ctx,
                refusal,
            })
        }
        Route::Unknown => Err(ResultErr::Stale),
        Route::Unsupported => Err(ResultErr::Unsupported),
    }
}

async fn handle(
    shared: &Shared,
    c: &mut Conn,
    env: Envelope,
    sc: &mut ScrollState,
    inflight: &mut Option<Inflight>,
    vs: &mut VoiceState,
) -> Result<()> {
    match env.msg {
        Message::AudioStart(a) if shared.stt.is_some() => {
            vs.recs.clear(); // one recording at a time per watch
            vs.recs.insert(a.aid, crate::voice::Recording::new(a.sid));
            if let Some(stt) = &shared.stt {
                stt.warm();
            }
            Ok(())
        }
        Message::Audio(a) => {
            if let Some(r) = vs.recs.get_mut(&a.aid) {
                if !r.push(a.seq, a.data) {
                    vs.recs.remove(&a.aid); // longer than the watch ever records
                }
            }
            Ok(())
        }
        Message::AudioEnd(e) => {
            let (Some(rec), Some(stt)) = (vs.recs.remove(&e.aid), shared.stt.clone()) else {
                return Ok(());
            };
            let done = vs.done.clone();
            let dir = crate::paths::home().join("voice");
            tokio::task::spawn_blocking(move || {
                let pcm = rec.pcm();
                let t0 = Instant::now();
                let text = stt.transcribe(&pcm, &dir, e.aid).unwrap_or_else(|err| {
                    tracing::warn!("transcription failed: {err}");
                    String::new()
                });
                // the words are the user's: only their count reaches the log
                tracing::info!(
                    "recording {} ({:.1} s) transcribed in {} ms: {} words",
                    e.aid,
                    pcm.len() as f64 / crate::voice::RATE as f64,
                    t0.elapsed().as_millis(),
                    text.split_whitespace().count()
                );
                let _ = done.send((e.aid, rec.sid, text));
            });
            Ok(())
        }
        Message::KbStart(kb) => {
            // an unusable layout or session: later strokes are answered `stale`
            if let Ok(t) = keys_target(shared, Some(kb.sid)) {
                vs.keys.start(&shared.term, &t, &kb).await;
            }
            Ok(())
        }
        Message::KbStroke(kb) => {
            let reply = match keys_target(shared, vs.keys.sid_of(kb.kid)) {
                Ok(t) => vs.keys.stroke(&shared.term, &t, &kb).await,
                Err(e) => KbCands {
                    kid: kb.kid,
                    seq: kb.seq,
                    cands: Vec::new(),
                    typed: None,
                    err: Some(e),
                },
            };
            c.send(None, Message::KbCands(reply)).await
        }
        Message::Cmd(cmd)
            if matches!(
                cmd.op,
                CmdOp::KbPick | CmdOp::KbDel | CmdOp::KbSend | CmdOp::KbEnd
            ) =>
        {
            let res = match keys_target(shared, cmd.sid) {
                Ok(t) => {
                    vs.keys
                        .cmd(&shared.term, &t, cmd.op, cmd.arg, cmd.hold_ms)
                        .await
                }
                Err(e) => fail(e, "no terminal for this session"),
            };
            c.send(Some(env.id), Message::Result(res)).await
        }
        Message::Cmd(cmd) if cmd.op == CmdOp::Scroll => {
            scroll_cmd(shared, c, sc, inflight, env.id, &cmd).await
        }
        Message::Cmd(cmd) if cmd.op == CmdOp::Detail => detail_cmd(shared, c, env.id, &cmd).await,
        Message::Cmd(cmd) => {
            if cmd.op == CmdOp::Focus && c.peer_minor >= WINDOW_MINOR {
                hand_back(shared, &cmd);
            }
            let res = handle_cmd(shared, &cmd).await;
            c.send(Some(env.id), Message::Result(res)).await
        }
        Message::Ping(_) => c.send(Some(env.id), Message::Pong(Empty {})).await,
        Message::Log(l) => {
            tracing::info!(target: "device", lvl = ?l.lvl, "device: {}", l.msg);
            Ok(())
        }
        // Traced on receipt; nothing else to do yet.
        _ => Ok(()),
    }
}

/// Open on PC on a request (`focus` with its `rid`, proto 0.11): the request goes back to the terminal
/// at once. It answers nothing: the hook ends with no decision, as when the window runs out.
fn hand_back(shared: &Shared, cmd: &Cmd) {
    let Some(rid) = cmd.rid else {
        return;
    };
    let done = shared.broker.hand_back(rid, cmd.sid);
    shared
        .trace
        .audit("hand_back", cmd.sid, Some(rid), json!({"open": done}));
}

/// `cmd detail {rid, arg: off}` (proto 0.11, ANSWER-WINDOW.md §4): the full text of an open request
/// from byte `off`, as `detail` chunks of at most [`DETAIL_CHUNK`](crate::broker::DETAIL_CHUNK) bytes
/// cut on character boundaries, then one `result`. Withheld under privacy `meta`.
async fn detail_cmd(shared: &Shared, c: &mut Conn, id: u64, cmd: &Cmd) -> Result<()> {
    let off = match cmd.arg {
        None => Some(0),
        Some(Arg::Uint(o)) => usize::try_from(o).ok(),
        Some(_) => None,
    };
    let res = if c.peer_minor < WINDOW_MINOR {
        Err((ResultErr::Unsupported, "detail needs proto 0.11"))
    } else if !preview_allowed(c.privacy) {
        Err((ResultErr::Denied, "content is withheld"))
    } else {
        match (cmd.rid, off) {
            (None, _) => Err((ResultErr::Denied, "rid required")),
            (_, None) => Err((ResultErr::Denied, "bad offset")),
            (Some(rid), Some(off)) => shared.broker.detail(rid).and_then(|(sid, text)| {
                if text.is_char_boundary(off) {
                    Ok((rid, sid, text, off))
                } else {
                    Err((ResultErr::Denied, "bad offset"))
                }
            }),
        }
    };
    let res = match res {
        Ok((rid, sid, text, off)) => {
            let total = text.len() as u64;
            for (at, chunk) in chunks(&text, off, crate::broker::DETAIL_CHUNK) {
                let d = Detail {
                    rid: Some(rid),
                    sid: Some(sid),
                    text: chunk.to_owned(),
                    off: Some(at as u64),
                    total: Some(total),
                };
                c.send(None, Message::Detail(d)).await?;
            }
            CmdResult::ok()
        }
        Err((err, msg)) => fail(err, msg),
    };
    c.send(Some(id), Message::Result(res)).await
}

/// `text[off..]` in pieces of at most `max` bytes, each cut on a character boundary, with its offset.
/// An empty rest is one empty piece, so the device always hears the total.
fn chunks(text: &str, off: usize, max: usize) -> Vec<(usize, &str)> {
    let mut out = Vec::new();
    let mut at = off;
    loop {
        let mut end = (at + max).min(text.len());
        while !text.is_char_boundary(end) {
            end -= 1;
        }
        out.push((at, &text[at..end]));
        if end >= text.len() {
            return out;
        }
        at = end;
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
        CmdOp::Rerun => rerun_cmd(shared, cmd).await,
        CmdOp::Cancel if is_job(shared, cmd.sid) => cancel_job(shared, cmd).await,
        // a job is not a conversation: nothing to type a reply or a transcript into
        CmdOp::Type | CmdOp::Dictate if is_job(shared, cmd.sid) => {
            fail(ResultErr::Denied, "not for terminal jobs")
        }
        CmdOp::Ack | CmdOp::Snooze | CmdOp::NotifyDone => {
            let reg = &shared.registry;
            let arg = match cmd.arg {
                Some(Arg::Uint(n)) => Some(n),
                _ => None,
            };
            let done = cmd.sid.and_then(|sid| match cmd.op {
                // a finished job's card is removed, not just marked seen
                CmdOp::Ack if shared.jobs.dismiss(sid) => Some(()),
                CmdOp::Ack => reg.ack(sid),
                CmdOp::Snooze => reg.snooze(sid, arg.unwrap_or(30)),
                _ => reg.notify_done(sid, arg != Some(0)),
            });
            match done {
                Some(()) => CmdResult::ok(),
                None => fail(ResultErr::Stale, "unknown session"),
            }
        }
        CmdOp::Type | CmdOp::Cancel | CmdOp::Dictate => type_cmd(shared, cmd).await,
        _ => fail(ResultErr::Unsupported, "command not implemented"),
    }
}

/// A transcript answering card `rid` (`Action::Dictate`); the card's owner types it.
fn dictate_answer(shared: &Shared, detail: &Value, sid: u64, rid: u64, aid: u64) -> CmdResult {
    let res = if !shared.broker.takes_dictation(rid) {
        fail(ResultErr::Stale, "no card for that answer")
    } else {
        match shared.transcripts.lock().unwrap().take(aid, sid) {
            None => fail(ResultErr::Stale, "no transcript for that recording"),
            Some(text) => match shared.broker.answer_text(rid, text) {
                Ok(_) => {
                    tracing::info!("rid {rid} answered from device by dictation");
                    CmdResult::ok()
                }
                Err((err, msg)) => fail(err, msg),
            },
        }
    };
    shared.trace.audit(
        "approval",
        Some(sid),
        Some(rid),
        json!({"cmd": detail, "result": res}),
    );
    res
}

/// Quick reply (`Type`, arg = index into the welcome's replies) or interrupt (`Cancel`, Esc, only
/// after a hold) typed into the session's terminal (typing.rs; issue 20261110-b150).
async fn type_cmd(shared: &Shared, cmd: &Cmd) -> CmdResult {
    let detail = serde_json::to_value(cmd).unwrap_or_default();
    let route = cmd
        .sid
        .map_or(Route::Unknown, |sid| shared.registry.route(sid));
    let (session, ctx) = match route {
        Route::Ready(s, ctx) => (s, ctx),
        Route::Unknown => return fail(ResultErr::Stale, "unknown session"),
        Route::Unsupported => return fail(ResultErr::Unsupported, "no terminal for this session"),
    };
    let text = if cmd.op == CmdOp::Dictate {
        // a transcript is the user's own words, sent only after a hold, like an approval
        if cmd.hold_ms.unwrap_or(0) < MIN_APPROVE_HOLD_MS {
            return fail(ResultErr::Denied, "hold-to-confirm too short");
        }
        let aid = match cmd.arg {
            Some(Arg::Uint(a)) => a,
            _ => return fail(ResultErr::Denied, "dictate needs a recording id"),
        };
        if let Some(rid) = cmd.rid {
            // an answer to that card (a form's free-text field), not a new prompt
            return dictate_answer(shared, &detail, session.sid, rid, aid);
        }
        if let Some(why) = crate::typing::refusal(&session, shared.broker.open_for(session.sid)) {
            return fail(ResultErr::Denied, why);
        }
        let Some(text) = shared.transcripts.lock().unwrap().take(aid, session.sid) else {
            return fail(ResultErr::Stale, "no transcript for that recording");
        };
        Some(text)
    } else if cmd.op == CmdOp::Type {
        let text = match cmd.arg {
            Some(Arg::Uint(i)) if i >= SESSION_REPLY_BASE => {
                usize::try_from(i - SESSION_REPLY_BASE)
                    .ok()
                    .and_then(|j| session.rp.as_ref()?.get(j).cloned())
            }
            Some(Arg::Uint(i)) => usize::try_from(i)
                .ok()
                .and_then(|j| shared.replies.get(j).cloned()),
            _ => None,
        };
        let Some(text) = text else {
            return fail(ResultErr::Denied, "no such reply");
        };
        if let Some(why) = crate::typing::refusal(&session, shared.broker.open_for(session.sid)) {
            return fail(ResultErr::Denied, why);
        }
        Some(text)
    } else {
        if cmd.hold_ms.unwrap_or(0) < MIN_APPROVE_HOLD_MS {
            return fail(ResultErr::Denied, "hold-to-confirm too short");
        }
        None
    };
    let term = shared.term.clone();
    let res = tokio::task::spawn_blocking(move || match text {
        Some(t) => crate::typing::type_reply(term.as_ref(), &ctx, &t),
        None => crate::typing::interrupt(term.as_ref(), &ctx),
    })
    .await
    .unwrap_or_else(|e| Err(e.to_string()));
    // the reply's text is the user's own fixed list, not terminal content: the index is enough
    shared.trace.audit(
        match cmd.op {
            CmdOp::Type => "type",
            CmdOp::Dictate => "dictate",
            _ => "interrupt",
        },
        cmd.sid,
        None,
        json!({"cmd": detail, "ok": res.is_ok()}),
    );
    typing_result(res)
}

fn typing_result(res: std::result::Result<(), String>) -> CmdResult {
    match res {
        Ok(()) => CmdResult::ok(),
        Err(m) if m.contains("cannot be typed") => fail(ResultErr::Unsupported, &m),
        Err(m) if m.contains("no window") => fail(ResultErr::NoWindow, &m),
        Err(m) => fail(ResultErr::Denied, &m),
    }
}

fn is_job(shared: &Shared, sid: Option<u64>) -> bool {
    sid.and_then(|s| shared.registry.agent_of(s)) == Some(Agent::Shell)
}

/// `Cancel` on a job: Ctrl-C for the shell's terminal (SIGINT to its foreground process group),
/// only after a hold.
async fn cancel_job(shared: &Shared, cmd: &Cmd) -> CmdResult {
    if cmd.hold_ms.unwrap_or(0) < MIN_APPROVE_HOLD_MS {
        return fail(ResultErr::Denied, "hold-to-confirm too short");
    }
    let Some(pid) = cmd.sid.and_then(|sid| shared.jobs.shell_pid(sid)) else {
        return fail(ResultErr::Stale, "unknown job");
    };
    let res = tokio::task::spawn_blocking(move || crate::jobs::interrupt_foreground(pid))
        .await
        .unwrap_or_else(|e| Err(crate::jobs::CancelErr::Failed(e.to_string())));
    shared.trace.audit(
        "interrupt",
        cmd.sid,
        None,
        json!({"cmd": serde_json::to_value(cmd).unwrap_or_default(), "ok": res.is_ok()}),
    );
    use crate::jobs::CancelErr;
    match res {
        Ok(()) => CmdResult::ok(),
        Err(CancelErr::Stale) => fail(ResultErr::Stale, "nothing runs in that terminal"),
        Err(CancelErr::Unsupported) => fail(ResultErr::Unsupported, "stop unsupported here"),
        Err(CancelErr::Failed(m)) => fail(ResultErr::Internal, &m),
    }
}

/// `Rerun`: a finished job's command line typed into its terminal again, after a hold. The
/// audit line names the command like `type` does (the op and sid), never the text.
async fn rerun_cmd(shared: &Shared, cmd: &Cmd) -> CmdResult {
    use crate::jobs::RerunErr;
    if cmd.hold_ms.unwrap_or(0) < MIN_APPROVE_HOLD_MS {
        return fail(ResultErr::Denied, "hold-to-confirm too short");
    }
    let route = cmd
        .sid
        .map_or(Route::Unknown, |sid| shared.registry.route(sid));
    let (session, ctx) = match route {
        Route::Ready(s, ctx) => (s, ctx),
        Route::Unknown => return fail(ResultErr::Stale, "unknown session"),
        Route::Unsupported => return fail(ResultErr::Unsupported, "no terminal for this session"),
    };
    if session.ag != Some(Agent::Shell) {
        return fail(ResultErr::Denied, "only a finished job can run again");
    }
    let text = match shared.jobs.rerun_text(session.sid) {
        Ok(t) => t,
        Err(RerunErr::Stale) => return fail(ResultErr::Stale, "no finished job"),
        Err(RerunErr::Busy) => return fail(ResultErr::Denied, "the terminal is busy"),
        Err(RerunErr::Untypable) => {
            return fail(ResultErr::Denied, "that command cannot be typed again")
        }
    };
    let term = shared.term.clone();
    let res =
        tokio::task::spawn_blocking(move || crate::typing::type_reply(term.as_ref(), &ctx, &text))
            .await
            .unwrap_or_else(|e| Err(e.to_string()));
    shared.trace.audit(
        "rerun",
        cmd.sid,
        None,
        json!({"cmd": serde_json::to_value(cmd).unwrap_or_default(), "ok": res.is_ok()}),
    );
    typing_result(res)
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

    /// Records the keys a quick reply or interrupt types (issue 20261110-b150).
    #[derive(Default)]
    struct FakeTerm {
        sent: Mutex<Vec<String>>,
    }
    impl Terminal for FakeTerm {
        fn read_screen(&self, _: &crate::registry::HostCtx) -> Result<String, FocusError> {
            Ok("> \n".into())
        }
        fn send_keys(&self, _: &crate::registry::HostCtx, k: &str) -> Result<(), FocusError> {
            self.sent.lock().unwrap().push(k.to_owned());
            Ok(())
        }
    }

    fn shared_typing(term: Arc<FakeTerm>) -> Arc<Shared> {
        let trace = Trace::null();
        let registry = Arc::new(Registry::new(trace.clone()));
        let mut s = Shared::new(
            registry.clone(),
            Arc::new(Broker::new(registry)),
            trace,
            Arc::new(NoFocus),
            Arc::new(FakeScroll::default()),
        );
        s.term = term;
        s.replies = vec!["Continue".into(), "Run the tests".into()];
        Arc::new(s)
    }

    fn type_cmd_of(op: CmdOp, sid: u64, arg: Option<u64>, hold_ms: Option<u64>) -> Cmd {
        Cmd {
            op,
            sid: Some(sid),
            rid: None,
            arg: arg.map(Arg::Uint),
            hold_ms,
        }
    }

    #[test]
    fn keys_are_offered_only_to_devices_that_ask_for_them() {
        assert_eq!(
            keys_offered(7, &[Feature::Scroll, Feature::Keys]),
            Some(true)
        );
        assert_eq!(keys_offered(6, &[Feature::Keys]), None, "protocol too old");
        assert_eq!(
            keys_offered(7, &[Feature::Voice]),
            None,
            "feature not advertised"
        );
    }

    #[tokio::test]
    async fn quick_reply_types_the_named_reply_and_enter() {
        let term = Arc::new(FakeTerm::default());
        let shared = shared_typing(term.clone());
        let sid = add_session(&shared);
        let r = handle_cmd(&shared, &type_cmd_of(CmdOp::Type, sid, Some(1), None)).await;
        assert!(r.ok, "{r:?}");
        assert_eq!(*term.sent.lock().unwrap(), vec!["Run the tests", "\\r"]);
        let r = handle_cmd(&shared, &type_cmd_of(CmdOp::Type, sid, Some(5), None)).await;
        assert!(!r.ok && r.err == Some(ResultErr::Denied), "no such reply");
        let r = handle_cmd(&shared, &type_cmd_of(CmdOp::Type, 999, Some(0), None)).await;
        assert!(!r.ok && r.err == Some(ResultErr::Stale));
    }

    #[tokio::test]
    async fn quick_reply_from_the_sessions_own_answers() {
        use crate::registry::{SessionEvent, SessionPatch};
        let term = Arc::new(FakeTerm::default());
        let shared = shared_typing(term.clone());
        let sid = add_session(&shared);
        let mut p = SessionPatch::new(Agent::Claude);
        p.state = Some(SessState::Done);
        p.ask = Some((
            "Should I flash it?".into(),
            vec!["Yes, go ahead".into(), "No".into()],
        ));
        shared.registry.apply(SessionEvent::Update {
            key: "claude:s".into(),
            via: crate::registry::Via::Local,
            patch: p,
            ctx: crate::registry::HostCtx::default(),
        });
        let r = handle_cmd(
            &shared,
            &type_cmd_of(CmdOp::Type, sid, Some(SESSION_REPLY_BASE + 1), None),
        )
        .await;
        assert!(r.ok, "{r:?}");
        assert_eq!(*term.sent.lock().unwrap(), vec!["No", "\\r"]);
        let r = handle_cmd(
            &shared,
            &type_cmd_of(CmdOp::Type, sid, Some(SESSION_REPLY_BASE + 2), None),
        )
        .await;
        assert!(
            !r.ok && r.err == Some(ResultErr::Denied),
            "past the session's answers"
        );
    }

    #[test]
    fn the_closing_question_reaches_only_devices_that_know_it() {
        let mut s = Session::delta(1);
        s.q = Some("Should I flash it?".into());
        s.rp = Some(vec!["Yes, go ahead".into()]);
        for (minor, privacy, want) in [
            (4, Privacy::Preview, false),
            (5, Privacy::Meta, false),
            (5, Privacy::Preview, true),
        ] {
            let (_tx, rx) = mpsc::unbounded_channel();
            let (tx, _rx) = mpsc::unbounded_channel();
            let c = Conn {
                link: Box::new(ChanLink { rx, tx }),
                trace: Trace::null(),
                src: "t".into(),
                next_id: 1,
                privacy,
                peer_minor: minor,
                dictate: false,
                deltas: DeltaQueue::default(),
                hidden: HashSet::new(),
                linked: None,
            };
            let out = c.for_peer(s.clone());
            assert_eq!(
                out.q.is_some() && out.rp.is_some(),
                want,
                "minor {minor} {privacy:?}"
            );
        }
    }

    #[tokio::test]
    async fn quick_reply_is_refused_while_a_request_is_open() {
        let term = Arc::new(FakeTerm::default());
        let shared = shared_typing(term.clone());
        let sid = add_session(&shared);
        let payload = json!({"tool_name": "Bash", "tool_input": {"command": "rm -rf build"}});
        let req = crate::broker::request_of(
            sid,
            "claude:s".into(),
            "claude",
            "PermissionRequest",
            &payload,
            std::time::Duration::from_secs(15),
        )
        .unwrap();
        let (_rid, _wait) = shared.broker.open(req);
        let r = handle_cmd(&shared, &type_cmd_of(CmdOp::Type, sid, Some(0), None)).await;
        assert!(!r.ok && r.err == Some(ResultErr::Denied), "{r:?}");
        assert!(term.sent.lock().unwrap().is_empty(), "nothing typed");
    }

    #[tokio::test]
    async fn interrupt_needs_a_hold_then_sends_escape() {
        let term = Arc::new(FakeTerm::default());
        let shared = shared_typing(term.clone());
        let sid = add_session(&shared);
        let r = handle_cmd(&shared, &type_cmd_of(CmdOp::Cancel, sid, None, Some(300))).await;
        assert!(
            !r.ok && r.err == Some(ResultErr::Denied),
            "a short hold is refused"
        );
        assert!(term.sent.lock().unwrap().is_empty());
        let r = handle_cmd(&shared, &type_cmd_of(CmdOp::Cancel, sid, None, Some(1250))).await;
        assert!(r.ok, "{r:?}");
        assert_eq!(*term.sent.lock().unwrap(), vec!["\\x1b"]);
    }

    #[tokio::test]
    async fn replies_reach_only_devices_that_know_them() {
        for (minor, want) in [(3u64, false), (4, true)] {
            let shared = shared_typing(Arc::new(FakeTerm::default()));
            let mut d = spawn_link(&shared);
            d.to_daemon.send(hello_with_minor("1.0.0", minor)).unwrap();
            let w = d.next().await;
            let Message::Welcome(w) = w.msg else {
                panic!("welcome first")
            };
            assert_eq!(w.replies.is_some(), want, "minor {minor}");
        }
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
            dictate: false,
            notice: false,
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

    fn long_req(text: &str, ttl_s: u64) -> crate::broker::Request {
        crate::broker::Request {
            sid: 1,
            key: "claude:s".into(),
            tool: Some("Bash".into()),
            kind: AttKind::Approval,
            title: Some("Run shell command".into()),
            text: Some(text.into()),
            opts: Vec::new(),
            ttl: std::time::Duration::from_secs(ttl_s),
            focus_only: false,
            dictate: false,
            notice: false,
        }
    }

    /// Connects a device speaking proto 0.`minor` and reads past the welcome and the snapshot.
    async fn linked(shared: &Arc<Shared>, minor: u64) -> Dev {
        let mut d = spawn_link(shared);
        d.to_daemon.send(hello_with_minor("1.0.0", minor)).unwrap();
        d.next().await; // welcome
        d.next().await; // snapshot
        d
    }

    /// The text of every `detail` chunk up to the command's `result`, which it returns.
    async fn detail_chunks(d: &mut Dev) -> (Vec<Detail>, CmdResult) {
        let mut chunks = Vec::new();
        loop {
            match d.next().await.msg {
                Message::Detail(c) => chunks.push(c),
                Message::Result(r) => return (chunks, r),
                m => panic!("unexpected {m:?}"),
            }
        }
    }

    #[tokio::test]
    async fn the_window_and_full_text_reach_only_devices_speaking_0_11() {
        let shared = shared();
        let text = format!("deploy {}", "--flag ".repeat(60));
        let (rid, _wait) = shared.broker.open(long_req(&text, 60));
        let mut old = linked(&shared, 10).await;
        let mut new = linked(&shared, 11).await;
        let Message::Attention(a) = old.next().await.msg else {
            panic!()
        };
        assert_eq!(
            (a.left, a.win, a.len),
            (None, None, None),
            "an old decoder rejects unknown keys"
        );
        let Message::Attention(a) = new.next().await.msg else {
            panic!()
        };
        assert_eq!(a.win, Some(60_000));
        assert!(
            a.left.unwrap() <= 60_000 && a.left.unwrap() > 50_000,
            "what is left, not the whole window"
        );
        assert_eq!(a.len, Some(text.len() as u64));
        // Open on PC from the 0.10 watch focuses only: the request stays open
        old.to_daemon
            .send(cmd_bytes(1, cmd(CmdOp::Focus, Some(rid), None, None)))
            .unwrap();
        let _focus_result = result_of(&mut old).await; // no such session here: only the hand-back matters
        assert_eq!(shared.broker.pending().len(), 1);
        // from the 0.11 watch it also hands the request back at once
        new.to_daemon
            .send(cmd_bytes(1, cmd(CmdOp::Focus, Some(rid), None, None)))
            .unwrap();
        let _focus_result = result_of(&mut new).await;
        assert!(shared.broker.pending().is_empty());
        let Message::Clear(c) = new.next().await.msg else {
            panic!()
        };
        assert_eq!((c.rid, c.why), (rid, ClearWhy::HandedBack));
        let Message::Clear(c) = old.next().await.msg else {
            panic!()
        };
        assert_eq!(
            (c.rid, c.why),
            (rid, ClearWhy::Cancelled),
            "an old decoder knows no handed_back"
        );
        // and the answer that might still be on its way is stale
        new.to_daemon
            .send(cmd_bytes(
                2,
                cmd(CmdOp::Approve, Some(rid), None, Some(1250)),
            ))
            .unwrap();
        assert_eq!(result_of(&mut new).await.err, Some(ResultErr::Stale));
    }

    #[tokio::test]
    async fn the_full_text_arrives_in_chunks_bound_to_its_request() {
        let shared = shared();
        // multi-byte characters across chunk edges, 2.6 KB in all
        let text = "é".repeat(700) + &"x".repeat(1200);
        let (rid, wait) = shared.broker.open(long_req(&text, 60));
        let mut d = linked(&shared, 11).await;
        let _att = d.next().await;
        d.to_daemon
            .send(cmd_bytes(
                5,
                cmd(CmdOp::Detail, Some(rid), Some(Arg::Uint(0)), None),
            ))
            .unwrap();
        let (chunks, r) = detail_chunks(&mut d).await;
        assert!(r.ok);
        assert!(chunks.len() >= 3);
        let mut whole = String::new();
        for c in &chunks {
            assert_eq!(
                (c.rid, c.sid, c.total),
                (Some(rid), Some(1), Some(text.len() as u64))
            );
            assert_eq!(c.off, Some(whole.len() as u64), "in order, no gap");
            assert!(c.text.len() <= crate::broker::DETAIL_CHUNK);
            whole.push_str(&c.text);
        }
        assert_eq!(whole, text);
        // after a dropped link the watch resumes where it got to
        let resume = chunks[0].text.len() as u64;
        d.to_daemon
            .send(cmd_bytes(
                6,
                cmd(CmdOp::Detail, Some(rid), Some(Arg::Uint(resume)), None),
            ))
            .unwrap();
        let (rest, r) = detail_chunks(&mut d).await;
        assert!(r.ok);
        assert_eq!(rest[0].off, Some(resume));
        assert_eq!(
            rest.iter().map(|c| c.text.as_str()).collect::<String>(),
            &text[resume as usize..]
        );
        // an offset inside a character, the wrong request, or no request: no text at all
        for (id, rid, off, err) in [
            (7, Some(rid), 1, ResultErr::Denied),
            (8, Some(rid + 77), 0, ResultErr::Stale),
            (9, None, 0, ResultErr::Denied),
        ] {
            d.to_daemon
                .send(cmd_bytes(
                    id,
                    cmd(CmdOp::Detail, rid, Some(Arg::Uint(off)), None),
                ))
                .unwrap();
            let (none, r) = detail_chunks(&mut d).await;
            assert!(none.is_empty());
            assert_eq!(r.err, Some(err));
        }
        // a transfer does not hold up an answer sent right behind it, and the daemon never asks
        // whether the text was read (that lock is the watch's; a hold is the proof here)
        d.to_daemon
            .send(cmd_bytes(
                10,
                cmd(CmdOp::Detail, Some(rid), Some(Arg::Uint(0)), None),
            ))
            .unwrap();
        d.to_daemon
            .send(cmd_bytes(
                11,
                cmd(CmdOp::Approve, Some(rid), None, Some(1250)),
            ))
            .unwrap();
        let (_, r) = detail_chunks(&mut d).await;
        assert!(r.ok);
        assert!(result_of(&mut d).await.ok);
        assert_eq!(wait.await.unwrap().decision, "allow");
        let _clear = d.next().await;
        // once answered there is no text to fetch
        d.to_daemon
            .send(cmd_bytes(
                12,
                cmd(CmdOp::Detail, Some(rid), Some(Arg::Uint(0)), None),
            ))
            .unwrap();
        let (none, r) = detail_chunks(&mut d).await;
        assert!(none.is_empty());
        assert_eq!(r.err, Some(ResultErr::Stale));
    }

    #[tokio::test]
    async fn detail_needs_proto_0_11_and_content_privacy() {
        let shared = shared();
        let (rid, _w) = shared.broker.open(long_req(&"y".repeat(500), 60));
        let mut d = linked(&shared, 10).await;
        let _att = d.next().await;
        d.to_daemon
            .send(cmd_bytes(
                5,
                cmd(CmdOp::Detail, Some(rid), Some(Arg::Uint(0)), None),
            ))
            .unwrap();
        let (none, r) = detail_chunks(&mut d).await;
        assert!(none.is_empty());
        assert_eq!(r.err, Some(ResultErr::Unsupported));
        // privacy meta: neither the length nor the text leaves the daemon
        let (_tx, rx) = mpsc::unbounded_channel();
        let (tx, mut out) = mpsc::unbounded_channel();
        let mut c = Conn {
            link: Box::new(ChanLink { rx, tx }),
            trace: Trace::null(),
            src: "t".into(),
            next_id: 1,
            privacy: Privacy::Meta,
            peer_minor: 11,
            dictate: false,
            deltas: DeltaQueue::default(),
            hidden: HashSet::new(),
            linked: None,
        };
        c.send(
            None,
            Message::Attention(shared.broker.pending().pop().unwrap()),
        )
        .await
        .unwrap();
        let Message::Attention(a) = Envelope::from_bytes(&out.recv().await.unwrap())
            .unwrap()
            .msg
        else {
            panic!()
        };
        assert_eq!(a.len, None);
        assert!(a.win.is_some(), "the countdown is metadata");
        detail_cmd(
            &shared,
            &mut c,
            3,
            &cmd(CmdOp::Detail, Some(rid), Some(Arg::Uint(0)), None),
        )
        .await
        .unwrap();
        let env = Envelope::from_bytes(&out.recv().await.unwrap()).unwrap();
        let Message::Result(r) = env.msg else {
            panic!("no detail under meta, got {:?}", env.msg)
        };
        assert_eq!(r.err, Some(ResultErr::Denied));
    }

    #[test]
    fn chunks_cut_on_character_boundaries() {
        let t = "aé€😀".repeat(3);
        for max in [4usize, 5, 7, 100] {
            let cs = chunks(&t, 0, max);
            assert_eq!(cs.iter().map(|(_, c)| *c).collect::<String>(), t);
            assert!(cs.iter().all(|(_, c)| c.len() <= max && !c.is_empty()));
        }
        assert_eq!(
            chunks("", 0, 8),
            vec![(0, "")],
            "an empty text still says its total"
        );
        assert_eq!(chunks("abc", 3, 8), vec![(3, "")]);
    }

    #[tokio::test]
    async fn a_linked_device_counts_until_its_link_ends() {
        let shared = shared();
        assert_eq!(shared.broker.devices(), 0);
        let d = linked(&shared, 11).await;
        assert_eq!(shared.broker.devices(), 1);
        drop(d);
        for _ in 0..50 {
            if shared.broker.devices() == 0 {
                break;
            }
            tokio::time::sleep(std::time::Duration::from_millis(10)).await;
        }
        assert_eq!(shared.broker.devices(), 0);
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
            tokio::time::sleep(std::time::Duration::from_millis(20)).await;
            let ping = Envelope {
                id: 99,
                re: None,
                msg: Message::Ping(Empty {}),
            };
            d.to_daemon.send(ping.to_bytes().unwrap()).unwrap();
            // A held delta goes out before the pong; one left empty is not sent at all.
            let mut m = d.next().await.msg;
            if let Message::SessionUpd(u) = m {
                assert_eq!(u.hn.as_deref(), want);
                m = d.next().await.msg;
            } else {
                assert_eq!(want, None);
            }
            assert!(matches!(m, Message::Pong(_)), "{m:?}");
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
            dictate: false,
            deltas: DeltaQueue::default(),
            hidden: HashSet::new(),
            linked: None,
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

    /// A shell whose job ran (and ended, when `exit` is given) in a `Shared` with a fake terminal.
    fn job_shared(term: Arc<FakeTerm>, exit: Option<u64>) -> (Arc<Shared>, u64) {
        use crate::jobs::{Config, Jobs};
        let trace = Trace::null();
        let registry = Arc::new(Registry::new(trace.clone()));
        let mut s = Shared::new(
            registry.clone(),
            Arc::new(Broker::new(registry)),
            trace,
            Arc::new(NoFocus),
            Arc::new(FakeScroll::default()),
        );
        s.term = term;
        s.jobs = Arc::new(Jobs::new(
            s.registry.clone(),
            Config {
                min: std::time::Duration::ZERO,
                ..Default::default()
            },
        ));
        let now = std::time::Instant::now();
        s.jobs.start(
            now,
            1000,
            4321,
            "cargo test -- --nocapture",
            Default::default(),
        );
        s.jobs.tick(now);
        if let Some(code) = exit {
            s.jobs.end(now, 4321, code);
        }
        let sid = s.registry.sid_of("shell:4321").unwrap();
        (Arc::new(s), sid)
    }

    #[tokio::test]
    async fn rerun_types_a_finished_jobs_line_and_refuses_the_rest() {
        let term = Arc::new(FakeTerm::default());
        let (shared, sid) = job_shared(term.clone(), Some(1));
        let rerun = |sid, hold| type_cmd_of(CmdOp::Rerun, sid, None, hold);
        let r = handle_cmd(&shared, &rerun(sid, Some(300))).await;
        assert_eq!(
            (r.ok, r.err),
            (false, Some(ResultErr::Denied)),
            "needs a hold"
        );
        let r = handle_cmd(&shared, &rerun(999, Some(1250))).await;
        assert_eq!(r.err, Some(ResultErr::Stale));
        assert!(term.sent.lock().unwrap().is_empty());
        let agent = add_session(&shared);
        let r = handle_cmd(&shared, &rerun(agent, Some(1250))).await;
        assert_eq!(r.err, Some(ResultErr::Denied), "not a job");

        let r = handle_cmd(&shared, &rerun(sid, Some(1250))).await;
        assert!(r.ok, "{r:?}");
        assert_eq!(
            *term.sent.lock().unwrap(),
            vec!["cargo test -- --nocapture", "\\r"]
        );

        // the shell is running something again: refused
        shared
            .jobs
            .start(std::time::Instant::now(), 1, 4321, "ls", Default::default());
        let r = handle_cmd(&shared, &rerun(sid, Some(1250))).await;
        assert_eq!(r.err, Some(ResultErr::Denied));
        assert_eq!(term.sent.lock().unwrap().len(), 2);

        // a job still running (no finished one) is not a finished job
        let (running, sid) = job_shared(Arc::new(FakeTerm::default()), None);
        let r = handle_cmd(&running, &rerun(sid, Some(1250))).await;
        assert_eq!(r.err, Some(ResultErr::Stale));
        // no quick replies or transcripts for a job
        let r = handle_cmd(&running, &type_cmd_of(CmdOp::Type, sid, Some(0), None)).await;
        assert_eq!(r.err, Some(ResultErr::Denied));
    }

    #[tokio::test]
    async fn stop_needs_a_hold_and_a_running_foreground_job() {
        let term = Arc::new(FakeTerm::default());
        let (shared, sid) = job_shared(term.clone(), None);
        let stop = |sid, hold| type_cmd_of(CmdOp::Cancel, sid, None, hold);
        let r = handle_cmd(&shared, &stop(sid, Some(300))).await;
        assert_eq!(r.err, Some(ResultErr::Denied));
        // shell pid 4321 is not a process of this machine's tty: nothing to interrupt
        let r = handle_cmd(&shared, &stop(sid, Some(1250))).await;
        assert!(!r.ok);
        assert!(
            term.sent.lock().unwrap().is_empty(),
            "a job is stopped with SIGINT, never Esc"
        );
    }

    #[tokio::test]
    async fn ack_removes_a_finished_job_but_only_marks_a_running_one() {
        let (shared, sid) = job_shared(Arc::new(FakeTerm::default()), None);
        let ack = type_cmd_of(CmdOp::Ack, sid, None, None);
        assert!(handle_cmd(&shared, &ack).await.ok);
        assert_eq!(
            shared.registry.snapshot()[0].seen,
            Some(true),
            "still there"
        );
        shared.jobs.end(std::time::Instant::now(), 4321, 0);
        assert!(handle_cmd(&shared, &ack).await.ok);
        assert!(shared.registry.snapshot().is_empty());
        assert_eq!(handle_cmd(&shared, &ack).await.err, Some(ResultErr::Stale));
    }

    #[tokio::test]
    async fn jobs_reach_only_devices_speaking_proto_0_9() {
        let (shared, sid) = job_shared(Arc::new(FakeTerm::default()), None);
        let agent = add_session(&shared);
        for (minor, sees) in [(8, false), (9, true)] {
            let mut d = spawn_link(&shared);
            d.to_daemon.send(hello_with_minor("1.0.0", minor)).unwrap();
            d.next().await;
            let Message::Snapshot(s) = d.next().await.msg else {
                panic!("snapshot")
            };
            let sids: Vec<u64> = s.sessions.iter().map(|s| s.sid).collect();
            assert_eq!(sids.contains(&sid), sees, "minor {minor}");
            assert!(sids.contains(&agent));
            let job = s.sessions.iter().find(|s| s.sid == sid);
            assert_eq!(job.map(|s| s.eta), sees.then_some(Some(0)));

            // a delta, the finish with its exit status, and the card's removal
            shared.jobs.end(std::time::Instant::now(), 4321, 2);
            shared.registry.publish(Update::Gone(sid));
            tokio::time::sleep(std::time::Duration::from_millis(40)).await;
            let ping = Envelope {
                id: 99,
                re: None,
                msg: Message::Ping(Empty {}),
            };
            d.to_daemon.send(ping.to_bytes().unwrap()).unwrap();
            let mut seen = Vec::new();
            loop {
                match d.next().await.msg {
                    Message::Pong(_) => break,
                    m => seen.push(m),
                }
            }
            if sees {
                assert!(
                    matches!(&seen[0], Message::SessionUpd(u) if u.exit == Some(2)),
                    "{seen:?}"
                );
                assert!(matches!(seen.last(), Some(Message::Gone(g)) if g.sid == sid));
            } else {
                assert!(seen.is_empty(), "nothing for an old device: {seen:?}");
            }
            // the next round starts from a fresh job
            if minor == 8 {
                let now = std::time::Instant::now();
                shared
                    .jobs
                    .start(now, 1000, 4321, "cargo test", Default::default());
                shared.jobs.tick(now);
            }
        }
    }

    #[test]
    fn job_estimate_and_exit_are_stripped_for_old_devices() {
        let (_tx, rx) = mpsc::unbounded_channel();
        let (tx, _rx) = mpsc::unbounded_channel();
        let mut c = Conn {
            link: Box::new(ChanLink { rx, tx }),
            trace: Trace::null(),
            src: "t".into(),
            next_id: 1,
            privacy: Privacy::Preview,
            peer_minor: 8,
            dictate: false,
            deltas: DeltaQueue::default(),
            hidden: HashSet::new(),
            linked: None,
        };
        let mut s = Session::delta(1);
        (s.eta, s.exit) = (Some(60), Some(1));
        let out = c.for_peer(s.clone());
        assert_eq!((out.eta, out.exit), (None, None));
        c.peer_minor = 9;
        let out = c.for_peer(s);
        assert_eq!((out.eta, out.exit), (Some(60), Some(1)));
    }

    #[test]
    fn gauge_fields_need_proto_0_10_and_the_pull_request_needs_privacy() {
        let (_tx, rx) = mpsc::unbounded_channel();
        let (tx, _rx) = mpsc::unbounded_channel();
        let mut c = Conn {
            link: Box::new(ChanLink { rx, tx }),
            trace: Trace::null(),
            src: "t".into(),
            next_id: 1,
            privacy: Privacy::Preview,
            peer_minor: 9,
            dictate: false,
            deltas: DeltaQueue::default(),
            hidden: HashSet::new(),
            linked: None,
        };
        let mut s = Session::delta(1);
        (s.ctx, s.mdl, s.pm, s.pr, s.prs) = (
            Some(72),
            Some("Opus".into()),
            Some(PermMode::Plan),
            Some(12),
            Some(PrState::Approved),
        );
        let out = c.for_peer(s.clone());
        assert_eq!(out, Session::delta(1), "an 0.9 device gets none of them");
        c.peer_minor = 10;
        assert_eq!(c.for_peer(s.clone()), s);
        c.privacy = Privacy::Meta;
        let out = c.for_peer(s);
        assert_eq!(
            (out.ctx, out.pm, out.pr, out.prs),
            (Some(72), Some(PermMode::Plan), None, None)
        );
    }

    #[tokio::test]
    async fn usage_reaches_only_devices_speaking_proto_0_10() {
        use crate::usage::{Rates, Window};
        let shared = shared();
        let now = now_unix();
        let w = |pct| Window {
            pct,
            resets_at: now + 3600,
        };
        // a reading from before the device connected is handed over after the snapshot
        shared.registry.record_usage(Rates {
            five_hour: Some(w(42.0)),
            seven_day: None,
        });
        for (minor, sees) in [(9, false), (10, true)] {
            let mut d = spawn_link(&shared);
            d.to_daemon.send(hello_with_minor("1.0.0", minor)).unwrap();
            d.next().await; // welcome
            d.next().await; // snapshot
            shared.registry.record_usage(Rates {
                five_hour: Some(w(43.0 + minor as f64)),
                seven_day: None,
            });
            tokio::time::sleep(std::time::Duration::from_millis(40)).await;
            let ping = Envelope {
                id: 99,
                re: None,
                msg: Message::Ping(Empty {}),
            };
            d.to_daemon.send(ping.to_bytes().unwrap()).unwrap();
            let mut seen = Vec::new();
            loop {
                match d.next().await.msg {
                    Message::Pong(_) => break,
                    Message::Usage(u) => seen.push(u.h5),
                    m => panic!("unexpected {m:?}"),
                }
            }
            if sees {
                assert_eq!(seen.len(), 2, "{seen:?}");
                assert_eq!(seen[1], Some(53));
            } else {
                assert!(seen.is_empty(), "nothing for an old device: {seen:?}");
            }
        }
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

    /// A burst of hook events from 4 sessions: each step's start and result reach the device as
    /// one delta, every step and state change still arrives in order, a `clear` is not
    /// overtaken by the deltas published before it, and the device ends up with the registry's
    /// state.
    #[tokio::test]
    async fn a_burst_of_deltas_is_coalesced_without_losing_steps_or_order() {
        use crate::registry::{SessionEvent, SessionPatch, Step, Via};
        const STEPS: u64 = 8;
        let shared = shared();
        let mut d = spawn_link(&shared);
        d.to_daemon.send(hello_with_minor("1.0.0", 3)).unwrap();
        d.next().await; // welcome
        d.next().await; // snapshot
        let (_, mut published) = shared.registry.subscribe();
        let apply = |key: &str, f: &dyn Fn(&mut SessionPatch)| {
            let mut p = SessionPatch::new(Agent::Copilot);
            f(&mut p);
            shared.registry.apply(SessionEvent::Update {
                key: key.into(),
                via: Via::Local,
                patch: p,
                ctx: crate::registry::HostCtx::default(),
            });
        };
        let keys: Vec<String> = (0..4).map(|i| format!("copilot:s{i}")).collect();
        for k in &keys {
            apply(k, &|p| p.state = Some(SessState::Start));
            apply(k, &|p| {
                (p.state, p.step) = (Some(SessState::Work), Step::Reset)
            });
        }
        for n in 1..=STEPS {
            for k in &keys {
                apply(k, &|p| {
                    (p.state, p.step) = (Some(SessState::Work), Step::Start);
                    (p.tool, p.summary) = (Some("bash".into()), Some(format!("bash · step {n}")));
                });
                apply(k, &|p| {
                    (p.state, p.step) = (Some(SessState::Work), Step::Result(true))
                });
            }
            if n == 4 {
                shared.registry.publish(Update::Clear(Clear {
                    rid: 7,
                    why: ClearWhy::Cancelled,
                }));
            }
        }
        for k in &keys {
            apply(k, &|p| p.state = Some(SessState::Done));
        }
        let mut before = 0;
        while let Ok(u) = published.try_recv() {
            before += matches!(u, Update::Upsert(_)) as usize;
        }

        let sids: Vec<u64> = keys
            .iter()
            .map(|k| shared.registry.sid_of(k).unwrap())
            .collect();
        let mut model: BTreeMap<u64, Session> = BTreeMap::new();
        let mut steps: BTreeMap<u64, Vec<(u64, Option<bool>)>> = BTreeMap::new();
        let mut states: BTreeMap<u64, Vec<SessState>> = BTreeMap::new();
        let mut after = 0;
        let mut cleared = false;
        while model
            .values()
            .filter(|s| s.st == Some(SessState::Done))
            .count()
            < 4
        {
            match d.next().await.msg {
                Message::SessionUpd(u) => {
                    after += 1;
                    if let Some(n) = u.n {
                        steps.entry(u.sid).or_default().push((n, u.ok));
                    } else if let Some(ok) = u.ok {
                        steps.get_mut(&u.sid).unwrap().last_mut().unwrap().1 = Some(ok);
                    }
                    states.entry(u.sid).or_default().extend(u.st);
                    model
                        .entry(u.sid)
                        .or_insert_with(|| Session::delta(u.sid))
                        .merge(&u);
                }
                Message::Clear(c) => {
                    assert_eq!(c.rid, 7);
                    // everything published before the clear went out before it
                    for sid in &sids {
                        assert_eq!(steps[sid].last(), Some(&(4, Some(true))));
                    }
                    cleared = true;
                }
                m => panic!("{m:?}"),
            }
        }
        assert!(cleared);
        eprintln!("4 sessions x {STEPS} steps: {before} deltas published, {after} frames sent");
        assert_eq!(before, 4 * (2 + 2 * STEPS as usize + 1));
        assert_eq!(after, 4 * (1 + STEPS as usize));
        let want: Vec<_> = (1..=STEPS).map(|n| (n, Some(true))).collect();
        for s in shared.registry.snapshot() {
            assert_eq!(steps[&s.sid], want);
            assert_eq!(
                states[&s.sid],
                [SessState::Start, SessState::Work, SessState::Done]
            );
            assert_eq!(model[&s.sid], s);
        }
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
