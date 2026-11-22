//! Bench mode (BENCH.md §4-5): play a task script on a real watch as finger strokes, with the link held for
//! the bench, and bring back the watch's frame timeline.
//!
//! While a run holds a link, the link task (device.rs) sends that watch nothing from the registry or the
//! broker, and hands every command the watch sends to the run, which answers `result ok`: a test approval
//! never reaches an agent. When the run ends the link resynchronises, as after a lagged update.

use crate::registry::now_unix;
use crate::trace::Trace;
use agentlink_proto::{
    Action, Agent, AttKind, Attention, BenchOp, Cmd, CmdOp, DbgBench, DbgFrames, DbgInjectInput,
    DbgTouch, DbgUiState, Empty, InputEvent, InputSub, Message, SessState, Session, Snapshot,
};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::sync::Mutex;
use std::time::Duration;
use tokio::sync::mpsc::{unbounded_channel, UnboundedReceiver, UnboundedSender};
use tokio::time::Instant;

/// The first protocol minor with dbg_touch / dbg_bench / dbg_frames.
pub const BENCH_MINOR: u64 = 24;
/// One timeline record (BENCH.md §3).
pub const REC_BYTES: usize = 16;
const EXPECT_CMD_TIMEOUT: Duration = Duration::from_secs(10);
const UI_STATE_TIMEOUT: Duration = Duration::from_secs(3);
const DUMP_TIMEOUT: Duration = Duration::from_secs(60);
/// Time for the watch to draw the emptied screen before the timeline starts, and the last animation after.
const SETTLE: Duration = Duration::from_millis(400);

/// From the run to the link task.
#[derive(Debug)]
pub enum ToLink {
    Send(Box<Message>),
    /// Hold the link for the bench (true), or give it back and resynchronise (false).
    Hold(bool),
}

/// From the link task to the run.
#[derive(Debug)]
pub enum Event {
    Frames(DbgFrames),
    UiState(DbgUiState),
    Cmd(Cmd),
    Down,
}

struct Dev {
    key: String,
    minor: u64,
    debug: bool,
    fw: String,
    transport: &'static str,
}

#[derive(Default)]
struct Inner {
    devs: HashMap<u64, Dev>,
    txs: HashMap<u64, UnboundedSender<ToLink>>,
    active: Option<(u64, UnboundedSender<Event>)>,
}

pub struct Bench {
    inner: Mutex<Inner>,
    trace: Trace,
}

/// Gives the link back when a run ends, however it ends.
struct Hold<'a>(&'a Bench);

impl Drop for Hold<'_> {
    fn drop(&mut self) {
        let mut g = self.0.inner.lock().unwrap();
        if let Some((link, _)) = g.active.take() {
            if let Some(tx) = g.txs.get(&link) {
                let _ = tx.send(ToLink::Hold(false));
            }
        }
    }
}

/// `{"ctl":"bench", "name", "script", "human"?, "device"?}`: one run of one task.
pub struct Request {
    pub name: String,
    pub script: String,
    pub human: Option<u64>,
    pub device: Option<String>,
}

impl Request {
    pub fn from_json(v: &Value) -> Option<Request> {
        Some(Request {
            name: v["name"].as_str().unwrap_or("task").to_owned(),
            script: v["script"].as_str()?.to_owned(),
            human: v["human"].as_u64(),
            device: v["device"].as_str().map(str::to_owned),
        })
    }
}

impl Bench {
    pub fn new(trace: Trace) -> Self {
        Bench {
            inner: Mutex::default(),
            trace,
        }
    }

    // ── from the link tasks ──

    pub fn hello(
        &self,
        link_id: u64,
        key: &str,
        minor: u64,
        debug: bool,
        fw: &str,
        transport: &'static str,
    ) {
        self.inner.lock().unwrap().devs.insert(
            link_id,
            Dev {
                key: key.to_owned(),
                minor,
                debug,
                fw: fw.to_owned(),
                transport,
            },
        );
    }

    pub fn attach(&self, link_id: u64, tx: UnboundedSender<ToLink>) {
        self.inner.lock().unwrap().txs.insert(link_id, tx);
    }

    pub fn link_down(&self, link_id: u64) {
        let mut g = self.inner.lock().unwrap();
        g.devs.remove(&link_id);
        g.txs.remove(&link_id);
        if let Some((l, tx)) = &g.active {
            if *l == link_id {
                let _ = tx.send(Event::Down);
            }
        }
    }

    /// A message from the watch on `link_id`, for the run holding that link. Returns false when no run does.
    pub fn on_event(&self, link_id: u64, ev: Event) -> bool {
        let g = self.inner.lock().unwrap();
        match &g.active {
            Some((l, tx)) if *l == link_id => tx.send(ev).is_ok(),
            _ => false,
        }
    }

    // ── a run ──

    fn pick(&self, want: Option<&str>) -> Result<(u64, String, String, &'static str), String> {
        let g = self.inner.lock().unwrap();
        let mut ok: Vec<_> = g
            .devs
            .iter()
            .filter(|(_, d)| {
                d.minor >= BENCH_MINOR && d.debug && want.is_none_or(|w| d.key.starts_with(w))
            })
            .collect();
        ok.sort_by(|a, b| a.1.key.cmp(&b.1.key));
        match ok.as_slice() {
            [] => Err(
                "no watch connected with firmware that supports the bench (proto 0.24, debug)"
                    .into(),
            ),
            [(l, d)] => Ok((**l, d.key.clone(), d.fw.clone(), d.transport)),
            many => Err(format!(
                "several watches are connected ({}); pick one with --device",
                many.iter()
                    .map(|(_, d)| d.key.as_str())
                    .collect::<Vec<_>>()
                    .join(", ")
            )),
        }
    }

    fn send(&self, link_id: u64, m: ToLink) -> Result<(), String> {
        let g = self.inner.lock().unwrap();
        g.txs
            .get(&link_id)
            .is_some_and(|tx| tx.send(m).is_ok())
            .then_some(())
            .ok_or_else(|| "link to the watch dropped".to_owned())
    }

    /// Runs one task to its end, reporting on `out`; `pending` is the number of real requests waiting for an
    /// answer (a run does not start over them). Dropping the receiver of `out` stops it.
    pub async fn run(&self, req: Request, pending: usize, out: UnboundedSender<Value>) {
        let end = match self.run_inner(&req, pending, &out).await {
            Ok(v) => v,
            Err(msg) => json!({"bench": "error", "name": req.name, "msg": msg}),
        };
        self.trace.audit(
            "bench_end",
            None,
            None,
            json!({"name": req.name, "ok": end["bench"] == "done", "msg": end["msg"]}),
        );
        let _ = out.send(end);
    }

    async fn run_inner(
        &self,
        req: &Request,
        pending: usize,
        out: &UnboundedSender<Value>,
    ) -> Result<Value, String> {
        let steps = parse(&req.script)?;
        if pending > 0 {
            return Err(format!(
                "{pending} real request(s) wait for an answer: answer them before a bench run"
            ));
        }
        let (link, key, fw, transport) = self.pick(req.device.as_deref())?;
        let (tx, mut rx) = unbounded_channel();
        let _hold = {
            let mut g = self.inner.lock().unwrap();
            if g.active.is_some() {
                return Err("a bench run is already going".into());
            }
            g.active = Some((link, tx));
            Hold(self)
        };
        self.trace.audit(
            "bench_start",
            None,
            None,
            json!({"name": req.name, "device": key, "human": req.human}),
        );
        let _ = out.send(
            json!({"bench": "start", "name": req.name, "device": key, "fw": fw,
                                "transport": transport, "human": req.human}),
        );
        self.send(link, ToLink::Hold(true))?;
        let send = |m: Message| self.send(link, ToLink::Send(Box::new(m)));
        send(Message::Snapshot(Snapshot {
            sessions: vec![],
            away: None,
        }))?;
        let mut p = Player {
            rx: &mut rx,
            out,
            cmds: Vec::new(),
            seen: Vec::new(),
            ui: None,
            rng: req.human.map(Rng::new),
        };
        p.sleep(SETTLE).await?;
        send(Message::DbgBench(DbgBench {
            op: BenchOp::Start,
            arg: None,
        }))?;
        let t0 = Instant::now();
        let mut log = Vec::new();
        let mut after_wait = false;
        for (line, step) in &steps {
            let at = t0.elapsed().as_millis() as u64;
            match step {
                Step::Wait(ms) => {
                    p.sleep(Duration::from_millis(*ms)).await?;
                    after_wait = true;
                    continue;
                }
                Step::Input(inp) => {
                    if after_wait {
                        if let Some(ms) = p.rng.as_mut().map(Rng::reaction_ms) {
                            p.sleep_ms(ms).await?;
                        }
                    }
                    let (msg, busy) = inp.message(p.rng.as_mut());
                    send(msg)?;
                    p.sleep(Duration::from_millis(busy)).await?;
                }
                Step::Send(m) => {
                    send(fresh(m))?;
                    p.sleep_ms(50).await?;
                }
                Step::Mark(n) => send(Message::DbgBench(DbgBench {
                    op: BenchOp::Mark,
                    arg: Some(*n),
                }))?,
                Step::ExpectCmd(op) => {
                    if let Err(e) = p.expect_cmd(*op).await {
                        return Err(format!("line {line}: {e}"));
                    }
                }
                Step::ExpectScreen(name) => {
                    send(Message::DbgUiStateReq(Empty {}))?;
                    let got = p.ui_state().await?;
                    if &got != name {
                        return Err(format!(
                            "line {line}: expected screen {name}, the watch shows {got}"
                        ));
                    }
                }
            }
            after_wait = false;
            log.push(json!({"line": line, "at_ms": at}));
        }
        p.sleep(SETTLE).await?;
        send(Message::DbgBench(DbgBench {
            op: BenchOp::Stop,
            arg: None,
        }))?;
        let (recs, lost) = p.dump().await?;
        Ok(
            json!({"bench": "done", "name": req.name, "device": key, "fw": fw, "transport": transport,
                  "human": req.human, "recs": hex::encode(&recs), "lost": lost, "steps": log,
                  "cmds": p.seen}),
        )
    }
}

/// A session or card with this moment's clock.
fn fresh(m: &Message) -> Message {
    let now = now_unix();
    match m {
        Message::SessionUpd(s) => {
            let mut s = s.clone();
            s.t0 = Some(now - 3600 + s.sid);
            s.tst = Some(now - 40 - 53 * s.sid);
            Message::SessionUpd(s)
        }
        Message::Attention(a) => {
            let mut a = a.clone();
            a.exp = Some(now + 300);
            Message::Attention(a)
        }
        m => m.clone(),
    }
}

struct Player<'a> {
    rx: &'a mut UnboundedReceiver<Event>,
    out: &'a UnboundedSender<Value>,
    /// Commands the watch sent since the last expect_cmd.
    cmds: Vec<Cmd>,
    /// Every command the run saw, in order (the summary's).
    seen: Vec<String>,
    ui: Option<DbgUiState>,
    rng: Option<Rng>,
}

impl Player<'_> {
    fn take(&mut self, ev: Event) -> Result<Option<DbgFrames>, String> {
        match ev {
            Event::Cmd(c) => {
                let _ = self.out.send(json!({"bench": "cmd", "op": name(&c.op)}));
                self.seen.push(name(&c.op));
                self.cmds.push(c);
            }
            Event::UiState(s) => self.ui = Some(s),
            Event::Frames(f) => return Ok(Some(f)),
            Event::Down => return Err("link to the watch dropped".into()),
        }
        Ok(None)
    }

    /// Waits, taking the watch's messages meanwhile.
    async fn sleep(&mut self, d: Duration) -> Result<(), String> {
        let end = Instant::now() + d;
        loop {
            tokio::select! {
                _ = tokio::time::sleep_until(end) => return Ok(()),
                _ = self.out.closed() => return Err("cancelled".into()),
                ev = self.rx.recv() => {
                    let Some(ev) = ev else { return Err("cancelled".into()) };
                    self.take(ev)?;
                }
            }
        }
    }

    async fn sleep_ms(&mut self, ms: u64) -> Result<(), String> {
        self.sleep(Duration::from_millis(ms)).await
    }

    async fn next(&mut self, until: Instant) -> Result<Option<Event>, String> {
        tokio::select! {
            _ = tokio::time::sleep_until(until) => Ok(None),
            _ = self.out.closed() => Err("cancelled".into()),
            ev = self.rx.recv() => ev.map(Some).ok_or_else(|| "cancelled".into()),
        }
    }

    async fn expect_cmd(&mut self, op: CmdOp) -> Result<(), String> {
        let until = Instant::now() + EXPECT_CMD_TIMEOUT;
        loop {
            if self.cmds.iter().any(|c| c.op == op) {
                self.cmds.clear();
                return Ok(());
            }
            match self.next(until).await? {
                Some(ev) => {
                    self.take(ev)?;
                }
                None => return Err(format!("the watch did not send cmd {}", name(&op))),
            }
        }
    }

    async fn ui_state(&mut self) -> Result<String, String> {
        self.ui = None;
        let until = Instant::now() + UI_STATE_TIMEOUT;
        loop {
            if let Some(s) = self.ui.take() {
                return Ok(s.screen);
            }
            match self.next(until).await? {
                Some(ev) => {
                    self.take(ev)?;
                }
                None => return Err("the watch did not report its screen".into()),
            }
        }
    }

    async fn dump(&mut self) -> Result<(Vec<u8>, u64), String> {
        let until = Instant::now() + DUMP_TIMEOUT;
        let mut recs = Vec::new();
        loop {
            let Some(ev) = self.next(until).await? else {
                return Err(format!(
                    "the timeline stopped after {} records",
                    recs.len() / REC_BYTES
                ));
            };
            if let Some(f) = self.take(ev)? {
                if f.seq as usize * REC_BYTES != recs.len() {
                    return Err(format!("timeline records out of order at {}", f.seq));
                }
                recs.extend_from_slice(&f.recs);
                if f.last {
                    return Ok((recs, f.lost));
                }
            }
        }
    }
}

// ── scripts (BENCH.md §5) ──

#[derive(Debug, Clone, PartialEq)]
enum Input {
    Tap(i16, i16),
    Hold(i16, i16, u64),
    Swipe(i16, i16, i16, i16, u64),
    /// A drag along the rim from one angle to another (degrees, 0 = 12 o'clock, clockwise), at a radius.
    Scrub(i32, i32, u64, i32),
    Inject(InputEvent, u64),
}

impl Input {
    /// The message for this input and how long it keeps the script busy, as the simulator times it
    /// (sim_script.c): a tap is down 50 ms, a hold its time, a swipe a point every 10 ms.
    fn message(&self, rng: Option<&mut Rng>) -> (Message, u64) {
        let (dt, pts, busy) = match *self {
            Input::Inject(kind, busy) => {
                return (
                    Message::DbgInjectInput(DbgInjectInput {
                        kind,
                        x: None,
                        y: None,
                    }),
                    busy,
                );
            }
            Input::Tap(x, y) => {
                let (x, y) = scatter(rng, x, y);
                (50, vec![(x, y)], 100)
            }
            Input::Hold(x, y, ms) => {
                let (x, y) = scatter(rng, x, y);
                (ms.clamp(1, 2000), vec![(x, y)], ms + 150)
            }
            Input::Swipe(x1, y1, x2, y2, ms) => {
                let (mut r, n) = (rng, (ms / 10).clamp(1, 511) as i32);
                let (ax, ay) = scatter(r.as_deref_mut(), x1, y1);
                let (bx, by) = scatter(r, x2, y2);
                let pts = (0..=n)
                    .map(|i| {
                        let lerp =
                            |a: i16, b: i16| (a as i32 + (b as i32 - a as i32) * i / n) as i16;
                        (lerp(ax, bx), lerp(ay, by))
                    })
                    .collect();
                (10, pts, ms + 120)
            }
            Input::Scrub(d0, d1, ms, radius) => {
                let radius = radius as f64 + rng.map_or(0.0, |r| r.scatter_px() as f64);
                let n = (ms / 10).clamp(1, 511) as i32;
                let pts = (0..=n)
                    .map(|i| {
                        let deg = d0 as f64 + (d1 - d0) as f64 * i as f64 / n as f64;
                        let rad = deg.to_radians();
                        (
                            (233.0 + radius * rad.sin()).round() as i16,
                            (233.0 - radius * rad.cos()).round() as i16,
                        )
                    })
                    .collect();
                (10, pts, ms + 150)
            }
        };
        let mut b = Vec::with_capacity(pts.len() * 4);
        for (x, y) in pts {
            b.extend_from_slice(&x.to_le_bytes());
            b.extend_from_slice(&y.to_le_bytes());
        }
        (Message::DbgTouch(DbgTouch { dt, pts: b }), busy)
    }
}

fn scatter(rng: Option<&mut Rng>, x: i16, y: i16) -> (i16, i16) {
    match rng {
        Some(r) => (x + r.scatter_px(), y + r.scatter_px()),
        None => (x, y),
    }
}

#[derive(Debug, Clone, PartialEq)]
enum Step {
    Wait(u64),
    Input(Input),
    Send(Box<Message>),
    Mark(u64),
    ExpectCmd(CmdOp),
    ExpectScreen(String),
}

/// Lines only the simulator needs (its own link, checks the watch cannot answer): skipped on the watch.
const SIM_ONLY: &[&str] = &[
    "connect",
    "disconnect",
    "rx",
    "ui",
    "shot",
    "expect_label",
    "expect_no_label",
    "bench",
    "px_reset",
    "expect_px_max",
    "quit",
];

fn parse(script: &str) -> Result<Vec<(usize, Step)>, String> {
    let mut steps = Vec::new();
    for (i, raw) in script.lines().enumerate() {
        let line = raw.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (cmd, rest) = line.split_once(' ').unwrap_or((line, ""));
        // `bench mark N` is the one bench line for the watch; start / stop / save are the run's own
        let (cmd, rest) = match (cmd, rest.split_once(' ')) {
            ("bench", Some(("mark", n))) => ("mark", n),
            _ => (cmd, rest),
        };
        if SIM_ONLY.contains(&cmd) {
            continue;
        }
        let step =
            parse_step(cmd, rest.trim()).map_err(|e| format!("line {}: {e}: {line}", i + 1))?;
        steps.push((i + 1, step));
    }
    Ok(steps)
}

fn nums<T: std::str::FromStr>(rest: &str, n: usize) -> Result<Vec<T>, String> {
    let v: Vec<T> = rest
        .split_whitespace()
        .take(n)
        .map(|s| s.parse().map_err(|_| format!("not a number: {s}")))
        .collect::<Result<_, _>>()?;
    if v.len() < n {
        return Err(format!("needs {n} numbers"));
    }
    Ok(v)
}

fn parse_step(cmd: &str, rest: &str) -> Result<Step, String> {
    Ok(match cmd {
        "wait" => Step::Wait(nums::<u64>(rest, 1)?[0]),
        "tap" => {
            let v = nums::<i16>(rest, 2)?;
            Step::Input(Input::Tap(v[0], v[1]))
        }
        "hold" => {
            let v = nums::<i64>(rest, 3)?;
            Step::Input(Input::Hold(v[0] as i16, v[1] as i16, v[2].max(1) as u64))
        }
        "swipe" => {
            let n = rest.split_whitespace().count();
            let v = nums::<i64>(rest, n.clamp(4, 5))?;
            let ms = v.get(4).copied().unwrap_or(160).max(10) as u64;
            Step::Input(Input::Swipe(
                v[0] as i16,
                v[1] as i16,
                v[2] as i16,
                v[3] as i16,
                ms,
            ))
        }
        "scrub" => {
            let n = rest.split_whitespace().count();
            let v = nums::<i64>(rest, n.clamp(2, 4))?;
            let ms = v.get(2).copied().unwrap_or(400).max(10) as u64;
            Step::Input(Input::Scrub(
                v[0] as i32,
                v[1] as i32,
                ms,
                v.get(3).copied().unwrap_or(200) as i32,
            ))
        }
        "btn" => {
            let mut w = rest.split_whitespace();
            let b = w.next().ok_or("needs A or B")?;
            let mods: Vec<&str> = w.collect();
            let long = mods.iter().any(|m| *m == "long" || *m == "held");
            if mods.iter().any(|m| *m != "long" && *m != "held") {
                return Err("only btn A|B [long] plays on the watch".into());
            }
            // the watch waits 450 ms after a single press for a second one (sim_script.c btn)
            let (kind, busy) = match (b, long) {
                ("A", false) => (InputEvent::BtnA, 510),
                ("B", false) => (InputEvent::BtnB, 510),
                ("A", true) => (InputEvent::BtnALong, 1350),
                ("B", true) => (InputEvent::BtnBLong, 1350),
                _ => return Err("needs A or B".into()),
            };
            Step::Input(Input::Inject(kind, busy))
        }
        "lift" => Step::Input(Input::Inject(InputEvent::Lift, 200)),
        "mark" => Step::Mark(nums::<u64>(rest, 1)?[0]),
        "expect_cmd" => Step::ExpectCmd(by_name(&CMD_OPS, rest).ok_or("unknown cmd op")?),
        "expect_screen" if !rest.is_empty() => Step::ExpectScreen(rest.to_owned()),
        "session" => Step::Send(Box::new(Message::SessionUpd(parse_session(rest)?))),
        "question" => Step::Send(Box::new(Message::SessionUpd(parse_question(rest)?))),
        "attention" => Step::Send(Box::new(Message::Attention(parse_attention(rest)?))),
        _ => return Err("not a bench command".into()),
    })
}

/// `session SID STATE REPO|- [BRANCH|- [SUMMARY...]]`, as the simulator reads it.
fn parse_session(rest: &str) -> Result<Session, String> {
    let w: Vec<&str> = rest.splitn(5, ' ').collect();
    if w.len() < 3 {
        return Err("needs SID STATE REPO".into());
    }
    let sid: u64 = w[0].parse().map_err(|_| "bad sid")?;
    let mut s = Session::delta(sid);
    s.ag = Some(Agent::Claude);
    let (st, sub, seen) = match w[1] {
        "start" => (SessState::Start, None, None),
        "work" => (SessState::Work, None, None),
        "input" => (SessState::Input, Some(InputSub::Approval), None),
        "ask" => (SessState::Input, Some(InputSub::Question), None),
        "error" => (SessState::Error, None, None),
        "done" => (SessState::Done, None, Some(false)),
        "doneseen" => (SessState::Done, None, Some(true)),
        "idle" => (SessState::Idle, None, None),
        _ => return Err("unknown state".into()),
    };
    s.st = Some(st);
    s.sub = sub;
    s.seen = seen.or(Some(false));
    s.repo = (w[2] != "-").then(|| w[2].to_owned());
    s.br = w.get(3).filter(|b| **b != "-").map(|b| (*b).to_owned());
    s.sum = w.get(4).map(|t| (*t).to_owned());
    Ok(s)
}

/// `question SID Q|ANSWER...`: the agent's closing question and up to three answers (the Reply list).
fn parse_question(rest: &str) -> Result<Session, String> {
    let (sid, text) = rest.split_once(' ').ok_or("needs SID Q|ANSWER...")?;
    let mut s = Session::delta(sid.parse().map_err(|_| "bad sid")?);
    let mut f = text.split('|');
    s.q = f.next().map(str::to_owned);
    s.rp = Some(f.take(3).map(str::to_owned).collect());
    Ok(s)
}

/// `attention RID SID approval|question|error|done ACTS GRANT_MASK TITLE|TEXT|OPT...`
fn parse_attention(rest: &str) -> Result<Attention, String> {
    let w: Vec<&str> = rest.splitn(6, ' ').collect();
    if w.len() < 6 {
        return Err("needs RID SID KIND ACTS GRANT TEXT".into());
    }
    let num = |s: &str| s.parse::<u64>().map_err(|_| format!("not a number: {s}"));
    let kind = match w[2] {
        "approval" => AttKind::Approval,
        "question" => AttKind::Question,
        "error" => AttKind::Error,
        "done" => AttKind::Done,
        _ => return Err("unknown kind".into()),
    };
    let acts = w[3]
        .split(',')
        .map(|a| by_name(&ACTIONS, a).ok_or_else(|| format!("unknown action {a}")))
        .collect::<Result<Vec<_>, _>>()?;
    let mut f = w[5].split('|');
    let title = f.next().filter(|t| !t.is_empty()).map(str::to_owned);
    let text = f.next().filter(|t| !t.is_empty()).map(str::to_owned);
    let opts: Vec<String> = f.map(str::to_owned).collect();
    Ok(Attention {
        rid: num(w[0])?,
        sid: num(w[1])?,
        kind,
        title,
        text,
        opts: (!opts.is_empty()).then_some(opts),
        acts,
        exp: None,
        grant_mask: Some(num(w[4])?),
        left: None,
        win: None,
        len: None,
        hush: None,
        rule: None,
        scope: None,
        wide: None,
        qs: None,
    })
}

/// The wire name of an enum value (its serde name, as in the vectors).
fn name<T: serde::Serialize>(v: &T) -> String {
    serde_json::to_value(v)
        .ok()
        .and_then(|v| v.as_str().map(str::to_owned))
        .unwrap_or_default()
}

fn by_name<T: serde::Serialize + Copy>(all: &[T], s: &str) -> Option<T> {
    all.iter().copied().find(|v| name(v) == s)
}

const ACTIONS: [Action; 6] = [
    Action::Approve,
    Action::Deny,
    Action::Focus,
    Action::Reply,
    Action::Ack,
    Action::Dictate,
];
const CMD_OPS: [CmdOp; 22] = {
    use CmdOp::*;
    [
        Focus, Ack, Snooze, Approve, Deny, Reply, Cancel, Detail, NotifyDone, Quiet, Scroll, Type,
        Dictate, KbPick, KbDel, KbSend, KbEnd, Rerun, Unqueue, Answer, Pointer, Graph,
    ]
};

// ── the user model (BENCH.md §5) ──

/// SplitMix64: a seeded run is the same run.
struct Rng(u64);

impl Rng {
    fn new(seed: u64) -> Self {
        Rng(seed ^ 0x9E37_79B9_7F4A_7C15)
    }

    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    fn unit(&mut self) -> f64 {
        ((self.next() >> 11) as f64 + 0.5) / (1u64 << 53) as f64
    }

    /// A standard normal draw (Box-Muller).
    fn normal(&mut self) -> f64 {
        (-2.0 * self.unit().ln()).sqrt() * (std::f64::consts::TAU * self.unit()).cos()
    }

    /// Where a fingertip lands around its target: σ 6 px, cut at 3σ.
    fn scatter_px(&mut self) -> i16 {
        (self.normal().clamp(-3.0, 3.0) * 6.0).round() as i16
    }

    /// A person's reaction before acting on what they see: log-normal, median 250 ms.
    fn reaction_ms(&mut self) -> u64 {
        (250.0 * (0.35 * self.normal()).exp()).round() as u64
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TASK: &str = "# approve\nconnect\nrx a4\nsession 2 input webapp feat/login\nwait 300\n\
        attention 7 2 approval approve,deny,focus 0 Run shell command|npm test\nwait 400\n\
        expect_screen ask\nmark 1\nhold 233 282 1600\nexpect_cmd approve\nbench stop\n";

    #[test]
    fn a_simulator_script_plays_with_its_simulator_lines_skipped() {
        let steps = parse(TASK).unwrap();
        let kinds: Vec<_> = steps
            .iter()
            .map(|(l, s)| (*l, std::mem::discriminant(s)))
            .collect();
        assert_eq!(kinds.len(), 8, "{steps:?}");
        assert_eq!(steps[0].0, 4, "line numbers are the file's");
        assert!(
            matches!(&steps[0].1, Step::Send(m) if matches!(&**m, Message::SessionUpd(s) if s.st == Some(SessState::Input)
            && s.repo.as_deref() == Some("webapp") && s.br.as_deref() == Some("feat/login")))
        );
        let Step::Send(m) = &steps[2].1 else { panic!() };
        let Message::Attention(a) = &**m else {
            panic!()
        };
        assert_eq!((a.rid, a.sid, a.kind), (7, 2, AttKind::Approval));
        assert_eq!(a.acts, vec![Action::Approve, Action::Deny, Action::Focus]);
        assert_eq!(
            (a.title.as_deref(), a.text.as_deref()),
            (Some("Run shell command"), Some("npm test"))
        );
        assert_eq!(steps[4].1, Step::ExpectScreen("ask".into()));
        assert_eq!(steps[6].1, Step::Input(Input::Hold(233, 282, 1600)));
        assert_eq!(steps[7].1, Step::ExpectCmd(CmdOp::Approve));
    }

    #[test]
    fn unknown_lines_and_double_presses_are_refused_with_their_line() {
        assert!(parse("wait 10\nfrobnicate 3\n")
            .unwrap_err()
            .starts_with("line 2:"));
        assert!(parse("btn A double\n").unwrap_err().contains("only btn"));
        assert!(parse("expect_cmd nothing\n").is_err());
    }

    fn touch(m: &Message) -> (u64, Vec<(i16, i16)>) {
        let Message::DbgTouch(t) = m else {
            panic!("{m:?}")
        };
        let pts = t.pts.chunks(4).map(|c| {
            (
                i16::from_le_bytes([c[0], c[1]]),
                i16::from_le_bytes([c[2], c[3]]),
            )
        });
        (t.dt, pts.collect())
    }

    #[test]
    fn inputs_become_strokes_timed_like_the_simulator() {
        let (m, busy) = Input::Tap(233, 282).message(None);
        assert_eq!((touch(&m), busy), ((50, vec![(233, 282)]), 100));
        let (m, busy) = Input::Hold(10, 20, 1600).message(None);
        assert_eq!((touch(&m), busy), ((1600, vec![(10, 20)]), 1750));
        let (m, busy) = Input::Swipe(100, 233, 300, 233, 160).message(None);
        let (dt, pts) = touch(&m);
        assert_eq!((dt, pts.len(), busy), (10, 17, 280));
        assert_eq!(
            (pts[0], pts[8], pts[16]),
            ((100, 233), (200, 233), (300, 233))
        );
        let (m, busy) = Input::Scrub(0, 90, 400, 200).message(None);
        let (dt, pts) = touch(&m);
        assert_eq!((dt, pts.len(), busy), (10, 41, 550));
        assert_eq!(
            (pts[0], pts[40]),
            ((233, 33), (433, 233)),
            "12 o'clock to 3 o'clock"
        );
    }

    #[test]
    fn the_user_model_is_seeded_and_lands_near_the_target() {
        let draw = |seed| {
            let mut r = Rng::new(seed);
            (0..2000)
                .map(|_| (r.scatter_px(), r.reaction_ms()))
                .collect::<Vec<_>>()
        };
        assert_eq!(draw(7), draw(7));
        assert_ne!(draw(7), draw(8));
        let d = draw(7);
        assert!(d.iter().all(|(s, _)| s.abs() <= 18), "cut at 3 sigma");
        let mut rt: Vec<u64> = d.iter().map(|(_, r)| *r).collect();
        rt.sort_unstable();
        let median = rt[rt.len() / 2];
        assert!((220..=280).contains(&median), "median reaction {median}");
        let sd = (d.iter().map(|(s, _)| (*s as f64).powi(2)).sum::<f64>() / d.len() as f64).sqrt();
        assert!((5.0..7.0).contains(&sd), "scatter sd {sd}");
    }
}
