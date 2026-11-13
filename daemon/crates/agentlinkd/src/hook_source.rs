//! `AgentSource` fed by `agentlink-hook` over the IPC socket. Also answers control
//! requests (`{"ctl":"status"}`) from the CLI on the same socket.
//!
//! Hook line: `{"agent","event","ts","cwd","env":{..},"ppid","tty","pids":[..],"payload":{..}}`.
//! `cwd` is the hook process's; the agent's own cwd, when sent, is `payload.cwd`.
//!
//! A line with `"await": {"wait_ms": N}` is a blocking permission or question hook: it is
//! opened on the approval broker and the connection stays up until a device answers or the
//! window runs out, then one reply line goes back (`{"decision": "allow"|"deny"|"none", ..}`).
//! The window is the daemon's (`window::answer_window`, ANSWER-WINDOW.md). A hook that says
//! `"window": true` (agentlink-hook 0.11 and later) first gets `{"window_ms": N}` and then a blank
//! keepalive line every second, so a hook that Claude Code killed is noticed and its card leaves
//! the watch; an older hook gets the decision line only, never later than its own `wait_ms`.
//! Any other event from the same session, except a notification, settles what was pending
//! there ("first answer wins": the terminal got there first).
//!
//! Copilot's hooks cannot answer, so its prompts are opened as cards by `prompt`, which
//! types a device answer into the terminal.
//!
//! The same line protocol is spoken on the remote sockets (see `remote`), after an auth line.

use crate::broker::{request_of, Broker, Outcome};
use crate::ipc;
use crate::jobs::Jobs;
use crate::prompt;
use crate::registry::{
    clip, HostCtx, LastMsg, Registry, SessionEvent, SessionPatch, Step, Turn, Via,
};
use crate::remote;
use crate::source::AgentSource;
use crate::trace::{Dir, Trace};
use agentlink_proto::{Agent, ClearWhy, InputSub, SessState};
use async_trait::async_trait;
use serde_json::{json, Value};
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;
use tokio::io::{AsyncBufReadExt, AsyncRead, AsyncWrite, AsyncWriteExt, BufReader};

pub type StatusFn = Arc<dyn Fn() -> Value + Send + Sync>;

/// How often a waiting hook that understands it gets a blank keepalive line.
const KEEPALIVE: Duration = Duration::from_secs(1);

pub struct HookSource {
    pub socket: PathBuf,
    pub remote_socket: PathBuf,
    pub token_file: PathBuf,
    /// Directory of the attach sockets.
    pub home: PathBuf,
    pub trace: Trace,
    pub status: StatusFn,
    pub broker: Arc<Broker>,
    pub jobs: Arc<Jobs>,
    pub ota: Arc<crate::ota::Ota>,
}

/// What every connection handler needs.
pub struct ConnEnv {
    pub registry: Arc<Registry>,
    pub broker: Arc<Broker>,
    pub trace: Trace,
    pub status: StatusFn,
    pub token_file: PathBuf,
    pub home: PathBuf,
    pub jobs: Arc<Jobs>,
    pub ota: Arc<crate::ota::Ota>,
}

/// Which socket a connection came in on.
#[derive(Clone, Copy)]
pub enum Peer {
    Local,
    /// `attach` is the attach socket's id; `None` for `remote.sock`.
    Remote {
        attach: Option<u64>,
    },
}

#[async_trait]
impl AgentSource for HookSource {
    fn name(&self) -> &'static str {
        "hooks"
    }

    async fn run(self: Box<Self>, registry: Arc<Registry>) -> anyhow::Result<()> {
        let listener = ipc::bind(&self.socket)?;
        tracing::info!("hook socket listening on {}", self.socket.display());
        let env = Arc::new(ConnEnv {
            registry,
            broker: self.broker,
            trace: self.trace,
            status: self.status,
            token_file: self.token_file,
            home: self.home,
            jobs: self.jobs,
            ota: self.ota,
        });
        remote::spawn_remote_socket(&self.remote_socket, env.clone());
        loop {
            let stream = listener.accept().await?;
            tokio::spawn(handle_conn(stream, env.clone(), Peer::Local));
        }
    }
}

pub async fn handle_conn<S>(stream: S, env: Arc<ConnEnv>, peer: Peer)
where
    S: AsyncRead + AsyncWrite + Unpin + Send + 'static,
{
    let ConnEnv {
        registry,
        broker,
        trace,
        status,
        jobs,
        ..
    } = &*env;
    let (r, mut w) = tokio::io::split(stream);
    let mut lines = BufReader::new(r).lines();
    let via = match peer {
        Peer::Local => Via::Local,
        Peer::Remote { attach } => match remote::authenticate(&mut lines, &env, attach).await {
            Some(via) => via,
            None => return,
        },
    };
    let src = if via == Via::Local { "hook" } else { "remote" };
    let prompts = prompt::Env::system(registry.clone(), broker.clone(), trace.clone());
    let mut attach_guard = None;
    while let Ok(Some(line)) = lines.next_line().await {
        let Ok(v) = serde_json::from_str::<Value>(&line) else {
            // a malformed shell line may still hold the command line, a Stop line the agent's message
            let line = if line.contains("\"shell\"") || line.contains("last_assistant_message") {
                "<private>".into()
            } else {
                line
            };
            trace.record(
                src,
                Dir::In,
                "invalid_json",
                None,
                None,
                Value::String(line),
                None,
            );
            continue;
        };
        if v.get("ctl").is_some() && via != Via::Local {
            trace.record(src, Dir::Internal, "ctl_ignored", None, None, v, None);
            continue;
        }
        if v.get("ctl").and_then(Value::as_str) == Some("ota") {
            run_ota(&env.ota, &v, &mut lines, &mut w).await;
            continue;
        }
        let reply = match v.get("ctl").and_then(Value::as_str) {
            Some("status") => Some(status()),
            Some("remote_attach") => {
                let (reply, guard) = remote::open_attach(&env, &v);
                attach_guard = guard.or(attach_guard); // dropping the old guard closes it
                Some(reply)
            }
            _ => None,
        };
        if let Some(reply) = reply {
            let _ = w.write_all(format!("{reply}\n").as_bytes()).await;
            continue;
        }
        let event = v["event"].as_str().unwrap_or("?").to_owned();
        let mut traced = v.clone();
        crate::trace::redact_hook(&mut traced);
        trace.record(src, Dir::In, &event, None, None, traced, None);
        if v["agent"].as_str() == Some("shell") {
            // Jobs (jobs.rs) are this machine's own shells; a remote host's are not offered.
            if via == Via::Local {
                jobs.on_line(&v);
            }
            continue;
        }
        if event.eq_ignore_ascii_case("statusline") {
            on_statusline(registry, &v, &via);
            continue;
        }
        let key = session_key(&v, &via);
        if let Some(ev) = to_session_event(&v, &via) {
            registry.apply(ev);
        }
        let Some(wait) = v["await"]["wait_ms"].as_u64() else {
            // The terminal moved on: whatever this session had pending was answered there.
            // A notification is not a move: it may be the very prompt the card is about.
            if let Some(key) = &key {
                // Claude names the tool `tool_name`, Copilot `toolName`
                let tool = v["payload"]["tool_name"]
                    .as_str()
                    .or_else(|| v["payload"]["toolName"].as_str());
                if parent_of(&v).is_some() {
                    // A sub-agent moving on says nothing about the parent's open cards; its own
                    // tool ending answers only a card for that tool.
                    if event.to_ascii_lowercase().starts_with("posttooluse") && tool.is_some() {
                        broker.cancel_session(key, tool);
                    }
                    continue;
                }
                if !event.eq_ignore_ascii_case("notification") {
                    broker.cancel_session(
                        key,
                        if event.eq_ignore_ascii_case("posttooluse") {
                            tool
                        } else {
                            None
                        },
                    );
                }
                prompt::on_event(&prompts, &v, key);
            }
            continue;
        };
        let outcome = await_decision(
            Ctx {
                registry,
                broker,
                trace,
            },
            &v,
            key,
            Duration::from_millis(wait),
            &via,
            &mut w,
        )
        .await;
        let reply = format!("{}\n", serde_json::to_string(&outcome).unwrap_or_default());
        let _ = w.write_all(reply.as_bytes()).await;
    }
    drop(attach_guard);
}

/// `{"ctl":"ota"}`: the update runs while this connection stays open, one progress line per event.
/// Closing the connection stops it (the update sends the stop itself before it ends).
async fn run_ota<R, W>(ota: &crate::ota::Ota, v: &Value, lines: &mut tokio::io::Lines<R>, w: &mut W)
where
    R: tokio::io::AsyncBufRead + Unpin,
    W: AsyncWrite + Unpin,
{
    let Some(req) = crate::ota::Request::from_json(v) else {
        let _ = w
            .write_all(b"{\"ota\":\"error\",\"msg\":\"missing path\"}\n")
            .await;
        return;
    };
    let (tx, rx) = tokio::sync::mpsc::unbounded_channel();
    let mut rx = Some(rx);
    let mut run = std::pin::pin!(ota.run(req, tx));
    let mut eof = false;
    loop {
        tokio::select! {
            _ = &mut run => break,
            line = async {
                match rx.as_mut() {
                    Some(r) => r.recv().await,
                    None => std::future::pending().await,
                }
            }, if rx.is_some() => {
                let Some(line) = line else { rx = None; continue };
                if w.write_all(format!("{line}\n").as_bytes()).await.is_err() {
                    rx = None;
                }
            }
            l = lines.next_line(), if !eof && rx.is_some() => {
                if !matches!(l, Ok(Some(_))) {
                    eof = true;
                    rx = None;
                }
            }
        }
    }
    // the run's last lines are already queued
    if let Some(mut rx) = rx {
        while let Ok(line) = rx.try_recv() {
            let _ = w.write_all(format!("{line}\n").as_bytes()).await;
        }
    }
}

/// A status-line run (`agentlink-hook statusline`, usage.rs): readouts for its session, and the plan's
/// rate limits when it runs on this machine (a remote host may be another account). It never creates a
/// session and never answers or cancels anything.
fn on_statusline(registry: &Registry, v: &Value, via: &Via) {
    let payload = &v["payload"];
    if payload["session_id"].as_str().is_none_or(str::is_empty) {
        return;
    }
    if let Some(key) = session_key(v, via) {
        registry.readout(&key, &crate::usage::readout_of(payload));
    }
    if *via == Via::Local {
        registry.record_usage(crate::usage::rates_of(payload));
    }
}

struct Ctx<'a> {
    registry: &'a Registry,
    broker: &'a Broker,
    trace: &'a Trace,
}

/// How a wait for an answer ended.
enum Ended {
    Answer(Box<Outcome>),
    /// The request was settled elsewhere (terminal answer, hand-back): no decision.
    Cancelled,
    Timeout,
    /// The hook stopped listening (Claude Code killed it at its own timeout, or the user acted in the
    /// terminal): the terminal has the prompt now.
    HookGone,
}

/// Opens the request and waits for the device, the terminal, or the clock. Never returns
/// `allow` without a device decision.
async fn await_decision<W: AsyncWrite + Unpin>(
    ctx: Ctx<'_>,
    v: &Value,
    key: Option<String>,
    hook_wait: Duration,
    via: &Via,
    w: &mut W,
) -> Outcome {
    let Ctx {
        registry,
        broker,
        trace,
    } = ctx;
    let agent = v["agent"].as_str().unwrap_or("");
    let event = v["event"].as_str().unwrap_or("");
    let Some(key) = key else {
        return Outcome::NONE;
    };
    let Some(sid) = registry.sid_of(&key) else {
        return Outcome::NONE;
    };
    let host = registry.host_of(sid);
    // A hook that asks for the window is told it and kept alive; an older one stops listening after
    // its own wait, so the window may not outlast that.
    let talks = v["await"]["window"].as_bool() == Some(true);
    let pinned = v["await"]["pinned"].as_bool() == Some(true);
    // Where the user is (presence.rs): shortens the window and hushes the buzz, nothing else.
    let (here, reading, focus) = crate::presence::global()
        .here_async(registry.host_ctx(sid))
        .await;
    let hush = crate::presence::hush(crate::presence::Event::Needs, here);
    let seen = json!({"here": here.name(), "presence": reading.state.name(),
        "source": reading.source, "focus": focus.name(), "hush": hush});
    let window = crate::window::answer_window(&crate::window::Ask {
        event,
        cwd: v["payload"]["cwd"].as_str(),
        remote: *via != Via::Local,
        pinned: pinned.then_some(hook_wait),
        devices: broker.devices(),
        here,
    });
    let window = if talks { window } else { window.min(hook_wait) };
    let Some(req) = request_of(sid, key, agent, event, &v["payload"], window) else {
        trace.record(
            "hook",
            Dir::Internal,
            "not_askable",
            Some(sid),
            None,
            json!({"event": event}),
            None,
        );
        return Outcome::NONE;
    };
    if req.notice {
        // shown on the watch until the session moves on; the terminal takes the prompt now
        let (rid, _rx) = broker.open_with(req, hush);
        trace.record(
            "hook",
            Dir::Internal,
            "notice",
            Some(sid),
            Some(rid),
            json!({"event": event, "seen": seen}),
            None,
        );
        return Outcome::NONE;
    }
    if window.is_zero() {
        // the user set the window to 0: the terminal asks at once, the watch is not offered it
        trace.record(
            "hook",
            Dir::Internal,
            "no_window",
            Some(sid),
            None,
            json!({"event": event}),
            None,
        );
        return Outcome::NONE;
    }
    let (rid, rx) = broker.open_with(req, hush);
    trace.record(
        "hook",
        Dir::Internal,
        "window",
        Some(sid),
        Some(rid),
        json!({"event": event, "window_ms": window.as_millis() as u64, "devices": broker.devices(), "seen": seen}),
        None,
    );
    let ended = wait_for(rx, window, talks, w).await;
    let (from, why, outcome) = match ended {
        Ended::Answer(o) => ("device", None, *o),
        Ended::Cancelled => ("none", Some("cancelled"), Outcome::NONE),
        Ended::Timeout => {
            broker.cancel(rid, ClearWhy::Expired);
            ("none", Some("timeout"), Outcome::NONE)
        }
        Ended::HookGone => {
            broker.cancel(rid, ClearWhy::Expired);
            ("none", Some("hook_gone"), Outcome::NONE)
        }
    };
    // an answer's labels and dictated words are the user's content: the audit line says only that
    // there was one
    let mut said = serde_json::to_value(&outcome).unwrap_or_default();
    for k in ["answer", "answers"] {
        if let Some(x) = said.get_mut(k) {
            *x = Value::String("<private>".into());
        }
    }
    let detail = match why {
        None => json!({"from": from, "outcome": said, "host": host}),
        Some(why) => json!({"from": from, "why": why, "host": host}),
    };
    trace.audit("decision", Some(sid), Some(rid), detail);
    outcome
}

/// Waits for the device's answer until `window` ends. With `talks`, the hook first gets the window
/// and then a keepalive line every [`KEEPALIVE`]; a failed write means the hook is gone.
async fn wait_for<W: AsyncWrite + Unpin>(
    mut rx: tokio::sync::oneshot::Receiver<Outcome>,
    window: Duration,
    talks: bool,
    w: &mut W,
) -> Ended {
    let deadline = tokio::time::Instant::now() + window;
    if talks {
        let line = format!("{}\n", json!({"window_ms": window.as_millis() as u64}));
        if w.write_all(line.as_bytes()).await.is_err() || w.flush().await.is_err() {
            return Ended::HookGone;
        }
    }
    let mut tick = tokio::time::interval_at(tokio::time::Instant::now() + KEEPALIVE, KEEPALIVE);
    loop {
        tokio::select! {
            r = &mut rx => return match r {
                Ok(o) => Ended::Answer(Box::new(o)),
                Err(_) => Ended::Cancelled,
            },
            _ = tokio::time::sleep_until(deadline) => return Ended::Timeout,
            _ = tick.tick(), if talks => {
                if w.write_all(b"\n").await.is_err() || w.flush().await.is_err() {
                    return Ended::HookGone;
                }
            }
        }
    }
}

/// A Copilot sub-agent's (the `task` tool) parent session id. Its hooks carry their own
/// `sessionId` plus `parentAgentId`; it runs inside the parent's process and terminal, so the
/// watch shows it as part of the parent, never as a session of its own (issue 1210).
fn parent_of(v: &Value) -> Option<&str> {
    let p = &v["payload"];
    let parent = p["parentAgentId"].as_str().filter(|s| !s.is_empty())?;
    (p["sessionId"].as_str() != Some(parent)).then_some(parent)
}

/// `agent:session` key of a hook line (prefixed for remote ones), as `to_session_event` derives it.
/// A sub-agent's lines get its parent's key.
pub fn session_key(v: &Value, via: &Via) -> Option<String> {
    let agent_name = v["agent"].as_str()?;
    let payload = &v["payload"];
    let cwd = payload["cwd"]
        .as_str()
        .or_else(|| v["cwd"].as_str())
        .filter(|c| !c.is_empty());
    let sid = parent_of(v)
        .or_else(|| payload["sessionId"].as_str())
        .or_else(|| payload["session_id"].as_str())
        .map(str::to_owned)
        .unwrap_or_else(|| format!("{}#{}", cwd.unwrap_or("?"), v["ppid"].as_u64().unwrap_or(0)));
    Some(via.key(&format!("{agent_name}:{sid}")))
}

fn agent_of(name: &str) -> Agent {
    match name {
        "copilot" => Agent::Copilot,
        "claude" => Agent::Claude,
        "cloud" => Agent::Cloud,
        _ => Agent::Other,
    }
}

pub(crate) fn branch_of(cwd: &str) -> Option<String> {
    let head = std::path::Path::new(cwd)
        .ancestors()
        .find_map(|d| std::fs::read_to_string(d.join(".git/HEAD")).ok())?;
    head.trim()
        .strip_prefix("ref: refs/heads/")
        .map(str::to_owned)
}

/// Maps a hook line to a registry event. Pure apart from reading `.git/HEAD`.
pub fn to_session_event(v: &Value, via: &Via) -> Option<SessionEvent> {
    let agent_name = v["agent"].as_str()?;
    let event = v["event"].as_str()?.to_ascii_lowercase();
    let payload = &v["payload"];
    // The payload's cwd is the agent's; the top-level one is the hook process's own.
    let cwd = payload["cwd"]
        .as_str()
        .or_else(|| v["cwd"].as_str())
        .filter(|c| !c.is_empty())
        .map(str::to_owned);
    let key = session_key(v, via)?;
    // A sub-agent's own lifecycle (prompt, stop, idle, end, error) is not its parent's: only its
    // tool steps and anything that needs the user reach the parent.
    let sub_agent = parent_of(v).map(|_| payload["agentType"].as_str().unwrap_or("agent"));
    if sub_agent.is_some()
        && !matches!(
            event.as_str(),
            "pretooluse"
                | "posttooluse"
                | "posttoolusefailure"
                | "permissionrequest"
                | "notification"
        )
    {
        return None;
    }
    if event == "sessionend" {
        return Some(SessionEvent::End { key });
    }

    let mut p = SessionPatch::new(agent_of(agent_name));
    // Claude Code's permission mode rides on most hook payloads (GAUGES.md); not every event has it
    p.pm = payload["permission_mode"]
        .as_str()
        .and_then(agentlink_proto::PermMode::from_claude);
    let tool = payload["toolName"]
        .as_str()
        .or_else(|| payload["tool_name"].as_str());
    match event.as_str() {
        "sessionstart" => p.state = Some(SessState::Start),
        "userpromptsubmitted" | "userpromptsubmit" => {
            p.state = Some(SessState::Work);
            p.step = Step::Reset;
            p.ask = Some(Default::default()); /* the question was answered (or moved past) */
            p.turn = Turn::Prompt;
            p.last = LastMsg::Clear; /* it described a turn that is over */
        }
        "posttooluse" | "posttoolusefailure" => {
            p.state = Some(SessState::Work);
            p.step = Step::Result(tool_ok(&event, payload));
            if agent_name == "claude" {
                if let Some(fx) = crate::turn::effect_of(&event, payload) {
                    p.turn = Turn::Tool(fx);
                }
            }
        }
        "pretooluse" if matches!(tool, Some("AskUserQuestion" | "ask_user")) => {
            p.state = Some(SessState::Input);
            p.sub = Some(InputSub::Question);
            p.tool = tool.map(str::to_owned);
        }
        "pretooluse" => {
            p.state = Some(SessState::Work);
            p.ask = Some(Default::default());
            p.tool = tool.map(str::to_owned);
            p.summary = tool.map(|t| step_summary(t, payload));
            p.step = Step::Start;
        }
        "agentstop" | "stop" => {
            p.state = Some(SessState::Done);
            // Claude Code hands the turn's last message to Stop: its closing question feeds Reply
            if let Some(text) = payload["last_assistant_message"].as_str() {
                p.ask = Some(crate::ask::ask_of(text));
                // the message itself, for the watch's reader (LAST-MESSAGE.md); Copilot's agentStop has none
                if event == "stop" && agent_name == "claude" {
                    let clean = crate::lastmsg::clean(text);
                    p.last = if clean.is_empty() {
                        LastMsg::Clear
                    } else {
                        LastMsg::Set(clean)
                    };
                }
            }
        }
        "erroroccurred" | "stopfailure" => p.state = Some(SessState::Error),
        "permissionrequest" => {
            p.state = Some(SessState::Input);
            // the permission to show a question (AskUserQuestion) means the question is on screen
            p.sub = Some(if tool == Some("AskUserQuestion") {
                InputSub::Question
            } else {
                InputSub::Approval
            });
            p.tool = tool.map(str::to_owned);
        }
        "notification" => {
            let kind = payload["notification_type"].as_str().unwrap_or("");
            match kind {
                "permission_prompt" => {
                    p.state = Some(SessState::Input);
                    p.sub = Some(InputSub::Approval);
                }
                "elicitation_dialog" => {
                    p.state = Some(SessState::Input);
                    p.sub = Some(InputSub::Question);
                }
                "agent_idle" => p.state = Some(SessState::Idle),
                "agent_completed" => p.state = Some(SessState::Done),
                _ => return None,
            }
        }
        _ => return None,
    }
    if let Some(name) = sub_agent {
        match p.state {
            // the parent's own state and closing question stay as they are
            Some(SessState::Work) => {
                p.state = None;
                p.ask = None;
            }
            Some(SessState::Idle | SessState::Done) => return None,
            _ => {}
        }
        p.summary = p.summary.map(|s| format!("{name}: {s}"));
    }
    if let Some(c) = &cwd {
        p.repo = repo_of(c);
        // A remote path means nothing on this machine: trust the hook's own branch instead.
        p.branch = match via {
            Via::Local => branch_of(c),
            Via::Remote { .. } => v["branch"].as_str().map(|b| clip(b, 32)),
        };
    }
    Some(SessionEvent::Update {
        key,
        via: via.clone(),
        patch: p,
        ctx: host_ctx(v, cwd),
    })
}

/// Whether the tool that just ended succeeded.
fn tool_ok(event: &str, payload: &Value) -> bool {
    if event == "posttoolusefailure" {
        return false;
    }
    !matches!(
        payload["toolResult"]["resultType"].as_str(),
        Some("failure" | "denied" | "rejected" | "error")
    )
}

/// The tool's arguments: Copilot sends `toolArgs` as an object or a JSON-encoded string,
/// Claude `tool_input` as an object.
fn tool_args(payload: &Value) -> Value {
    let raw = if payload.get("toolArgs").is_some() {
        &payload["toolArgs"]
    } else {
        &payload["tool_input"]
    };
    match raw {
        Value::String(s) => serde_json::from_str(s).unwrap_or(Value::Null),
        v => v.clone(),
    }
}

fn first_line(s: &str) -> Option<&str> {
    s.lines().map(str::trim).find(|l| !l.is_empty())
}

/// `"<tool> · <argument>"` (≤ 80 chars): the command's first line, a file's last two path
/// components, a URL or a pattern; the bare tool name when there is none.
fn step_summary(tool: &str, payload: &Value) -> String {
    let args = tool_args(payload);
    let text = |k: &str| args[k].as_str();
    let arg = text("command")
        .and_then(first_line)
        .map(str::to_owned)
        .or_else(|| {
            ["file_path", "path", "filePath"]
                .iter()
                .find_map(|k| text(k))
                .and_then(|p| {
                    let parts: Vec<&str> = p.split(['/', '\\']).filter(|c| !c.is_empty()).collect();
                    let tail = parts.len().saturating_sub(2);
                    (!parts.is_empty()).then(|| parts[tail..].join("/"))
                })
        })
        .or_else(|| text("url").and_then(first_line).map(str::to_owned))
        .or_else(|| text("pattern").and_then(first_line).map(str::to_owned))
        .or_else(|| text("query").and_then(first_line).map(str::to_owned))
        .or_else(|| text("description").and_then(first_line).map(str::to_owned))
        .or_else(|| text("prompt").and_then(first_line).map(str::to_owned));
    match arg {
        Some(a) => clip(&format!("{tool} · {a}"), 80),
        None => clip(tool, 80),
    }
}

/// Last path component, splitting on both separators (remote paths may be Windows ones).
pub(crate) fn repo_of(cwd: &str) -> Option<String> {
    let name = cwd.rsplit(['/', '\\']).find(|c| !c.is_empty())?;
    Some(name.to_owned())
}

/// `env`, `tty` and `pids` of a hook line or an attach request's `ctx`.
pub fn host_ctx(v: &Value, cwd: Option<String>) -> HostCtx {
    HostCtx {
        cwd,
        env: v["env"]
            .as_object()
            .map(|m| {
                m.iter()
                    .filter_map(|(k, v)| Some((k.clone(), v.as_str()?.to_owned())))
                    .collect()
            })
            .unwrap_or_default(),
        tty: v["tty"].as_str().map(str::to_owned),
        pids: v["pids"]
            .as_array()
            .map(|a| {
                a.iter()
                    .filter_map(|p| u32::try_from(p.as_u64()?).ok())
                    .collect()
            })
            .unwrap_or_default(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    use crate::broker::Decision;
    use agentlink_proto::ResultErr;
    use tokio::io::AsyncReadExt;

    /// The window tests never read the real user's Claude Code settings or daemon setting.
    fn isolate() {
        std::env::set_var("CLAUDE_CONFIG_DIR", "/nonexistent/agentlink-test-claude");
        std::env::set_var("AGENTLINK_ANSWER_WINDOW_S", "60");
    }

    struct Rig {
        registry: Arc<Registry>,
        broker: Arc<Broker>,
        trace: Trace,
    }

    fn rig() -> Rig {
        isolate();
        let trace = Trace::null();
        let registry = Arc::new(Registry::new(trace.clone()));
        let broker = Arc::new(Broker::new(registry.clone()));
        Rig {
            registry,
            broker,
            trace,
        }
    }

    /// A PermissionRequest line from a hook that `talks` (agentlink-hook 0.11) or not, with `wait_ms`.
    fn perm_line(rig: &Rig, wait_ms: u64, talks: bool, pinned: bool) -> (Value, Option<String>) {
        let mut v = json!({"agent": "claude", "event": "PermissionRequest", "cwd": "/nonexistent/p",
            "payload": {"session_id": "s1", "cwd": "/nonexistent/p", "tool_name": "Bash",
                        "tool_input": {"command": "npm test"}},
            "await": {"wait_ms": wait_ms}});
        if talks {
            v["await"]["window"] = json!(true);
            v["await"]["pinned"] = json!(pinned);
        }
        if let Some(ev) = to_session_event(&v, &Via::Local) {
            rig.registry.apply(ev);
        }
        let key = session_key(&v, &Via::Local);
        (v, key)
    }

    async fn run(
        rig: &Rig,
        v: &Value,
        key: Option<String>,
        w: &mut tokio::io::DuplexStream,
    ) -> Outcome {
        let wait = Duration::from_millis(v["await"]["wait_ms"].as_u64().unwrap());
        await_decision(
            Ctx {
                registry: &rig.registry,
                broker: &rig.broker,
                trace: &rig.trace,
            },
            v,
            key,
            wait,
            &Via::Local,
            w,
        )
        .await
    }

    async fn read_all(mut r: tokio::io::DuplexStream) -> String {
        let mut s = String::new();
        let _ = r.read_to_string(&mut s).await;
        s
    }

    #[tokio::test]
    async fn a_talking_hook_hears_the_window_then_keepalives_and_silence_decides_nothing() {
        let rig = rig();
        let _dev = rig.broker.link();
        // a pinned 1.5 s is raised to the shortest window there is
        let (v, key) = perm_line(&rig, 1500, true, true);
        let (mut w, r) = tokio::io::duplex(4096);
        let reader = tokio::spawn(read_all(r));
        let t0 = std::time::Instant::now();
        let out = run(&rig, &v, key, &mut w).await;
        assert_eq!(
            out,
            Outcome::NONE,
            "the window ran out: no decision, the terminal asks"
        );
        assert!(t0.elapsed() >= crate::window::MIN);
        drop(w);
        let said = reader.await.unwrap();
        let mut lines = said.lines();
        assert_eq!(lines.next(), Some("{\"window_ms\":5000}"));
        assert!(
            lines.all(str::is_empty),
            "only keepalives after the window line: {said:?}"
        );
        assert!(said.matches('\n').count() >= 5, "a keepalive every second");
        // the expired request cannot be answered any more
        assert!(rig.broker.pending().is_empty());
    }

    #[tokio::test]
    async fn an_answer_inside_the_window_is_the_decision() {
        let rig = rig();
        let _dev = rig.broker.link();
        let (v, key) = perm_line(&rig, 30_000, true, true);
        let (mut w, r) = tokio::io::duplex(4096);
        let reader = tokio::spawn(read_all(r));
        let broker = rig.broker.clone();
        let answer = tokio::spawn(async move {
            loop {
                if let Some(a) = broker.pending().pop() {
                    assert_eq!(a.win, Some(30_000));
                    return broker.answer(a.rid, Decision::Deny).map(|o| o.decision);
                }
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
        });
        let out = run(&rig, &v, key, &mut w).await;
        assert_eq!(out.decision, "deny");
        assert_eq!(answer.await.unwrap(), Ok("deny"));
        drop(w);
        assert!(reader.await.unwrap().starts_with("{\"window_ms\":30000}"));
    }

    #[tokio::test]
    async fn a_hook_that_stopped_listening_takes_its_card_off_the_watch() {
        let rig = rig();
        let _dev = rig.broker.link();
        let (v, key) = perm_line(&rig, 60_000, true, true);
        let (mut w, r) = tokio::io::duplex(4096);
        let broker = rig.broker.clone();
        // Claude Code kills the hook a moment after the card went out
        let killer = tokio::spawn(async move {
            while broker.pending().is_empty() {
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
            let rid = broker.pending()[0].rid;
            tokio::time::sleep(Duration::from_millis(100)).await;
            drop(r);
            rid
        });
        let t0 = std::time::Instant::now();
        let out = run(&rig, &v, key, &mut w).await;
        let rid = killer.await.unwrap();
        assert_eq!(out, Outcome::NONE);
        assert!(
            t0.elapsed() < Duration::from_secs(5),
            "noticed at the next keepalive, not at the window's end"
        );
        assert!(rig.broker.pending().is_empty());
        assert_eq!(
            rig.broker.answer(rid, Decision::Approve).unwrap_err().0,
            ResultErr::Stale,
            "a hold that lands after the hook died answers nothing"
        );
    }

    #[tokio::test]
    async fn an_older_hook_is_never_held_past_its_own_wait() {
        let rig = rig();
        let _dev = rig.broker.link();
        let (v, key) = perm_line(&rig, 700, false, false);
        let (mut w, r) = tokio::io::duplex(4096);
        let reader = tokio::spawn(read_all(r));
        let broker = rig.broker.clone();
        let seen = tokio::spawn(async move {
            loop {
                if let Some(a) = broker.pending().pop() {
                    return a.win;
                }
                tokio::time::sleep(Duration::from_millis(5)).await;
            }
        });
        let t0 = std::time::Instant::now();
        assert_eq!(run(&rig, &v, key, &mut w).await, Outcome::NONE);
        assert!(
            t0.elapsed() < Duration::from_millis(1500),
            "the daemon's 60 s would outlast its read"
        );
        assert_eq!(seen.await.unwrap(), Some(700));
        drop(w);
        assert_eq!(
            reader.await.unwrap(),
            "",
            "no window line, no keepalives: it reads one line only"
        );
    }

    #[tokio::test]
    async fn no_watch_linked_means_a_short_hold_and_zero_means_none() {
        let rig = rig();
        let (v, key) = perm_line(&rig, 120_000, true, true);
        let broker = rig.broker.clone();
        let seen = tokio::spawn(async move {
            loop {
                if let Some(a) = broker.pending().pop() {
                    broker.cancel(a.rid, ClearWhy::AnsweredPc);
                    return a.win;
                }
                tokio::time::sleep(Duration::from_millis(5)).await;
            }
        });
        let (mut w, _r) = tokio::io::duplex(4096);
        assert_eq!(run(&rig, &v, key, &mut w).await, Outcome::NONE);
        assert_eq!(
            seen.await.unwrap(),
            Some(crate::window::NO_DEVICE.as_millis() as u64)
        );
        // AGENTLINK_WAIT_MS=0 on the hook: no card, no hold
        let _dev = rig.broker.link();
        let (v, key) = perm_line(&rig, 0, true, true);
        let (mut w, _r) = tokio::io::duplex(4096);
        assert_eq!(run(&rig, &v, key, &mut w).await, Outcome::NONE);
        assert!(rig.broker.pending().is_empty());
    }

    #[test]
    fn copilot_permission_notification_is_input_approval() {
        let v = json!({"agent":"copilot","event":"notification","cwd":"/nonexistent/proj",
                       "payload":{"sessionId":"s1","notification_type":"permission_prompt"}});
        let Some(SessionEvent::Update { key, patch, .. }) = to_session_event(&v, &Via::Local)
        else {
            panic!()
        };
        assert_eq!(key, "copilot:s1");
        assert_eq!(
            (patch.state, patch.sub),
            (Some(SessState::Input), Some(InputSub::Approval))
        );
        assert_eq!(patch.repo.as_deref(), Some("proj"));
    }

    #[test]
    fn payload_cwd_beats_hook_process_cwd_and_ctx_is_captured() {
        let v = json!({"agent":"claude","event":"Stop","cwd":"/hook/proc/dir","tty":"/dev/ttys004",
                       "pids":[10,20],"env":{"TERM_PROGRAM":"kitty"},
                       "payload":{"session_id":"s","cwd":"/nonexistent/agent-proj"}});
        let Some(SessionEvent::Update { patch, ctx, .. }) = to_session_event(&v, &Via::Local)
        else {
            panic!()
        };
        assert_eq!(patch.repo.as_deref(), Some("agent-proj"));
        assert_eq!(ctx.cwd.as_deref(), Some("/nonexistent/agent-proj"));
        assert_eq!(ctx.tty.as_deref(), Some("/dev/ttys004"));
        assert_eq!(ctx.pids, vec![10, 20]);
        assert_eq!(ctx.env["TERM_PROGRAM"], "kitty");
        let v =
            json!({"agent":"claude","event":"Stop","cwd":"/hook/dir","payload":{"session_id":"s"}});
        let Some(SessionEvent::Update { patch, .. }) = to_session_event(&v, &Via::Local) else {
            panic!()
        };
        assert_eq!(patch.repo.as_deref(), Some("dir"));
    }

    fn patch_of(v: Value) -> SessionPatch {
        let Some(SessionEvent::Update { patch, .. }) = to_session_event(&v, &Via::Local) else {
            panic!()
        };
        patch
    }

    #[test]
    fn copilot_sub_agents_fold_into_their_parent() {
        let sub = |event: &str, extra: Value| {
            let mut payload = json!({"sessionId":"child","parentAgentId":"parent",
                "agentId":"child","agentType":"explore","cwd":"/r"});
            payload
                .as_object_mut()
                .unwrap()
                .extend(extra.as_object().unwrap().clone());
            json!({"agent":"copilot","event":event,"payload":payload})
        };
        let pre = sub(
            "preToolUse",
            json!({"toolName":"bash","toolArgs":{"command":"ls"}}),
        );
        assert_eq!(
            session_key(&pre, &Via::Local).as_deref(),
            Some("copilot:parent")
        );
        let Some(SessionEvent::Update { key, patch, .. }) = to_session_event(&pre, &Via::Local)
        else {
            panic!()
        };
        assert_eq!(key, "copilot:parent");
        assert_eq!(patch.state, None, "the parent's state is its own");
        assert!(patch.ask.is_none(), "nor is its closing question cleared");
        assert_eq!(patch.step, Step::Start);
        assert_eq!(patch.summary.as_deref(), Some("explore: bash · ls"));
        let post = sub("postToolUse", json!({"toolName":"bash"}));
        assert_eq!(patch_of(post).step, Step::Result(true));
        for e in [
            "sessionStart",
            "userPromptSubmitted",
            "agentStop",
            "sessionEnd",
            "errorOccurred",
        ] {
            assert!(
                to_session_event(&sub(e, json!({})), &Via::Local).is_none(),
                "{e}"
            );
        }
        let idle = sub("notification", json!({"notification_type":"agent_idle"}));
        assert!(to_session_event(&idle, &Via::Local).is_none());
        let perm = sub("permissionRequest", json!({"toolName":"bash"}));
        assert_eq!(patch_of(perm).state, Some(SessState::Input));
        // a parent (no parentAgentId, or one naming itself) keeps its own key
        let own = json!({"agent":"copilot","event":"agentStop",
            "payload":{"sessionId":"parent","parentAgentId":"parent"}});
        assert_eq!(patch_of(own).state, Some(SessState::Done));
    }

    #[test]
    fn stop_carries_the_closing_question_and_a_new_prompt_clears_it() {
        let p = patch_of(json!({"agent":"claude","event":"Stop","cwd":"/r",
            "payload":{"session_id":"s","last_assistant_message":"Built and tested.\n\nShould I flash it now?"}}));
        assert_eq!(p.state, Some(SessState::Done));
        assert_eq!(
            p.ask,
            Some((
                "Should I flash it now?".into(),
                vec!["Yes, go ahead".into(), "No".into()]
            ))
        );
        let p = patch_of(
            json!({"agent":"claude","event":"UserPromptSubmit","cwd":"/r","payload":{"session_id":"s"}}),
        );
        assert_eq!(p.ask, Some((String::new(), vec![])));
        let p = patch_of(
            json!({"agent":"claude","event":"Stop","cwd":"/r","payload":{"session_id":"s"}}),
        );
        assert_eq!(p.ask, None, "no message: leave whatever was there");
    }

    #[test]
    fn a_stop_keeps_the_last_message_and_a_new_prompt_clears_it() {
        let reg = Registry::new(Trace::null());
        let mut rx = reg.subscribe().1;
        feed(&reg, &cl("UserPromptSubmit", json!({"prompt": "go"})));
        let _ = drain(&mut rx);
        assert!(reg.snapshot()[0].lm.is_none(), "nothing to clear: no keys");
        let msg = "## Done\n\nBuilt **it**. 🎉\n\n- tests pass";
        feed(&reg, &cl("Stop", json!({"last_assistant_message": msg})));
        let ds = drain(&mut rx);
        let d = ds.last().unwrap();
        assert_eq!(d.st, Some(SessState::Done), "in the delta that enters done");
        let lm = d.lm.unwrap();
        assert!(lm > 0 && lm <= u64::from(u32::MAX));
        let sid = d.sid;
        let (id, text) = reg.last_message(sid).unwrap();
        assert_eq!(
            (id, text.as_str()),
            (lm, "Done\n\nBuilt it.\n\n• tests pass")
        );
        assert_eq!(
            (d.ll, d.lp.as_deref()),
            (Some(text.len() as u64), Some("Done Built it."))
        );
        let snap = &reg.snapshot()[0];
        assert_eq!((snap.lm, snap.ll), (Some(lm), Some(text.len() as u64)));
        // another stop: another id
        feed(
            &reg,
            &cl("Stop", json!({"last_assistant_message": "Again."})),
        );
        let d = drain(&mut rx).pop().unwrap();
        assert!(d.lm.is_some_and(|n| n != lm && n != 0));
        // the next prompt clears it, once
        feed(&reg, &cl("UserPromptSubmit", json!({"prompt": "more"})));
        let d = drain(&mut rx).pop().unwrap();
        assert_eq!((d.lm, d.ll, d.lp.as_deref()), (Some(0), Some(0), Some("")));
        assert!(reg.last_message(sid).is_none());
        assert_eq!(reg.snapshot()[0].lm, None);
        feed(&reg, &cl("UserPromptSubmit", json!({"prompt": "more"})));
        assert!(drain(&mut rx).iter().all(|d| d.lm.is_none()));
        // a stop with nothing readable leaves no message
        feed(&reg, &cl("Stop", json!({"last_assistant_message": "🎉"})));
        assert!(reg.last_message(sid).is_none());
        // gone with the session
        feed(&reg, &cl("Stop", json!({"last_assistant_message": "kept"})));
        assert!(reg.last_message(sid).is_some());
        feed(&reg, &cl("SessionEnd", json!({})));
        assert!(reg.last_message(sid).is_none());
    }

    #[test]
    fn an_error_a_sub_agent_or_copilot_leaves_no_last_message() {
        let p = patch_of(cl(
            "StopFailure",
            json!({"last_assistant_message": "API Error: 529"}),
        ));
        assert_eq!(
            p.last,
            LastMsg::Keep,
            "the error text is not the agent's message"
        );
        let p = patch_of(json!({"agent":"copilot","event":"agentStop","cwd":"/r",
            "payload":{"sessionId":"c","last_assistant_message":"x"}}));
        assert_eq!(p.last, LastMsg::Keep);
        let p = patch_of(cl("Stop", json!({})));
        assert_eq!(
            p.last,
            LastMsg::Keep,
            "no message: leave whatever was there"
        );
        // a Copilot sub-agent's stop never reaches its parent at all
        let sub = json!({"agent":"copilot","event":"agentStop","payload":{"sessionId":"child",
            "parentAgentId":"parent","last_assistant_message":"x"}});
        assert!(to_session_event(&sub, &Via::Local).is_none());
    }

    #[tokio::test]
    async fn a_question_is_held_and_its_answers_go_to_the_hook_but_not_to_the_audit_log() {
        isolate();
        let dir = std::env::temp_dir().join(format!("al-hq-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let trace = Trace::new(dir.join("trace"), dir.join("audit.jsonl"));
        let registry = Arc::new(Registry::new(trace.clone()));
        let rig = Rig {
            broker: Arc::new(Broker::new(registry.clone())),
            registry,
            trace,
        };
        let _dev = rig.broker.link();
        let v = json!({"agent": "claude", "event": "PermissionRequest", "cwd": "/nonexistent/p",
            "payload": {"session_id": "s1", "cwd": "/nonexistent/p", "tool_name": "AskUserQuestion",
                "tool_input": {"questions": [{"question": "Which framework?", "header": "Framework",
                    "options": [{"label": "React"}, {"label": "Vue"}]}]}},
            "await": {"wait_ms": 30_000, "window": true}});
        rig.registry
            .apply(to_session_event(&v, &Via::Local).unwrap());
        let key = session_key(&v, &Via::Local);
        let broker = rig.broker.clone();
        let answer = tokio::spawn(async move {
            loop {
                if let Some(a) = broker.pending().pop() {
                    return broker
                        .answer_questions(a.rid, &[2], &[None])
                        .map(|o| o.decision);
                }
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
        });
        let (mut w, _r) = tokio::io::duplex(4096);
        let out = run(&rig, &v, key, &mut w).await;
        assert_eq!(answer.await.unwrap(), Ok("allow"));
        assert_eq!(
            serde_json::to_value(&out).unwrap(),
            json!({"decision": "allow", "answers": {"Which framework?": "Vue"}})
        );
        let log = std::fs::read_to_string(dir.join("audit.jsonl")).unwrap();
        let _ = std::fs::remove_dir_all(&dir);
        assert!(log.contains("\"decision\"") && log.contains("<private>"));
        assert!(!log.contains("Vue"), "{log}");
    }

    #[test]
    fn step_summary_from_copilot_and_claude_args() {
        let sum = |v: Value| patch_of(v).summary;
        let c = json!({"agent":"copilot","event":"preToolUse","payload":{"sessionId":"s",
            "toolName":"bash","toolArgs":"{\"command\":\"\\n  npm test\\nnext\"}"}});
        assert_eq!(sum(c).as_deref(), Some("bash · npm test"));
        let c = json!({"agent":"copilot","event":"preToolUse","payload":{"sessionId":"s",
            "toolName":"edit","toolArgs":{"path":"C:\\src\\proj\\lib\\a.rs"}}});
        assert_eq!(sum(c).as_deref(), Some("edit · lib/a.rs"));
        let c = json!({"agent":"claude","event":"PreToolUse","payload":{"session_id":"s",
            "tool_name":"Read","tool_input":{"file_path":"/a/b/c/d.txt"}}});
        let p = patch_of(c);
        assert_eq!(p.summary.as_deref(), Some("Read · c/d.txt"));
        assert_eq!(p.step, Step::Start);
        let c = json!({"agent":"claude","event":"PreToolUse","payload":{"session_id":"s",
            "tool_name":"Task","tool_input":{}}});
        assert_eq!(sum(c).as_deref(), Some("Task"));
        let ask = json!({"agent":"claude","event":"PreToolUse","payload":{"session_id":"s",
            "tool_name":"AskUserQuestion","tool_input":{"prompt":"x"}}});
        assert_eq!(patch_of(ask).step, Step::None);
    }

    #[test]
    fn step_results_and_prompt_reset() {
        let r = |agent: &str, ev: &str, payload: Value| {
            patch_of(json!({"agent":agent,"event":ev,"payload":payload})).step
        };
        assert_eq!(
            r("claude", "PostToolUseFailure", json!({"session_id":"s"})),
            Step::Result(false)
        );
        assert_eq!(
            r("claude", "PostToolUse", json!({"session_id":"s"})),
            Step::Result(true)
        );
        let res = |t: &str| json!({"sessionId":"s","toolResult":{"resultType":t}});
        assert_eq!(
            r("copilot", "postToolUse", res("denied")),
            Step::Result(false)
        );
        assert_eq!(
            r("copilot", "postToolUse", res("success")),
            Step::Result(true)
        );
        assert_eq!(
            r("copilot", "userPromptSubmitted", json!({"sessionId":"s"})),
            Step::Reset
        );
    }

    #[test]
    fn claude_session_end_removes_and_unknown_is_ignored() {
        let end = json!({"agent":"claude","event":"SessionEnd","payload":{"session_id":"c1"}});
        assert!(
            matches!(to_session_event(&end, &Via::Local), Some(SessionEvent::End { key }) if key == "claude:c1")
        );
        assert!(
            to_session_event(&json!({"agent":"claude","event":"Whatever"}), &Via::Local).is_none()
        );
    }

    #[test]
    fn remote_line_uses_its_own_branch_and_windows_repo_name() {
        let via = Via::Remote {
            host: "Win1".into(),
            os: "win".into(),
            attach: None,
        };
        let v = json!({"agent":"copilot","event":"Stop","branch":"feat/x","cwd":"C:\\src\\proj\\",
                       "payload":{"sessionId":"s1"}});
        let Some(SessionEvent::Update { key, patch, .. }) = to_session_event(&v, &via) else {
            panic!()
        };
        assert_eq!(key, "r:win1|copilot:s1");
        assert_eq!(patch.repo.as_deref(), Some("proj"));
        assert_eq!(patch.branch.as_deref(), Some("feat/x"));
        assert_eq!(session_key(&v, &via).as_deref(), Some(key.as_str()));
    }

    #[test]
    fn permission_mode_rides_on_the_hooks() {
        let p = patch_of(json!({"agent":"claude","event":"PreToolUse","cwd":"/r",
            "payload":{"session_id":"s","permission_mode":"acceptEdits","tool_name":"Edit"}}));
        assert_eq!(p.pm, Some(agentlink_proto::PermMode::AcceptEdits));
        let p = patch_of(json!({"agent":"claude","event":"Stop","cwd":"/r",
            "payload":{"session_id":"s","permission_mode":"bypassPermissions"}}));
        assert_eq!(p.pm, Some(agentlink_proto::PermMode::Bypass));
        let p = patch_of(json!({"agent":"claude","event":"Stop","cwd":"/r",
            "payload":{"session_id":"s","permission_mode":"someFutureMode"}}));
        assert_eq!(p.pm, None, "an unknown mode is not guessed");
        let p = patch_of(
            json!({"agent":"claude","event":"Stop","cwd":"/r","payload":{"session_id":"s"}}),
        );
        assert_eq!(p.pm, None);
    }

    #[test]
    fn status_line_feeds_known_sessions_and_local_rate_limits() {
        use crate::registry::Update;
        let reg = Registry::new(Trace::null());
        let mut rx = reg.subscribe().1;
        let now = crate::registry::now_unix();
        let line = |sid: &str| {
            json!({"agent":"claude","event":"statusline","cwd":"/r","payload":{
                "session_id": sid, "model": {"display_name": "Opus"},
                "context_window": {"used_percentage": 71.6},
                "rate_limits": {"five_hour": {"used_percentage": 12.5, "resets_at": now + 3600}},
                "pr": {"number": 9, "review_state": "approved"}}})
        };
        // no card yet: the readout is dropped, the account's figures are kept
        on_statusline(&reg, &line("s"), &Via::Local);
        assert!(
            reg.snapshot().is_empty(),
            "a status line never makes a card"
        );
        assert!(matches!(rx.try_recv(), Ok(Update::Usage(u)) if u.h5 == Some(12)));
        // once the hooks made the session, its readouts arrive as one delta
        reg.apply(
            to_session_event(
                &json!({"agent":"claude","event":"SessionStart","cwd":"/r","payload":{"session_id":"s"}}),
                &Via::Local,
            )
            .unwrap(),
        );
        let _ = rx.try_recv();
        on_statusline(&reg, &line("s"), &Via::Local);
        let Ok(Update::Upsert(d)) = rx.try_recv() else {
            panic!("readout delta")
        };
        assert_eq!(
            (d.ctx, d.mdl.as_deref(), d.pr, d.prs),
            (
                Some(71),
                Some("Opus"),
                Some(9),
                Some(agentlink_proto::PrState::Approved)
            )
        );
        assert!(rx.try_recv().is_err(), "unchanged figures are not re-sent");
        // the same line again: nothing new
        on_statusline(&reg, &line("s"), &Via::Local);
        assert!(rx.try_recv().is_err());
        // a remote host's rate limits are not this account's
        let via = Via::Remote {
            host: "box".into(),
            os: "linux".into(),
            attach: None,
        };
        let mut v = line("r");
        v["payload"]["rate_limits"]["five_hour"]["used_percentage"] = json!(99);
        on_statusline(&reg, &v, &via);
        assert!(rx.try_recv().is_err());
        // a line without a session id is ignored
        on_statusline(
            &reg,
            &json!({"agent":"claude","event":"statusline","payload":{}}),
            &Via::Local,
        );
        assert!(rx.try_recv().is_err());
    }

    /// One hook line of a Claude Code session `t`.
    fn cl(event: &str, payload: Value) -> Value {
        let mut p = payload;
        p["session_id"] = json!("t");
        json!({"agent":"claude","event":event,"cwd":"/nonexistent/proj","payload":p})
    }

    fn feed(reg: &Registry, v: &Value) {
        reg.apply(to_session_event(v, &Via::Local).unwrap());
    }

    fn drain(rx: &mut tokio::sync::broadcast::Receiver<crate::registry::Update>) -> Vec<Session> {
        let mut out = vec![];
        while let Ok(crate::registry::Update::Upsert(d)) = rx.try_recv() {
            out.push(d);
        }
        out
    }

    use agentlink_proto::Session;

    fn edit(path: &str, old: &str, new: &str) -> Value {
        cl(
            "PostToolUse",
            json!({"tool_name":"Edit","tool_input":{"file_path":path,"old_string":old,"new_string":new}}),
        )
    }

    #[test]
    fn a_whole_turn_ends_with_its_summary_and_the_next_prompt_clears_it() {
        let reg = Registry::new(Trace::null());
        let mut rx = reg.subscribe().1;
        feed(&reg, &cl("UserPromptSubmit", json!({"prompt":"go"})));
        feed(&reg, &edit("/p/a.rs", "a", "b\nc"));
        feed(&reg, &edit("/p/a.rs", "x", "y"));
        feed(&reg, &edit("/p/b.rs", "q\nr", "q"));
        feed(
            &reg,
            &cl(
                "PostToolUseFailure",
                json!({"tool_name":"Bash","tool_input":{"command":"cargo test"},
                "error":"Exit code 101\nfailures"}),
            ),
        );
        feed(&reg, &cl("Stop", json!({})));
        let ds = drain(&mut rx);
        // nothing of the summary before the stop
        assert!(ds[..ds.len() - 1]
            .iter()
            .all(|d| d.fc.is_none() && d.dur.is_none()));
        let last = ds.last().unwrap();
        assert_eq!(last.st, Some(SessState::Done));
        assert_eq!(
            (last.fc, last.la, last.lr, last.tp, last.dur),
            (Some(2), Some(3), Some(3), Some(false), Some(0))
        );
        // a reconnecting device is told the same by the snapshot
        let snap = &reg.snapshot()[0];
        assert_eq!((snap.fc, snap.tp), (Some(2), Some(false)));
        // the next prompt: the tally starts over and the stored figures go
        feed(&reg, &cl("UserPromptSubmit", json!({"prompt":"again"})));
        assert_eq!(reg.snapshot()[0].fc, None);
        feed(&reg, &cl("Stop", json!({})));
        let ds = drain(&mut rx);
        let last = ds.last().unwrap();
        assert_eq!(
            (last.fc, last.la, last.lr, last.tp, last.dur),
            (None, None, None, None, Some(0)),
            "unknown stays absent, not zero"
        );
    }

    #[test]
    fn a_session_the_daemon_met_mid_turn_gets_no_summary() {
        let reg = Registry::new(Trace::null());
        let mut rx = reg.subscribe().1;
        feed(&reg, &edit("/p/a.rs", "a", "b"));
        feed(&reg, &cl("Stop", json!({})));
        let ds = drain(&mut rx);
        assert!(ds.iter().all(|d| d.fc.is_none() && d.dur.is_none()));
    }

    #[test]
    fn an_errored_turn_and_a_copilot_session() {
        let reg = Registry::new(Trace::null());
        let mut rx = reg.subscribe().1;
        feed(&reg, &cl("UserPromptSubmit", json!({})));
        feed(&reg, &edit("/p/a.rs", "a", "b"));
        feed(&reg, &cl("StopFailure", json!({"error":"rate_limit"})));
        let last = drain(&mut rx).pop().unwrap();
        assert_eq!(
            (last.st, last.fc, last.la, last.lr),
            (Some(SessState::Error), Some(1), Some(1), Some(1))
        );
        // Copilot's payloads are not tallied
        let reg = Registry::new(Trace::null());
        let mut rx = reg.subscribe().1;
        for e in ["userPromptSubmitted", "agentStop"] {
            feed(
                &reg,
                &json!({"agent":"copilot","event":e,"cwd":"/r","payload":{"sessionId":"c"}}),
            );
        }
        assert!(drain(&mut rx).iter().all(|d| d.dur.is_none()));
    }

    #[test]
    fn a_second_stop_with_less_known_replaces_the_summary() {
        let reg = Registry::new(Trace::null());
        let mut rx = reg.subscribe().1;
        feed(&reg, &cl("UserPromptSubmit", json!({})));
        feed(&reg, &edit("/p/a.rs", "a", "b"));
        feed(&reg, &cl("Stop", json!({})));
        let _ = drain(&mut rx);
        // a stop hook sends the agent back to work; this edit's lines cannot be told
        feed(
            &reg,
            &cl(
                "PostToolUse",
                json!({"tool_name":"Write","tool_input":{"file_path":"/p/c","content":"z"},
                "tool_response":{"type":"update"}}),
            ),
        );
        feed(&reg, &cl("Stop", json!({})));
        let last = drain(&mut rx).pop().unwrap();
        assert_eq!((last.fc, last.la, last.lr), (Some(2), None, None));
        assert_eq!(reg.snapshot()[0].la, None);
    }
}
