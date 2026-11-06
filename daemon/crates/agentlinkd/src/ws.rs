//! WebSocket transport for the simulator: binary frames, one envelope each.
//! Bound to loopback only (PROTOCOL §8.4).

use crate::transport::{Link, Transport};
use anyhow::{Context, Result};
use async_trait::async_trait;
use futures_util::{SinkExt, StreamExt};
use std::time::Duration;
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::tungstenite::Message;
use tokio_tungstenite::WebSocketStream;

pub struct WsTransport {
    listener: TcpListener,
}

impl WsTransport {
    pub async fn bind(addr: &str) -> Result<Self> {
        let listener = TcpListener::bind(addr)
            .await
            .with_context(|| format!("binding {addr}"))?;
        Ok(WsTransport { listener })
    }

    pub fn local_addr(&self) -> Result<std::net::SocketAddr> {
        Ok(self.listener.local_addr()?)
    }
}

#[async_trait]
impl Transport for WsTransport {
    fn name(&self) -> &'static str {
        "ws"
    }

    async fn accept(&mut self) -> Result<Box<dyn Link>> {
        loop {
            let (tcp, peer) = self.listener.accept().await?;
            match tokio::time::timeout(Duration::from_secs(5), tokio_tungstenite::accept_async(tcp))
                .await
            {
                Ok(Ok(ws)) => {
                    return Ok(Box::new(WsLink {
                        ws,
                        peer: peer.to_string(),
                    }))
                }
                Ok(Err(e)) => tracing::warn!("ws handshake from {peer} failed: {e}"),
                Err(_) => tracing::warn!("ws handshake from {peer} timed out"),
            }
        }
    }
}

struct WsLink {
    ws: WebSocketStream<TcpStream>,
    peer: String,
}

#[async_trait]
impl Link for WsLink {
    fn peer(&self) -> String {
        format!("ws://{}", self.peer)
    }

    async fn send(&mut self, frame: Vec<u8>) -> Result<()> {
        self.ws.send(Message::Binary(frame.into())).await?;
        Ok(())
    }

    async fn recv(&mut self) -> Result<Option<Vec<u8>>> {
        loop {
            match self.ws.next().await {
                None | Some(Ok(Message::Close(_))) => return Ok(None),
                Some(Ok(Message::Binary(b))) => return Ok(Some(b.to_vec())),
                Some(Ok(_)) => continue, // ping/pong are answered by tungstenite; text is not protocol
                Some(Err(e)) => return Err(e.into()),
            }
        }
    }
}
