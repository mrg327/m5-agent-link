//! Bringing the agent's terminal to the front (DESIGN §10). One driver per OS/terminal
//! will implement this; for now a logging no-op.

use crate::registry::HostCtx;
use agentlink_proto::Agent;
use async_trait::async_trait;

pub struct FocusTarget {
    pub sid: u64,
    pub agent: Option<Agent>,
    pub ctx: HostCtx,
}

#[derive(Debug)]
#[allow(dead_code)] // returned by real drivers
pub enum FocusError {
    NoWindow,
    Unsupported,
    Internal(String),
}

#[async_trait]
pub trait FocusDriver: Send + Sync {
    async fn focus(&self, target: &FocusTarget) -> Result<(), FocusError>;
}

/// Logs the request and succeeds without touching any window.
pub struct LoggingFocus;

#[async_trait]
impl FocusDriver for LoggingFocus {
    async fn focus(&self, t: &FocusTarget) -> Result<(), FocusError> {
        tracing::info!(
            "focus (no-op) sid={} agent={:?} cwd={:?} term={:?}",
            t.sid,
            t.agent,
            t.ctx.cwd,
            t.ctx.env.get("TERM_PROGRAM")
        );
        Ok(())
    }
}
