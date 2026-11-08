//! Where session events come from. Variation is real here (hooks, transcript tailing,
//! cloud polling), so this is a trait.

use crate::registry::Registry;
use async_trait::async_trait;
use std::sync::Arc;

#[async_trait]
pub trait AgentSource: Send {
    fn name(&self) -> &'static str;

    /// Runs until the source fails; events are applied to the registry as they arrive.
    async fn run(self: Box<Self>, registry: Arc<Registry>) -> anyhow::Result<()>;
}
