//! Over-the-air firmware update, daemon side (PROTOCOL §6c, FIRMWARE.md §9).
//!
//! Only on request (`{"ctl":"ota"}` from `agentlink ota`), never on its own. The request streams
//! progress lines back on the same connection; closing it stops the transfer. The pure parts (the
//! image check and the send window) are separate so they can be tested without a device.

use crate::trace::Trace;
use agentlink_proto::{FwAck, FwBegin, FwChunk, FwStatus, Message};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::collections::{HashMap, VecDeque};
use std::sync::Mutex;
use std::time::Duration;
use tokio::sync::mpsc::{unbounded_channel, UnboundedReceiver, UnboundedSender};
use tokio::time::Instant;

/// Bytes per `fw_chunk` (the protocol's maximum).
pub const CHUNK: usize = 2048;
/// Chunks sent and not yet answered.
pub const WINDOW: usize = 4;
/// The OTA slot's size.
pub const MAX_IMAGE: usize = 0x60_0000;
/// No answer from the watch for this long ends the transfer.
const ACK_TIMEOUT: Duration = Duration::from_secs(10);
/// How long a dropped link may take to come back before the transfer is given up.
const RECONNECT_WAIT: Duration = Duration::from_secs(20);
/// How long the watch may take to restart into the new image and say hello.
const RESTART_WAIT: Duration = Duration::from_secs(90);
/// Progress lines at most this often, unless the percentage moved by `PROGRESS_STEP`.
const PROGRESS_EVERY: Duration = Duration::from_secs(1);
const PROGRESS_STEP: u64 = 5;

/// What the image check learned.
#[derive(Debug, PartialEq, Eq)]
pub struct ImageInfo {
    pub version: String,
}

fn cstr(b: &[u8]) -> String {
    let end = b.iter().position(|&c| c == 0).unwrap_or(b.len());
    String::from_utf8_lossy(&b[..end]).into_owned()
}

/// Refuses what the watch would refuse anyway, with a message that says why. `unsigned` skips only
/// the signature check (for testing the device's own refusal).
pub fn check_image(data: &[u8], unsigned: bool) -> Result<ImageInfo, String> {
    if data.len() > MAX_IMAGE {
        return Err(format!(
            "image is {} bytes, more than the {} byte update slot",
            data.len(),
            MAX_IMAGE
        ));
    }
    if data.len() < 112 || data[0] != 0xE9 {
        return Err("not an ESP32 app image (bad magic byte)".into());
    }
    if data[32..36] != [0x32, 0x54, 0xCD, 0xAB] {
        return Err("not an ESP-IDF app image (no app description at offset 32)".into());
    }
    let project = cstr(&data[80..112]);
    if project != "agentlink" {
        return Err(format!(
            "image is for project \"{project}\", not \"agentlink\""
        ));
    }
    if !unsigned {
        let n = data.len();
        if !n.is_multiple_of(4096) || data[n - 4096] != 0xE7 || data[n - 4095] != 0x02 {
            return Err(
                "image is not signed (build with the signing key; see FIRMWARE.md §9)".into(),
            );
        }
    }
    Ok(ImageInfo {
        version: cstr(&data[48..80]),
    })
}

/// One chunk to send: where it starts and how long it is.
pub type ChunkAt = (u64, usize);

/// The send window as a pure state machine: fed the device's `go` acks, it says which chunks go out.
#[derive(Debug)]
pub struct Window {
    size: u64,
    /// Everything below this offset has been sent (maybe not yet acked).
    sent: u64,
    /// The offset the device last said it wants.
    acked: u64,
    /// The `next` each unanswered chunk will be acked with, oldest first.
    queue: VecDeque<u64>,
    /// Acks still to come for chunks sent before a rewind: they say nothing new.
    stale: usize,
}

impl Window {
    pub fn new(size: u64) -> Self {
        Window {
            size,
            sent: 0,
            acked: 0,
            queue: VecDeque::new(),
            stale: 0,
        }
    }

    pub fn acked(&self) -> u64 {
        self.acked
    }

    fn fill(&mut self) -> Vec<ChunkAt> {
        let mut out = Vec::new();
        while self.queue.len() < WINDOW && self.sent < self.size {
            let len = (self.size - self.sent).min(CHUNK as u64) as usize;
            out.push((self.sent, len));
            self.sent += len as u64;
            self.queue.push_back(self.sent);
        }
        out
    }

    /// The device answered `fw_begin` with `go`: start (or resume) from `next`.
    pub fn begin(&mut self, next: u64) -> Result<Vec<ChunkAt>, String> {
        if next > self.size {
            return Err(format!("device wants offset {next} of {}", self.size));
        }
        self.sent = next;
        self.acked = next;
        self.queue.clear();
        self.stale = 0;
        Ok(self.fill())
    }

    /// A `go` ack for a chunk. A `next` that is not the one expected means the device skipped a chunk
    /// (a gap or a duplicate): resend from where it says.
    pub fn on_go(&mut self, next: u64) -> Result<Vec<ChunkAt>, String> {
        if next > self.size {
            return Err(format!("device wants offset {next} of {}", self.size));
        }
        if self.stale > 0 {
            if next == self.acked {
                self.stale -= 1;
                return Ok(Vec::new());
            }
            self.stale = 0;
        }
        if self.queue.front() == Some(&next) {
            self.queue.pop_front();
            self.acked = next;
        } else if let Some(i) = self.queue.iter().position(|&n| n == next) {
            // an ack for a later chunk: the earlier acks were lost, the data got there
            self.queue.drain(..=i);
            self.acked = next;
        } else if next == self.acked && self.queue.is_empty() {
            return Ok(Vec::new());
        } else {
            self.stale = self.queue.len().saturating_sub(1);
            self.queue.clear();
            self.sent = next;
            self.acked = next;
        }
        Ok(self.fill())
    }
}

/// What the link tasks tell a running update.
enum Event {
    Ack {
        key: String,
        ack: FwAck,
    },
    Hello {
        key: String,
        link_id: u64,
        fw: String,
    },
    Down {
        key: String,
        link_id: u64,
    },
}

struct Dev {
    link_id: u64,
    minor: u64,
    fw: String,
    usb: bool,
}

#[derive(Default)]
struct Inner {
    devs: HashMap<String, Dev>,
    txs: HashMap<u64, UnboundedSender<Message>>,
    active: Option<UnboundedSender<Event>>,
}

/// Connected links as far as updating is concerned, and the one update that may run.
pub struct Ota {
    inner: Mutex<Inner>,
    trace: Trace,
}

struct ActiveGuard<'a>(&'a Ota);

impl Drop for ActiveGuard<'_> {
    fn drop(&mut self) {
        self.0.inner.lock().unwrap().active = None;
    }
}

/// `{"ctl":"ota", "path", "unsigned"?, "device"?}`
pub struct Request {
    pub path: String,
    pub unsigned: bool,
    pub device: Option<String>,
}

impl Request {
    pub fn from_json(v: &Value) -> Option<Request> {
        Some(Request {
            path: v["path"].as_str()?.to_owned(),
            unsigned: v["unsigned"].as_bool() == Some(true),
            device: v["device"].as_str().map(str::to_owned),
        })
    }
}

impl Ota {
    pub fn new(trace: Trace) -> Self {
        Ota {
            inner: Mutex::default(),
            trace,
        }
    }

    // ── from the link tasks ──

    /// A device said hello (also a re-hello on a live link). Like the watch (link.c), a live USB link is
    /// preferred: a hello on another transport does not take the device from it.
    pub fn hello(&self, key: &str, link_id: u64, minor: u64, fw: &str, usb: bool) {
        let mut g = self.inner.lock().unwrap();
        if !usb
            && g.devs
                .get(key)
                .is_some_and(|d| d.usb && d.link_id != link_id)
        {
            return;
        }
        g.devs.insert(
            key.to_owned(),
            Dev {
                link_id,
                minor,
                fw: fw.to_owned(),
                usb,
            },
        );
        if g.txs.contains_key(&link_id) {
            Self::notify(&g, || Event::Hello {
                key: key.to_owned(),
                link_id,
                fw: fw.to_owned(),
            });
        }
    }

    /// The link task is serving: messages for the device go through `tx`.
    pub fn attach(&self, link_id: u64, tx: UnboundedSender<Message>) {
        let mut g = self.inner.lock().unwrap();
        g.txs.insert(link_id, tx);
        let found = g
            .devs
            .iter()
            .find(|(_, d)| d.link_id == link_id)
            .map(|(k, d)| (k.clone(), d.fw.clone()));
        if let Some((key, fw)) = found {
            Self::notify(&g, || Event::Hello { key, link_id, fw });
        }
    }

    pub fn link_down(&self, link_id: u64) {
        let mut g = self.inner.lock().unwrap();
        g.txs.remove(&link_id);
        let key = g
            .devs
            .iter()
            .find(|(_, d)| d.link_id == link_id)
            .map(|(k, _)| k.clone());
        if let Some(key) = key {
            g.devs.remove(&key);
            Self::notify(&g, || Event::Down { key, link_id });
        }
    }

    pub fn on_ack(&self, key: &str, ack: FwAck) {
        let g = self.inner.lock().unwrap();
        Self::notify(&g, || Event::Ack {
            key: key.to_owned(),
            ack,
        });
    }

    fn notify(g: &Inner, ev: impl FnOnce() -> Event) {
        if let Some(a) = &g.active {
            let _ = a.send(ev());
        }
    }

    fn send(&self, key: &str, msg: Message) -> bool {
        let g = self.inner.lock().unwrap();
        g.devs
            .get(key)
            .and_then(|d| g.txs.get(&d.link_id))
            .is_some_and(|tx| tx.send(msg).is_ok())
    }

    // ── the update ──

    fn pick(&self, want: Option<&str>) -> Result<(String, u64, String), String> {
        let g = self.inner.lock().unwrap();
        let mut ok: Vec<_> = g
            .devs
            .iter()
            .filter(|(k, d)| {
                d.minor >= crate::device::FW_MINOR && want.is_none_or(|w| k.starts_with(w))
            })
            .collect();
        ok.sort_by(|a, b| a.0.cmp(b.0));
        match ok.as_slice() {
            [] => Err(if want.is_some() {
                "no such watch connected with firmware that supports OTA (proto 0.15)".into()
            } else {
                "no watch connected with firmware that supports OTA (proto 0.15)".into()
            }),
            [(k, d)] => Ok(((*k).clone(), d.link_id, d.fw.clone())),
            many => Err(format!(
                "several watches are connected ({}); pick one with --device",
                many.iter()
                    .map(|(k, _)| k.as_str())
                    .collect::<Vec<_>>()
                    .join(", ")
            )),
        }
    }

    /// Runs one update to its end, reporting on `out`. Dropping the receiver of `out` stops it.
    pub async fn run(&self, req: Request, out: UnboundedSender<Value>) {
        let end = match self.run_inner(&req, &out).await {
            Ok(v) => v,
            Err(msg) => json!({"ota": "error", "msg": msg}),
        };
        self.trace.audit("ota_end", None, None, end.clone());
        let _ = out.send(end);
    }

    async fn run_inner(
        &self,
        req: &Request,
        out: &UnboundedSender<Value>,
    ) -> Result<Value, String> {
        let data = tokio::fs::read(&req.path)
            .await
            .map_err(|e| format!("cannot read {}: {e}", req.path))?;
        let info = check_image(&data, req.unsigned)?;
        let sha: [u8; 32] = Sha256::digest(&data).into();
        let (tx, mut rx) = unbounded_channel();
        let guard = {
            let mut g = self.inner.lock().unwrap();
            if g.active.is_some() {
                return Err("an update is already running".into());
            }
            g.active = Some(tx);
            ActiveGuard(self)
        };
        let (key, link_id, old_fw) = self.pick(req.device.as_deref())?;
        let size = data.len() as u64;
        self.trace.audit(
            "ota_start",
            None,
            None,
            json!({"device": key, "size": size, "sha": hex::encode(sha), "ver": info.version, "old": old_fw}),
        );
        let _ = out.send(json!({"ota": "begin", "size": size, "ver": info.version, "from": old_fw, "device": key}));

        let mut xfer = Transfer {
            ota: self,
            key: &key,
            data: &data,
            sha,
            ver: &info.version,
            out,
        };
        xfer.run(&mut rx, link_id).await?;
        let _ = out.send(json!({"ota": "done"}));
        let _ = out.send(json!({"ota": "restarting"}));

        // The watch restarts into the new image and says hello again.
        let end = Instant::now() + RESTART_WAIT;
        loop {
            let ev = tokio::select! {
                _ = out.closed() => return Err("cancelled".into()),
                ev = tokio::time::timeout_at(end, rx.recv()) => ev,
            };
            match ev {
                Err(_) | Ok(None) => {
                    return Err("the watch did not come back within 90 s of the update".into())
                }
                Ok(Some(Event::Hello { key: k, fw, .. })) if k == key => {
                    drop(guard);
                    return Ok(if fw.trim() == info.version.trim() {
                        json!({"ota": "ok", "fw": fw})
                    } else {
                        json!({"ota": "rolled_back", "fw": fw})
                    });
                }
                Ok(Some(_)) => {}
            }
        }
    }
}

struct Transfer<'a> {
    ota: &'a Ota,
    key: &'a str,
    data: &'a [u8],
    sha: [u8; 32],
    ver: &'a str,
    out: &'a UnboundedSender<Value>,
}

impl Transfer<'_> {
    fn begin(&self) {
        self.ota.send(
            self.key,
            Message::FwBegin(FwBegin {
                size: self.data.len() as u64,
                sha: self.sha,
                ver: Some(self.ver.to_owned()),
            }),
        );
    }

    fn stop(&self) {
        self.ota.send(
            self.key,
            Message::FwBegin(FwBegin {
                size: 0,
                sha: [0; 32],
                ver: None,
            }),
        );
    }

    fn send_chunks(&self, chunks: Vec<ChunkAt>) {
        for (off, len) in chunks {
            let o = off as usize;
            self.ota.send(
                self.key,
                Message::FwChunk(FwChunk {
                    off,
                    data: self.data[o..o + len].to_vec(),
                }),
            );
        }
    }

    /// From fw_begin to the device's `done`.
    async fn run(
        &mut self,
        rx: &mut UnboundedReceiver<Event>,
        mut link_id: u64,
    ) -> Result<(), String> {
        let size = self.data.len() as u64;
        let mut win = Window::new(size);
        let mut started = false;
        let mut down = false;
        let mut deadline = Instant::now() + ACK_TIMEOUT;
        let mut last_say = (Instant::now(), 0u64);
        self.begin();
        loop {
            let ev = tokio::select! {
                _ = self.out.closed() => {
                    self.stop();
                    return Err("cancelled".into());
                }
                ev = tokio::time::timeout_at(deadline, rx.recv()) => ev,
            };
            let ev = match ev {
                Ok(Some(ev)) => ev,
                Ok(None) => return Err("internal: event channel closed".into()),
                Err(_) if down => return Err("link to the watch dropped".into()),
                Err(_) => {
                    self.stop();
                    return Err("no answer from the watch for 10 s".into());
                }
            };
            match ev {
                Event::Down { key, link_id: l } if key == self.key && l == link_id => {
                    down = true;
                    deadline = Instant::now() + RECONNECT_WAIT;
                }
                Event::Hello {
                    key, link_id: l, ..
                } if key == self.key => {
                    // reconnected (or rebooted): open the same transfer again, the device resumes
                    let minor_ok = self
                        .ota
                        .inner
                        .lock()
                        .unwrap()
                        .devs
                        .get(&key)
                        .is_some_and(|d| d.minor >= crate::device::FW_MINOR);
                    if !minor_ok {
                        return Err("the watch came back without OTA support".into());
                    }
                    link_id = l;
                    down = false;
                    started = false;
                    deadline = Instant::now() + ACK_TIMEOUT;
                    self.begin();
                }
                Event::Ack { key, ack } if key == self.key => {
                    deadline = Instant::now() + ACK_TIMEOUT;
                    let why = |what: &str| match &ack.msg {
                        Some(m) => format!("{what}: {m}"),
                        None => what.to_owned(),
                    };
                    match ack.st {
                        FwStatus::Go => {
                            let chunks = if started {
                                win.on_go(ack.next)?
                            } else {
                                started = true;
                                win.begin(ack.next)?
                            };
                            self.send_chunks(chunks);
                            self.progress(&mut last_say, win.acked(), size);
                        }
                        FwStatus::Done => return Ok(()),
                        FwStatus::Busy => return Err(why("the watch is busy")),
                        FwStatus::Bad => return Err(why("the watch refused the image")),
                        FwStatus::Fail => return Err(why("the watch failed to write the image")),
                        FwStatus::Stopped => return Err(why("the watch stopped the update")),
                    }
                }
                _ => {}
            }
        }
    }

    fn progress(&self, last: &mut (Instant, u64), next: u64, size: u64) {
        let pct = next * 100 / size.max(1);
        if next > 0
            && (last.0.elapsed() >= PROGRESS_EVERY || pct >= last.1 + PROGRESS_STEP || next == size)
        {
            *last = (Instant::now(), pct);
            let _ = self
                .out
                .send(json!({"ota": "progress", "next": next, "size": size}));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_watch_on_usb_and_ble_is_updated_over_usb() {
        let ota = Ota::new(Trace::null());
        let link = |ota: &Ota| ota.pick(None).map(|(_, l, _)| l);
        ota.hello("w", 1, 16, "a", true);
        ota.hello("w", 2, 16, "a", false); // BLE says hello last; the watch stays on USB
        assert_eq!(link(&ota), Ok(1));
        ota.link_down(1); // unplugged: the watch moves to BLE and says hello there again
        ota.hello("w", 2, 16, "a", false);
        assert_eq!(link(&ota), Ok(2));
        ota.hello("w", 3, 16, "a", true); // plugged back in: USB takes over
        assert_eq!(link(&ota), Ok(3));
    }

    fn image(signed: bool, len: usize) -> Vec<u8> {
        let mut d = vec![0u8; len];
        d[0] = 0xE9;
        d[32..36].copy_from_slice(&[0x32, 0x54, 0xCD, 0xAB]);
        d[48..53].copy_from_slice(b"1.2.3");
        d[80..89].copy_from_slice(b"agentlink");
        if signed {
            d[len - 4096] = 0xE7;
            d[len - 4095] = 0x02;
        }
        d
    }

    #[test]
    fn image_checks() {
        let ok = check_image(&image(true, 8192), false).unwrap();
        assert_eq!(ok.version, "1.2.3");
        assert!(check_image(&image(false, 8192), false)
            .unwrap_err()
            .contains("not signed"));
        assert!(check_image(&image(false, 8192), true).is_ok());
        let mut d = image(true, 8192);
        d[0] = 0;
        assert!(check_image(&d, true).unwrap_err().contains("magic"));
        let mut d = image(true, 8192);
        d[33] = 0;
        assert!(check_image(&d, true).unwrap_err().contains("description"));
        let mut d = image(true, 8192);
        d[80] = b'x';
        assert!(check_image(&d, true).unwrap_err().contains("project"));
        assert!(check_image(&image(true, MAX_IMAGE + 4096), false)
            .unwrap_err()
            .contains("slot"));
        let mut d = image(true, 8192);
        d.push(0); // not a multiple of 4096
        assert!(check_image(&d, false).unwrap_err().contains("not signed"));
    }

    fn offs(c: Vec<ChunkAt>) -> Vec<u64> {
        c.into_iter().map(|(o, _)| o).collect()
    }

    const C: u64 = CHUNK as u64;

    #[test]
    fn window_fills_then_slides() {
        let mut w = Window::new(10 * C + 5);
        assert_eq!(offs(w.begin(0).unwrap()), [0, C, 2 * C, 3 * C]);
        assert_eq!(offs(w.on_go(C).unwrap()), [4 * C]);
        assert_eq!(offs(w.on_go(2 * C).unwrap()), [5 * C]);
        assert_eq!(w.acked(), 2 * C);
    }

    #[test]
    fn window_last_chunk_is_short_and_nothing_follows() {
        let mut w = Window::new(C + 5);
        assert_eq!(w.begin(0).unwrap(), [(0, CHUNK), (C, 5)]);
        assert!(w.on_go(C).unwrap().is_empty());
        assert!(w.on_go(C + 5).unwrap().is_empty());
    }

    #[test]
    fn window_resumes_from_the_devices_offset() {
        let mut w = Window::new(10 * C);
        assert_eq!(offs(w.begin(6 * C).unwrap()), [6 * C, 7 * C, 8 * C, 9 * C]);
        assert!(w.begin(11 * C).is_err());
    }

    #[test]
    fn window_rewinds_after_a_gap_and_ignores_the_stale_acks() {
        let mut w = Window::new(20 * C);
        w.begin(0).unwrap();
        w.on_go(C).unwrap();
        // chunk 1 was lost: the device acks the later ones with the offset it wants
        assert_eq!(offs(w.on_go(C).unwrap()), [C, 2 * C, 3 * C, 4 * C]);
        // two more acks for chunks sent before the rewind carry the same offset: no resend
        assert!(w.on_go(C).unwrap().is_empty());
        assert!(w.on_go(C).unwrap().is_empty());
        // then progress is normal again
        assert_eq!(offs(w.on_go(2 * C).unwrap()), [5 * C]);
        assert_eq!(w.acked(), 2 * C);
    }

    #[test]
    fn window_skips_over_a_lost_ack() {
        let mut w = Window::new(20 * C);
        w.begin(0).unwrap();
        assert_eq!(offs(w.on_go(2 * C).unwrap()), [4 * C, 5 * C]);
        assert_eq!(w.acked(), 2 * C);
    }
}
