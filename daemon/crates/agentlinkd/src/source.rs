//! Where session events come from. Variation is real here (hooks, transcript tailing,
//! cloud polling), so this is a trait.

use crate::registry::SessionEvent;
use async_trait::async_trait;
use tokio::sync::mpsc;

#[async_trait]
pub trait AgentSource: Send {
    fn name(&self) -> &'static str;

    /// Runs until the source fails or the channel closes.
    async fn run(self: Box<Self>, tx: mpsc::Sender<SessionEvent>) -> anyhow::Result<()>;
}
