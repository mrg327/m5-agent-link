//! BLE GATT transport, btleplug central (PROTOCOL §1: rx/tx characteristics, 1-byte fragment header).

mod frag;

use crate::transport::{Link, Transport};
use anyhow::Context;
use async_trait::async_trait;
use btleplug::api::{Central, Manager as _, Peripheral as _, ScanFilter, WriteType};
use btleplug::platform::{Adapter, Manager, Peripheral};
use futures_util::StreamExt;
use std::collections::{HashMap, HashSet};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::sync::mpsc;
use uuid::Uuid;

/// Service / characteristics. Must match `link_ble.cpp` on the firmware.
const SERVICE_UUID: Uuid = Uuid::from_u128(0xa9e1f000_7c3b_4d52_9e18_b06a35c4d2f1);
const RX_UUID: Uuid = Uuid::from_u128(0xa9e1f001_7c3b_4d52_9e18_b06a35c4d2f1); // daemon → device
const TX_UUID: Uuid = Uuid::from_u128(0xa9e1f002_7c3b_4d52_9e18_b06a35c4d2f1); // device → daemon

/// Bytes per write-without-response (header included). 20 fits the default ATT MTU of 23, since
/// btleplug cannot report the negotiated MTU on every OS. `AGENTLINK_BLE_WRITE_LEN` overrides it.
const DEFAULT_WRITE_LEN: usize = 20;
const MAX_WRITE_LEN: usize = 244;

const SCAN_POLL: Duration = Duration::from_secs(1);
const UNAVAILABLE_RETRY: Duration = Duration::from_secs(15);
const CONNECT_COOLDOWN: Duration = Duration::from_secs(5);
const CONNECT_TIMEOUT: Duration = Duration::from_secs(15);
const LIVENESS_POLL: Duration = Duration::from_secs(2);

type Active = Arc<Mutex<HashSet<String>>>;

pub struct BleTransport {
    adapter: Option<Adapter>,
    active: Active,
    cooldown: HashMap<String, Instant>,
    pin_name: Option<String>,
    write_len: usize,
}

impl BleTransport {
    pub fn new() -> Self {
        let write_len = std::env::var("AGENTLINK_BLE_WRITE_LEN")
            .ok()
            .and_then(|v| v.parse::<usize>().ok())
            .map_or(DEFAULT_WRITE_LEN, |n| n.clamp(2, MAX_WRITE_LEN));
        Self {
            adapter: None,
            active: Arc::default(),
            cooldown: HashMap::new(),
            pin_name: std::env::var("AGENTLINK_BLE_NAME")
                .ok()
                .filter(|s| !s.is_empty()),
            write_len,
        }
    }

    async fn adapter(&mut self) -> anyhow::Result<Adapter> {
        if let Some(a) = &self.adapter {
            return Ok(a.clone());
        }
        let manager = Manager::new().await.context("bluetooth manager")?;
        let adapter = manager
            .adapters()
            .await
            .context("listing bluetooth adapters")?
            .into_iter()
            .next()
            .context("no bluetooth adapter")?;
        adapter
            .start_scan(ScanFilter {
                services: vec![SERVICE_UUID],
            })
            .await
            .context("starting bluetooth scan (permission?)")?;
        self.adapter = Some(adapter.clone());
        Ok(adapter)
    }

    /// Finds a not-yet-linked device advertising our service.
    async fn find_candidate(
        &mut self,
        adapter: &Adapter,
    ) -> anyhow::Result<Option<(Peripheral, String, String)>> {
        for p in adapter.peripherals().await? {
            let key = format!("{:?}", p.id());
            if self.active.lock().unwrap().contains(&key) {
                continue;
            }
            if self
                .cooldown
                .get(&key)
                .is_some_and(|t| t.elapsed() < CONNECT_COOLDOWN)
            {
                continue;
            }
            let Some(props) = p.properties().await? else {
                continue;
            };
            let name = props.local_name.clone().unwrap_or_default();
            let ours = props.services.contains(&SERVICE_UUID) || name.starts_with("AgentLink-");
            if !ours || self.pin_name.as_ref().is_some_and(|pin| *pin != name) {
                continue;
            }
            return Ok(Some((p, name, props.address.to_string())));
        }
        Ok(None)
    }
}

#[async_trait]
impl Transport for BleTransport {
    fn name(&self) -> &'static str {
        "ble"
    }

    /// Never returns `Err`: an absent adapter or denied permission is retried slowly so that
    /// Bluetooth can be switched on or granted later without restarting the daemon.
    async fn accept(&mut self) -> anyhow::Result<Box<dyn Link>> {
        let mut unavailable_logged = false;
        loop {
            let adapter = match self.adapter().await {
                Ok(a) => a,
                Err(e) => {
                    if !unavailable_logged {
                        tracing::warn!(
                            "ble unavailable ({e:#}); retrying every {}s",
                            UNAVAILABLE_RETRY.as_secs()
                        );
                        unavailable_logged = true;
                    }
                    tokio::time::sleep(UNAVAILABLE_RETRY).await;
                    continue;
                }
            };
            unavailable_logged = false;
            match self.find_candidate(&adapter).await {
                Ok(Some((p, name, addr))) => {
                    let key = format!("{:?}", p.id());
                    self.active.lock().unwrap().insert(key.clone());
                    let guard = ActiveGuard {
                        active: self.active.clone(),
                        key: key.clone(),
                    };
                    match tokio::time::timeout(
                        CONNECT_TIMEOUT,
                        open(p.clone(), name, addr, self.write_len, guard),
                    )
                    .await
                    {
                        Ok(Ok(link)) => return Ok(Box::new(link)),
                        Ok(Err(e)) => tracing::warn!("ble connect failed: {e:#}"),
                        Err(_) => {
                            tracing::warn!("ble connect timed out");
                            let _ = p.disconnect().await;
                        }
                    }
                    // `guard` was moved into `open`, so the failed attempt already freed `active`.
                    self.cooldown.insert(key, Instant::now());
                }
                Ok(None) => {}
                Err(e) => tracing::debug!("ble scan poll: {e:#}"),
            }
            tokio::time::sleep(SCAN_POLL).await;
        }
    }
}

/// Frees a device for re-linking once its `Link` (or a failed attempt) is gone.
struct ActiveGuard {
    active: Active,
    key: String,
}

impl Drop for ActiveGuard {
    fn drop(&mut self) {
        self.active.lock().unwrap().remove(&self.key);
    }
}

async fn open(
    p: Peripheral,
    name: String,
    addr: String,
    write_len: usize,
    guard: ActiveGuard,
) -> anyhow::Result<BleLink> {
    let setup = async {
        if !p.is_connected().await.unwrap_or(false) {
            p.connect().await.context("connect")?;
        }
        p.discover_services().await.context("discover services")?;
        let chars = p.characteristics();
        let rx = chars
            .iter()
            .find(|c| c.uuid == RX_UUID)
            .cloned()
            .context("rx characteristic missing")?;
        let tx = chars
            .iter()
            .find(|c| c.uuid == TX_UUID)
            .cloned()
            .context("tx characteristic missing")?;
        let notifications = p.notifications().await.context("notifications")?;
        p.subscribe(&tx).await.context("subscribe tx")?;
        anyhow::Ok((rx, notifications))
    };
    let (rx_char, mut notifications) = match setup.await {
        Ok(v) => v,
        Err(e) => {
            let _ = p.disconnect().await;
            return Err(e);
        }
    };

    // The reader owns reassembly; dropping `tx` (on disconnect) makes `recv` return `None`.
    let (tx, rx) = mpsc::unbounded_channel();
    let reader_p = p.clone();
    tokio::spawn(async move {
        let mut asm = frag::Reassembler::new();
        let mut liveness = tokio::time::interval(LIVENESS_POLL);
        loop {
            tokio::select! {
                n = notifications.next() => match n {
                    Some(n) if n.uuid == TX_UUID => {
                        if let Some(msg) = asm.push(&n.value) {
                            if tx.send(msg).is_err() {
                                break;
                            }
                        }
                    }
                    Some(_) => {}
                    None => break,
                },
                _ = liveness.tick() => {
                    // Some backends never end the notification stream on disconnect.
                    if !reader_p.is_connected().await.unwrap_or(false) {
                        break;
                    }
                }
            }
        }
    });

    tracing::info!("ble linked to {name} [{addr}], write chunk {write_len} B");
    Ok(BleLink {
        p,
        rx_char,
        rx,
        name,
        addr,
        write_len,
        _guard: guard,
    })
}

pub struct BleLink {
    p: Peripheral,
    rx_char: btleplug::api::Characteristic,
    rx: mpsc::UnboundedReceiver<Vec<u8>>,
    name: String,
    addr: String,
    write_len: usize,
    _guard: ActiveGuard,
}

impl Drop for BleLink {
    fn drop(&mut self) {
        let p = self.p.clone();
        tokio::spawn(async move {
            let _ = p.disconnect().await;
        });
    }
}

#[async_trait]
impl Link for BleLink {
    fn peer(&self) -> String {
        format!("{} [{}]", self.name, self.addr)
    }

    async fn send(&mut self, frame: Vec<u8>) -> anyhow::Result<()> {
        let frags = frag::fragment(&frame, self.write_len)
            .with_context(|| format!("frame of {} B cannot be sent over BLE", frame.len()))?;
        for f in frags {
            self.p
                .write(&self.rx_char, &f, WriteType::WithoutResponse)
                .await
                .context("ble write")?;
        }
        Ok(())
    }

    async fn recv(&mut self) -> anyhow::Result<Option<Vec<u8>>> {
        Ok(self.rx.recv().await)
    }
}
