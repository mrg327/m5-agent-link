//! `AgentSource` fed by `agentlink-hook` over the IPC socket. Also answers control
//! requests (`{"ctl":"status"}`) from the CLI on the same socket.
//!
//! Hook line: `{"agent","event","ts","cwd","env":{..},"ppid","tty","pids":[..],"payload":{..}}`.
//! `cwd` is the hook process's; the agent's own cwd, when sent, is `payload.cwd`.
//!
//! A line with `"await": {"wait_ms": N}` is a blocking permission or question hook: it is
//! opened on the approval broker and the connection stays up until a device answers or the
//! wait runs out, then one reply line goes back (`{"decision": "allow"|"deny"|"none", ..}`).
//! Any other event from the same session, except a notification, settles what was pending
//! there ("first answer wins": the terminal got there first).
//!
//! Copilot's hooks cannot answer, so its prompts are opened as cards by `prompt`, which
//! types a device answer into the terminal.
//!
//! The same line protocol is spoken on the remote sockets (see `remote`), after an auth line.

use crate::broker::{request_of, Broker, Outcome};
use crate::ipc;
use crate::prompt;
use crate::registry::{clip, HostCtx, Registry, SessionEvent, SessionPatch, Step, Via};
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

/// Longest a hook may ask us to hold its prompt back (DESIGN §6a: a short window, then the
/// terminal prompt appears as usual).
const MAX_WAIT: Duration = Duration::from_secs(120);

pub struct HookSource {
    pub socket: PathBuf,
    pub remote_socket: PathBuf,
    pub token_file: PathBuf,
    /// Directory of the attach sockets.
    pub home: PathBuf,
    pub trace: Trace,
    pub status: StatusFn,
    pub broker: Arc<Broker>,
}

/// What every connection handler needs.
pub struct ConnEnv {
    pub registry: Arc<Registry>,
    pub broker: Arc<Broker>,
    pub trace: Trace,
    pub status: StatusFn,
    pub token_file: PathBuf,
    pub home: PathBuf,
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
        trace.record(src, Dir::In, &event, None, None, v.clone(), None);
        let key = session_key(&v, &via);
        if let Some(ev) = to_session_event(&v, &via) {
            registry.apply(ev);
        }
        let Some(wait) = v["await"]["wait_ms"].as_u64() else {
            // The terminal moved on: whatever this session had pending was answered there.
            // A notification is not a move: it may be the very prompt the card is about.
            if let Some(key) = &key {
                let tool = v["payload"]["tool_name"].as_str();
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
            registry,
            broker,
            trace,
            &v,
            key,
            Duration::from_millis(wait),
        )
        .await;
        let reply = format!("{}\n", serde_json::to_string(&outcome).unwrap_or_default());
        let _ = w.write_all(reply.as_bytes()).await;
    }
    drop(attach_guard);
}

/// Opens the request and waits for the device, the terminal, or the clock. Never returns
/// `allow` without a device decision.
async fn await_decision(
    registry: &Registry,
    broker: &Broker,
    trace: &Trace,
    v: &Value,
    key: Option<String>,
    wait: Duration,
) -> Outcome {
    let wait = wait.min(MAX_WAIT);
    let agent = v["agent"].as_str().unwrap_or("");
    let event = v["event"].as_str().unwrap_or("");
    let Some(key) = key else {
        return Outcome::NONE;
    };
    let Some(sid) = registry.sid_of(&key) else {
        return Outcome::NONE;
    };
    let host = registry.host_of(sid);
    let Some(req) = request_of(sid, key, agent, event, &v["payload"], wait) else {
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
    let (rid, rx) = broker.open(req);
    match tokio::time::timeout(wait, rx).await {
        Ok(Ok(outcome)) => {
            trace.audit(
                "decision",
                Some(sid),
                Some(rid),
                json!({"from": "device", "outcome": outcome, "host": host}),
            );
            outcome
        }
        Ok(Err(_)) => {
            // cancelled (terminal answered, or the request was dropped): no decision
            trace.audit(
                "decision",
                Some(sid),
                Some(rid),
                json!({"from": "none", "why": "cancelled", "host": host}),
            );
            Outcome::NONE
        }
        Err(_) => {
            broker.cancel(rid, ClearWhy::Expired);
            trace.audit(
                "decision",
                Some(sid),
                Some(rid),
                json!({"from": "none", "why": "timeout", "host": host}),
            );
            Outcome::NONE
        }
    }
}

/// `agent:session` key of a hook line (prefixed for remote ones), as `to_session_event` derives it.
pub fn session_key(v: &Value, via: &Via) -> Option<String> {
    let agent_name = v["agent"].as_str()?;
    let payload = &v["payload"];
    let cwd = payload["cwd"]
        .as_str()
        .or_else(|| v["cwd"].as_str())
        .filter(|c| !c.is_empty());
    let sid = payload["sessionId"]
        .as_str()
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

fn branch_of(cwd: &str) -> Option<String> {
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
    if event == "sessionend" {
        return Some(SessionEvent::End { key });
    }

    let mut p = SessionPatch::new(agent_of(agent_name));
    let tool = payload["toolName"]
        .as_str()
        .or_else(|| payload["tool_name"].as_str());
    match event.as_str() {
        "sessionstart" => p.state = Some(SessState::Start),
        "userpromptsubmitted" | "userpromptsubmit" => {
            p.state = Some(SessState::Work);
            p.step = Step::Reset;
            p.ask = Some(Default::default()); /* the question was answered (or moved past) */
        }
        "posttooluse" | "posttoolusefailure" => {
            p.state = Some(SessState::Work);
            p.step = Step::Result(tool_ok(&event, payload));
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
            }
        }
        "erroroccurred" | "stopfailure" => p.state = Some(SessState::Error),
        "permissionrequest" => {
            p.state = Some(SessState::Input);
            p.sub = Some(InputSub::Approval);
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
fn repo_of(cwd: &str) -> Option<String> {
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
}
