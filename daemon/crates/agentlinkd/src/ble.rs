//! BLE GATT transport, btleplug central (PROTOCOL §1: rx/tx characteristics, 1-byte fragment header).

mod frag;

use crate::transport::{Link, Transport};
use anyhow::Context;
use async_trait::async_trait;
use btleplug::api::{Central, Manager as _, Peripheral as _, ScanFilter, WriteType};
use btleplug::platform::{Adapter, Manager, Peripheral};
use futures_util::StreamExt;
use std::collections::{HashMap, HashSet};
use std::future::Future;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::sync::mpsc;
use uuid::Uuid;

/// Service / characteristics. Must match `link_ble.cpp` on the firmware.
const SERVICE_UUID: Uuid = Uuid::from_u128(0xa9e1f000_7c3b_4d52_9e18_b06a35c4d2f1);
const RX_UUID: Uuid = Uuid::from_u128(0xa9e1f001_7c3b_4d52_9e18_b06a35c4d2f1); // daemon → device
const TX_UUID: Uuid = Uuid::from_u128(0xa9e1f002_7c3b_4d52_9e18_b06a35c4d2f1); // device → daemon

/// Bytes per write-without-response (fragment header included) follow the negotiated ATT MTU:
/// MTU − 3 (Write Command opcode + handle). 20 fits the minimum MTU of 23 and is the fallback.
const ATT_WRITE_OVERHEAD: usize = 3;
const DEFAULT_WRITE_LEN: usize = 20;
/// The firmware asks for MTU 247 (`BLE_PREFERRED_MTU`) and the link uses the smaller of both
/// sides' MTUs, so 244 is the most it accepts. Also caps `AGENTLINK_BLE_WRITE_LEN`.
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
    /// `AGENTLINK_BLE_WRITE_LEN`; `None` follows the MTU.
    write_len: Option<usize>,
}

impl BleTransport {
    pub fn new() -> Self {
        let write_len = parse_write_len(std::env::var("AGENTLINK_BLE_WRITE_LEN").ok().as_deref());
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
    write_len: Option<usize>,
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
        let mut frags = 0usize;
        let mut liveness = tokio::time::interval(LIVENESS_POLL);
        loop {
            tokio::select! {
                n = notifications.next() => match n {
                    Some(n) if n.uuid == TX_UUID => {
                        frags += 1;
                        if let Some(msg) = asm.push(&n.value) {
                            tracing::debug!("ble rx {} B in {frags} notifications", msg.len());
                            frags = 0;
                            if tx.send(msg).is_err() {
                                break;
                            }
                        }
                    }
                    Some(_) => {}
                    None => break,
                },
                _ = liveness.tick() => {
                    // Some backends never end the notification stream on disconnect, and on macOS
                    // `is_connected` can hang for good once CoreBluetooth drops the peripheral.
                    let alive = tokio::time::timeout(LIVENESS_POLL, reader_p.is_connected()).await;
                    if !matches!(alive, Ok(Ok(true))) {
                        tracing::info!("ble link to {} lost", reader_p.id());
                        break;
                    }
                }
            }
        }
    });

    let mtu = p.mtu();
    let chunk = chunk_len(write_len, false, mtu);
    let how = if write_len.is_some() {
        "AGENTLINK_BLE_WRITE_LEN"
    } else {
        "mtu"
    };
    tracing::info!("ble linked to {name} [{addr}], mtu {mtu}, write chunk {chunk} B ({how})");
    Ok(BleLink {
        p,
        rx_char,
        rx,
        name,
        addr,
        write_len,
        degraded: false,
        last_chunk: chunk,
        _guard: guard,
    })
}

pub struct BleLink {
    p: Peripheral,
    rx_char: btleplug::api::Characteristic,
    rx: mpsc::UnboundedReceiver<Vec<u8>>,
    name: String,
    addr: String,
    write_len: Option<usize>,
    /// A large write failed: stay at `DEFAULT_WRITE_LEN` for the rest of this link.
    degraded: bool,
    last_chunk: usize,
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
        // Read per frame: Windows reports the MTU exchange after `connect` returns.
        let chunk = chunk_len(self.write_len, self.degraded, self.p.mtu());
        if chunk != self.last_chunk {
            tracing::info!("ble write chunk {} -> {chunk} B", self.last_chunk);
            self.last_chunk = chunk;
        }
        let (p, c) = (&self.p, &self.rx_char);
        let sent = send_frame(&frame, chunk, self.write_len.is_none(), |f| async move {
            p.write(c, &f, WriteType::WithoutResponse)
                .await
                .context("ble write")
        })
        .await?;
        if sent.chunk != chunk {
            self.degraded = true;
            self.last_chunk = sent.chunk;
        }
        tracing::debug!(
            "ble tx {} B in {} writes of <= {} B",
            frame.len(),
            sent.writes,
            sent.chunk
        );
        Ok(())
    }

    async fn recv(&mut self) -> anyhow::Result<Option<Vec<u8>>> {
        Ok(self.rx.recv().await)
    }
}

/// `AGENTLINK_BLE_WRITE_LEN`: a byte count pins the write size; unset, empty or `auto` follows
/// the MTU.
fn parse_write_len(v: Option<&str>) -> Option<usize> {
    v.and_then(|v| v.trim().parse::<usize>().ok())
        .map(|n| n.clamp(2, MAX_WRITE_LEN))
}

/// Write size for one frame. btleplug reports the negotiated ATT MTU (CoreBluetooth: the
/// write-without-response limit + 3; BlueZ: the characteristic's `MTU`; WinRT: `MaxPduSize`) and
/// 23 until it knows better, so the result is never more than the link accepts.
fn chunk_len(fixed: Option<usize>, degraded: bool, mtu: u16) -> usize {
    match fixed {
        Some(n) => n,
        None if degraded => DEFAULT_WRITE_LEN,
        None => usize::from(mtu)
            .saturating_sub(ATT_WRITE_OVERHEAD)
            .clamp(DEFAULT_WRITE_LEN, MAX_WRITE_LEN),
    }
}

#[derive(Debug, PartialEq)]
struct Sent {
    writes: usize,
    chunk: usize,
}

/// Fragments and writes `frame`. If a write larger than `DEFAULT_WRITE_LEN` fails and `fallback` is
/// allowed, the whole frame is resent at `DEFAULT_WRITE_LEN`; its FIRST fragment makes the device drop
/// any part already delivered (PROTOCOL §1).
async fn send_frame<W, F>(
    frame: &[u8],
    chunk: usize,
    fallback: bool,
    mut write: W,
) -> anyhow::Result<Sent>
where
    W: FnMut(Vec<u8>) -> F,
    F: Future<Output = anyhow::Result<()>>,
{
    let mut chunk = chunk;
    loop {
        let frags = frag::fragment(frame, chunk)
            .with_context(|| format!("frame of {} B cannot be sent over BLE", frame.len()))?;
        let writes = frags.len();
        let mut failed = None;
        for f in frags {
            if let Err(e) = write(f).await {
                failed = Some(e);
                break;
            }
        }
        match failed {
            None => return Ok(Sent { writes, chunk }),
            Some(e) if fallback && chunk > DEFAULT_WRITE_LEN => {
                tracing::warn!(
                    "ble write of {chunk} B failed ({e:#}); falling back to {DEFAULT_WRITE_LEN} B"
                );
                chunk = DEFAULT_WRITE_LEN;
            }
            Some(e) => return Err(e),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;

    #[test]
    fn chunk_follows_mtu_within_bounds() {
        assert_eq!(chunk_len(None, false, 23), 20);
        assert_eq!(chunk_len(None, false, 0), 20);
        assert_eq!(chunk_len(None, false, 185), 182);
        assert_eq!(chunk_len(None, false, 247), 244);
        assert_eq!(chunk_len(None, false, 517), MAX_WRITE_LEN);
        assert_eq!(chunk_len(None, true, 247), DEFAULT_WRITE_LEN);
        assert_eq!(chunk_len(Some(100), true, 23), 100);
    }

    #[test]
    fn write_len_override() {
        assert_eq!(parse_write_len(None), None);
        assert_eq!(parse_write_len(Some("")), None);
        assert_eq!(parse_write_len(Some("auto")), None);
        assert_eq!(parse_write_len(Some("20")), Some(20));
        assert_eq!(parse_write_len(Some("1")), Some(2));
        assert_eq!(parse_write_len(Some("999")), Some(MAX_WRITE_LEN));
    }

    #[tokio::test]
    async fn large_writes_cut_fragment_count() {
        let frame: Vec<u8> = (0..1500).map(|i| i as u8).collect();
        let mut n = 0;
        let sent = send_frame(&frame, 244, true, |_| {
            n += 1;
            async { Ok(()) }
        })
        .await
        .unwrap();
        assert_eq!(
            sent,
            Sent {
                writes: 7,
                chunk: 244
            }
        );
        assert_eq!(n, 7);
        let at20 = send_frame(&frame, 20, true, |_| async { Ok(()) })
            .await
            .unwrap();
        assert_eq!(at20.writes, 79);
    }

    #[tokio::test]
    async fn failed_large_write_resends_whole_frame_at_min() {
        let frame: Vec<u8> = (0..600).map(|i| (i * 7) as u8).collect();
        // Reassembles as the firmware does; takes two large fragments, then rejects them.
        let asm = RefCell::new(frag::Reassembler::new());
        let got = RefCell::new(Vec::new());
        let accepted = RefCell::new(0usize);
        let sent = send_frame(&frame, 244, true, |f| {
            let ok = f.len() <= DEFAULT_WRITE_LEN || *accepted.borrow() < 2;
            if ok {
                *accepted.borrow_mut() += 1;
                if let Some(m) = asm.borrow_mut().push(&f) {
                    got.borrow_mut().push(m);
                }
            }
            async move {
                if ok {
                    Ok(())
                } else {
                    Err(anyhow::anyhow!("too long"))
                }
            }
        })
        .await
        .unwrap();
        assert_eq!(
            sent,
            Sent {
                writes: 32,
                chunk: DEFAULT_WRITE_LEN
            }
        );
        assert_eq!(*got.borrow(), vec![frame]);
    }

    #[tokio::test]
    async fn no_fallback_when_pinned_or_already_minimal() {
        let frame = vec![1u8; 100];
        let fail = |_| async { Err(anyhow::anyhow!("gone")) };
        assert!(send_frame(&frame, 244, false, fail).await.is_err());
        let mut calls = 0;
        let r = send_frame(&frame, 20, true, |_| {
            calls += 1;
            async { Err::<(), _>(anyhow::anyhow!("gone")) }
        })
        .await;
        assert!(r.is_err());
        assert_eq!(calls, 1);
    }
}
