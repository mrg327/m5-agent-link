//! `AgentSource` fed by `agentlink-hook` over the IPC socket. Also answers control
//! requests (`{"ctl":"status"}`) from the CLI on the same socket.
//!
//! Hook line: `{"agent","event","ts","cwd","env":{..},"ppid","payload":{..}}`.

use crate::ipc::{self, IpcStream};
use crate::registry::{HostCtx, SessionEvent, SessionPatch};
use crate::source::AgentSource;
use crate::trace::{Dir, Trace};
use agentlink_proto::{Agent, InputSub, SessState};
use async_trait::async_trait;
use serde_json::Value;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::sync::mpsc;

pub type StatusFn = Arc<dyn Fn() -> Value + Send + Sync>;

pub struct HookSource {
    pub socket: PathBuf,
    pub trace: Trace,
    pub status: StatusFn,
}

#[async_trait]
impl AgentSource for HookSource {
    fn name(&self) -> &'static str {
        "hooks"
    }

    async fn run(self: Box<Self>, tx: mpsc::Sender<SessionEvent>) -> anyhow::Result<()> {
        let listener = ipc::bind(&self.socket)?;
        tracing::info!("hook socket listening on {}", self.socket.display());
        loop {
            let stream = listener.accept().await?;
            tokio::spawn(handle_conn(
                stream,
                tx.clone(),
                self.trace.clone(),
                self.status.clone(),
            ));
        }
    }
}

async fn handle_conn(
    stream: IpcStream,
    tx: mpsc::Sender<SessionEvent>,
    trace: Trace,
    status: StatusFn,
) {
    let (r, mut w) = tokio::io::split(stream);
    let mut lines = BufReader::new(r).lines();
    while let Ok(Some(line)) = lines.next_line().await {
        let Ok(v) = serde_json::from_str::<Value>(&line) else {
            trace.record(
                "hook",
                Dir::In,
                "invalid_json",
                None,
                None,
                Value::String(line),
                None,
            );
            continue;
        };
        if v.get("ctl").and_then(Value::as_str) == Some("status") {
            let reply = format!("{}\n", status());
            let _ = w.write_all(reply.as_bytes()).await;
            continue;
        }
        let event = v["event"].as_str().unwrap_or("?").to_owned();
        trace.record("hook", Dir::In, &event, None, None, v.clone(), None);
        if let Some(ev) = to_session_event(&v) {
            let _ = tx.send(ev).await;
        }
    }
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
pub fn to_session_event(v: &Value) -> Option<SessionEvent> {
    let agent_name = v["agent"].as_str()?;
    let event = v["event"].as_str()?.to_ascii_lowercase();
    let payload = &v["payload"];
    let cwd = v["cwd"]
        .as_str()
        .or_else(|| payload["cwd"].as_str())
        .map(str::to_owned);
    let sid = payload["sessionId"]
        .as_str()
        .or_else(|| payload["session_id"].as_str())
        .map(str::to_owned)
        .unwrap_or_else(|| {
            format!(
                "{}#{}",
                cwd.as_deref().unwrap_or("?"),
                v["ppid"].as_u64().unwrap_or(0)
            )
        });
    let key = format!("{agent_name}:{sid}");
    if event == "sessionend" {
        return Some(SessionEvent::End { key });
    }

    let mut p = SessionPatch::new(agent_of(agent_name));
    let tool = payload["toolName"]
        .as_str()
        .or_else(|| payload["tool_name"].as_str());
    match event.as_str() {
        "sessionstart" => p.state = Some(SessState::Start),
        "userpromptsubmitted" | "userpromptsubmit" | "posttooluse" | "posttoolusefailure" => {
            p.state = Some(SessState::Work)
        }
        "pretooluse" => {
            p.state = Some(SessState::Work);
            p.tool = tool.map(str::to_owned);
        }
        "agentstop" | "stop" => p.state = Some(SessState::Done),
        "erroroccurred" | "stopfailure" => p.state = Some(SessState::Error),
        "permissionrequest" => {
            p.state = Some(SessState::Input);
            p.sub = Some(InputSub::Approval);
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
        p.repo = std::path::Path::new(c)
            .file_name()
            .map(|n| n.to_string_lossy().into_owned());
        p.branch = branch_of(c);
    }
    let env = v["env"]
        .as_object()
        .map(|m| {
            m.iter()
                .filter_map(|(k, v)| Some((k.clone(), v.as_str()?.to_owned())))
                .collect()
        })
        .unwrap_or_default();
    Some(SessionEvent::Update {
        key,
        patch: p,
        ctx: HostCtx { cwd, env },
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn copilot_permission_notification_is_input_approval() {
        let v = json!({"agent":"copilot","event":"notification","cwd":"/nonexistent/proj",
                       "payload":{"sessionId":"s1","notification_type":"permission_prompt"}});
        let Some(SessionEvent::Update { key, patch, .. }) = to_session_event(&v) else {
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
    fn claude_session_end_removes_and_unknown_is_ignored() {
        let end = json!({"agent":"claude","event":"SessionEnd","payload":{"session_id":"c1"}});
        assert!(
            matches!(to_session_event(&end), Some(SessionEvent::End { key }) if key == "claude:c1")
        );
        assert!(to_session_event(&json!({"agent":"claude","event":"Whatever"})).is_none());
    }
}
