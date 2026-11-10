//! agentlinkd: the agentlink PC daemon.
//!
//! sources (hooks, …) → registry → device links (WebSocket now; BLE/USB later),
//! with everything traced to `~/.agentlink/trace/`.

mod ask;
mod ble;
mod broker;
mod deltas;
mod device;
mod focus;
mod hook_source;
mod ipc;
mod keys;
mod paths;
mod prompt;
mod registry;
mod remote;
mod scroll;
mod source;
mod trace;
mod transport;
mod typing;
mod usb;
mod voice;
mod ws;

use anyhow::Result;
use device::Shared;
use serde_json::json;
use source::AgentSource;
use std::sync::Arc;
use transport::Transport;

const DEFAULT_WS_ADDR: &str = "127.0.0.1:7878";

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()),
        )
        .with_writer(std::io::stderr)
        .init();

    let trace = trace::Trace::new(paths::trace_dir(), paths::audit_log());
    let registry = Arc::new(registry::Registry::new(trace.clone()));
    let broker = Arc::new(broker::Broker::new(registry.clone()));
    let shared = Arc::new(Shared::new(
        registry.clone(),
        broker.clone(),
        trace.clone(),
        focus::default_driver(),
        focus::default_scroll_driver(),
    ));
    if let Some(stt) = &shared.stt {
        stt.warm();
    }
    let started = registry::now_unix();

    let status: hook_source::StatusFn = {
        let shared = shared.clone();
        Arc::new(move || {
            json!({
                "ok": true,
                "version": env!("CARGO_PKG_VERSION"),
                "pid": std::process::id(),
                "uptime_s": registry::now_unix().saturating_sub(started),
                "host": shared.host,
                "devices": shared.devices.lock().unwrap().values().collect::<Vec<_>>(),
                "sessions": shared.registry.snapshot(),
                "remotes": shared.registry.remotes(),
                "attaches": shared.registry.attaches(),
            })
        })
    };

    let sources: Vec<Box<dyn AgentSource>> = vec![Box::new(hook_source::HookSource {
        socket: paths::hook_socket(),
        remote_socket: paths::remote_socket(),
        token_file: paths::remote_token(),
        home: paths::home(),
        trace: trace.clone(),
        status,
        broker,
    })];
    for s in sources {
        let reg = registry.clone();
        tokio::spawn(async move {
            let name = s.name();
            if let Err(e) = s.run(reg).await {
                tracing::error!("source {name} stopped: {e:#}");
            }
        });
    }

    let addr = std::env::var("AGENTLINK_WS_ADDR").unwrap_or_else(|_| DEFAULT_WS_ADDR.into());
    let ws = ws::WsTransport::bind(&addr).await?;
    tracing::info!("websocket transport listening on ws://{}", ws.local_addr()?);
    tokio::spawn(accept_loop(Box::new(ws), shared.clone()));
    if env_enabled("AGENTLINK_USB") {
        tokio::spawn(accept_loop(
            Box::new(usb::UsbTransport::new()),
            shared.clone(),
        ));
    }
    if env_enabled("AGENTLINK_BLE") {
        tokio::spawn(accept_loop(
            Box::new(ble::BleTransport::new()),
            shared.clone(),
        ));
    }

    tokio::signal::ctrl_c().await?;
    tracing::info!("shutting down");
    let _ = std::fs::remove_file(paths::hook_socket());
    let _ = std::fs::remove_file(paths::remote_socket());
    for socket in registry.attach_sockets() {
        let _ = std::fs::remove_file(socket);
    }
    Ok(())
}

/// Transports are on unless the env var is set to `0`/`false`/`off`.
fn env_enabled(var: &str) -> bool {
    !matches!(std::env::var(var).as_deref(), Ok("0" | "false" | "off"))
}

async fn accept_loop(mut transport: Box<dyn Transport>, shared: Arc<Shared>) {
    let name = transport.name();
    loop {
        match transport.accept().await {
            Ok(link) => {
                let shared = shared.clone();
                tokio::spawn(async move {
                    if let Err(e) = device::run_link(shared, link, name).await {
                        tracing::warn!("{name} link ended: {e:#}");
                    }
                });
            }
            Err(e) => {
                tracing::error!("{name} accept failed: {e:#}");
                return;
            }
        }
    }
}
