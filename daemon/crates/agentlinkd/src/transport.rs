//! Device transports. One `Link` carries whole envelopes (one frame = one envelope);
//! fragmentation/COBS framing belongs to the concrete transport.

use async_trait::async_trait;

#[async_trait]
pub trait Transport: Send {
    fn name(&self) -> &'static str;

    /// Waits for the next device connection.
    async fn accept(&mut self) -> anyhow::Result<Box<dyn Link>>;
}

#[async_trait]
pub trait Link: Send {
    /// Human-readable peer description for logs.
    fn peer(&self) -> String;

    async fn send(&mut self, frame: Vec<u8>) -> anyhow::Result<()>;

    /// Next frame, or `None` when the peer closed. Must be cancel-safe (used in `select!`).
    async fn recv(&mut self) -> anyhow::Result<Option<Vec<u8>>>;
}
