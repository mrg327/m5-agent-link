//! USB CDC transport (PROTOCOL §1: `0x00 ‖ COBS(payload ‖ crc16)` ‖ `0x00`) over the ESP32-S3
//! USB Serial/JTAG port. The same stream carries device log text, which is forwarded to the
//! `device_log` tracing target.

mod frame;

use crate::transport::{Link, Transport};
use agentlink_proto::{Empty, Envelope, Message};
use anyhow::{anyhow, Result};
use async_trait::async_trait;
use frame::{encode_frame, Decoder, Event};
use std::collections::{HashMap, VecDeque};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Weak};
use std::time::Duration;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};
use tokio::sync::mpsc;
use tokio::task::JoinHandle;
use tokio_serial::{SerialPortBuilderExt, SerialPortType};

const ESP_VID: u16 = 0x303A;
const ESP_PID: u16 = 0x1001;
const BAUD: u32 = 115_200;
const POLL: Duration = Duration::from_secs(2);
/// The device drops the link after ~12 s of silence, so keep this well below that.
const PING_EVERY: Duration = Duration::from_secs(5);
/// Ping ids live far above the ids the daemon allocates for its own messages (the device only takes 32-bit ids).
const PING_ID_BASE: u64 = 1 << 30;

/// Everything that belongs to one open port; dropping it stops the I/O tasks and closes the port.
struct Session {
    rx: mpsc::Receiver<Vec<u8>>,
    tx: mpsc::Sender<Vec<u8>>,
    greeted: Arc<AtomicBool>,
    tasks: [JoinHandle<()>; 2],
    _alive: Arc<()>,
}

impl Drop for Session {
    fn drop(&mut self) {
        for t in &self.tasks {
            t.abort();
        }
    }
}

/// A session handed from a finished link to the next one, with the frame that triggered it.
struct Handoff {
    session: Session,
    first: Vec<u8>,
}

fn log_text(line: &str) {
    if line.starts_with('#') {
        tracing::info!(target: "device_log", "{line}");
    } else {
        tracing::debug!(target: "device_log", "{line}");
    }
}

fn spawn_session<S>(stream: S, name: String, alive: Arc<()>, ping_every: Duration) -> Session
where
    S: AsyncRead + AsyncWrite + Send + 'static,
{
    let (mut rd, mut wr) = tokio::io::split(stream);
    let (in_tx, rx) = mpsc::channel::<Vec<u8>>(64);
    let (tx, mut out_rx) = mpsc::channel::<Vec<u8>>(64);
    let greeted = Arc::new(AtomicBool::new(false));

    let reader = tokio::spawn(async move {
        let mut dec = Decoder::default();
        let mut buf = [0u8; 1024];
        loop {
            match rd.read(&mut buf).await {
                Ok(0) => break,
                Ok(n) => {
                    for ev in dec.feed(&buf[..n]) {
                        match ev {
                            Event::Text(l) => log_text(&l),
                            Event::Frame(f) => {
                                if in_tx.send(f).await.is_err() {
                                    return;
                                }
                            }
                        }
                    }
                }
                Err(e) => {
                    tracing::warn!("{name}: read failed: {e}");
                    break;
                }
            }
        }
        // Dropping `in_tx` ends the link.
    });

    let writer = tokio::spawn({
        let greeted = greeted.clone();
        async move {
            let mut tick = tokio::time::interval(ping_every);
            tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
            let mut ping_id = PING_ID_BASE;
            loop {
                let bytes = tokio::select! {
                    f = out_rx.recv() => match f {
                        Some(f) => encode_frame(&f),
                        None => return,
                    },
                    _ = tick.tick() => {
                        if !greeted.load(Ordering::Relaxed) {
                            continue;
                        }
                        ping_id += 1;
                        let env = Envelope { id: ping_id, re: None, msg: Message::Ping(Empty {}) };
                        match env.to_bytes() {
                            Ok(b) => encode_frame(&b),
                            Err(_) => continue,
                        }
                    }
                };
                if wr.write_all(&bytes).await.is_err() || wr.flush().await.is_err() {
                    return;
                }
            }
        }
    });

    Session {
        rx,
        tx,
        greeted,
        tasks: [reader, writer],
        _alive: alive,
    }
}

pub struct UsbTransport {
    live: HashMap<String, Weak<()>>,
    handoff_tx: mpsc::UnboundedSender<Handoff>,
    handoff_rx: mpsc::UnboundedReceiver<Handoff>,
    last_err: Option<String>,
}

impl UsbTransport {
    pub fn new() -> Self {
        let (handoff_tx, handoff_rx) = mpsc::unbounded_channel();
        Self {
            live: HashMap::new(),
            handoff_tx,
            handoff_rx,
            last_err: None,
        }
    }

    fn candidates(&self) -> Vec<String> {
        if let Ok(p) = std::env::var("AGENTLINK_USB_PORT") {
            if !p.is_empty() {
                return vec![p];
            }
        }
        let Ok(ports) = tokio_serial::available_ports() else {
            return Vec::new();
        };
        let mut out: Vec<String> = ports
            .into_iter()
            .filter(|p| {
                matches!(&p.port_type, SerialPortType::UsbPort(u) if u.vid == ESP_VID && u.pid == ESP_PID)
            })
            .map(|p| callout_path(&p.port_name))
            .collect();
        out.sort();
        out.dedup();
        out
    }

    fn is_live(&mut self, path: &str) -> bool {
        match self.live.get(path) {
            Some(w) if w.strong_count() > 0 => true,
            Some(_) => {
                self.live.remove(path);
                false
            }
            None => false,
        }
    }

    // Opening the port must not touch DTR/RTS: on the ESP32-S3 USB Serial/JTAG, driving them
    // (e.g. both low) resets the chip, while the plain open (both asserted by the OS) does not.
    fn try_open(&mut self, path: &str) -> Result<Session> {
        let port = tokio_serial::new(path, BAUD).open_native_async()?;
        let alive = Arc::new(());
        self.live.insert(path.to_owned(), Arc::downgrade(&alive));
        Ok(spawn_session(port, path.to_owned(), alive, PING_EVERY))
    }

    fn link(&self, path: String, session: Session, first: Option<Vec<u8>>) -> Box<dyn Link> {
        Box::new(UsbLink {
            path,
            session: Some(session),
            pending: first.into_iter().collect(),
            hello_seen: false,
            handoff: self.handoff_tx.clone(),
        })
    }
}

impl Default for UsbTransport {
    fn default() -> Self {
        Self::new()
    }
}

/// macOS lists both `tty.*` and `cu.*`; only the call-out device opens without waiting for carrier.
fn callout_path(p: &str) -> String {
    match p.strip_prefix("/dev/tty.") {
        Some(rest) if cfg!(target_os = "macos") => format!("/dev/cu.{rest}"),
        _ => p.to_owned(),
    }
}

#[async_trait]
impl Transport for UsbTransport {
    fn name(&self) -> &'static str {
        "usb"
    }

    async fn accept(&mut self) -> Result<Box<dyn Link>> {
        loop {
            if let Ok(h) = self.handoff_rx.try_recv() {
                return Ok(self.link("usb".into(), h.session, Some(h.first)));
            }
            for path in self.candidates() {
                if self.is_live(&path) {
                    continue;
                }
                match self.try_open(&path) {
                    Ok(session) => {
                        self.last_err = None;
                        tracing::info!("usb: opened {path}");
                        return Ok(self.link(path, session, None));
                    }
                    Err(e) => {
                        let msg = format!("{path}: {e}");
                        if self.last_err.as_deref() != Some(&msg) {
                            tracing::warn!("usb: cannot open {msg}");
                            self.last_err = Some(msg);
                        }
                    }
                }
            }
            tokio::select! {
                h = self.handoff_rx.recv() => {
                    if let Some(h) = h {
                        return Ok(self.link("usb".into(), h.session, Some(h.first)));
                    }
                }
                _ = tokio::time::sleep(POLL) => {}
            }
        }
    }
}

struct UsbLink {
    path: String,
    session: Option<Session>,
    pending: VecDeque<Vec<u8>>,
    hello_seen: bool,
    handoff: mpsc::UnboundedSender<Handoff>,
}

fn is_hello(frame: &[u8]) -> bool {
    matches!(
        Envelope::from_bytes(frame),
        Ok(Envelope {
            msg: Message::Hello(_),
            ..
        })
    )
}

#[async_trait]
impl Link for UsbLink {
    fn peer(&self) -> String {
        self.path.clone()
    }

    async fn send(&mut self, frame: Vec<u8>) -> Result<()> {
        let s = self
            .session
            .as_ref()
            .ok_or_else(|| anyhow!("usb link closed"))?;
        if frame.len() > frame::MAX_PAYLOAD {
            return Err(anyhow!("frame too large for usb ({} bytes)", frame.len()));
        }
        s.tx.send(frame)
            .await
            .map_err(|_| anyhow!("usb port closed"))
    }

    async fn recv(&mut self) -> Result<Option<Vec<u8>>> {
        let Some(s) = self.session.as_mut() else {
            return Ok(None);
        };
        let f = match self.pending.pop_front() {
            Some(f) => f,
            None => match s.rx.recv().await {
                Some(f) => f,
                None => return Ok(None),
            },
        };
        if is_hello(&f) {
            if self.hello_seen {
                // The device restarted its handshake (reboot, or it timed us out). The
                // handshake loop only understands a first hello, so continue on a fresh link
                // over the same open port rather than reopening it (which could reset the chip).
                if let Some(session) = self.session.take() {
                    let _ = self.handoff.send(Handoff { session, first: f });
                }
                return Ok(None);
            }
            self.hello_seen = true;
            s.greeted.store(true, Ordering::Relaxed);
        }
        Ok(Some(f))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::io::duplex;

    fn hello_bytes() -> Vec<u8> {
        let p = format!(
            "{}/../../../protocol/vectors/hello_sim.json",
            env!("CARGO_MANIFEST_DIR")
        );
        let v: serde_json::Value = serde_json::from_slice(&std::fs::read(p).unwrap()).unwrap();
        hex::decode(v["cbor_hex"].as_str().unwrap()).unwrap()
    }

    fn link_over(s: Session) -> (UsbLink, mpsc::UnboundedReceiver<Handoff>) {
        let (handoff, h_rx) = mpsc::unbounded_channel();
        let link = UsbLink {
            path: "t".into(),
            session: Some(s),
            pending: VecDeque::new(),
            hello_seen: false,
            handoff,
        };
        (link, h_rx)
    }

    #[tokio::test]
    async fn frames_round_trip_around_log_text() {
        let (daemon_side, mut device) = duplex(8192);
        let (mut link, _h) = link_over(spawn_session(
            daemon_side,
            "t".into(),
            Arc::new(()),
            PING_EVERY,
        ));
        let hello = hello_bytes();
        let mut wire = b"I (1) boot\r\n".to_vec();
        wire.extend(encode_frame(&hello));
        device.write_all(&wire).await.unwrap();
        assert_eq!(link.recv().await.unwrap(), Some(hello));

        link.send(vec![1, 2, 3]).await.unwrap();
        let mut got = [0u8; 16];
        let n = device.read(&mut got).await.unwrap();
        assert_eq!(
            Decoder::default().feed(&got[..n]),
            vec![Event::Frame(vec![1, 2, 3])]
        );
    }

    #[tokio::test]
    async fn second_hello_is_handed_to_a_new_link() {
        let (daemon_side, mut device) = duplex(8192);
        let (mut link, mut h_rx) = link_over(spawn_session(
            daemon_side,
            "t".into(),
            Arc::new(()),
            PING_EVERY,
        ));
        let hello = hello_bytes();
        device.write_all(&encode_frame(&hello)).await.unwrap();
        device.write_all(&encode_frame(&hello)).await.unwrap();
        assert_eq!(link.recv().await.unwrap(), Some(hello.clone()));
        assert_eq!(link.recv().await.unwrap(), None);
        assert_eq!(h_rx.try_recv().expect("session handed off").first, hello);
    }

    #[tokio::test]
    async fn pings_start_after_hello() {
        let every = Duration::from_millis(50);
        let (daemon_side, mut device) = duplex(8192);
        let (mut link, _h) = link_over(spawn_session(daemon_side, "t".into(), Arc::new(()), every));
        tokio::time::sleep(every * 3).await;
        device
            .write_all(&encode_frame(&hello_bytes()))
            .await
            .unwrap();
        link.recv().await.unwrap();
        let mut got = [0u8; 64];
        let n = device.read(&mut got).await.unwrap();
        let ev = Decoder::default().feed(&got[..n]);
        let [Event::Frame(f)] = &ev[..] else {
            panic!("expected one frame, got {ev:?}")
        };
        assert!(matches!(
            Envelope::from_bytes(f).unwrap().msg,
            Message::Ping(_)
        ));
    }
}
