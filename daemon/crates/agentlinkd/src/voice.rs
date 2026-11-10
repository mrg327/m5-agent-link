//! Voice dictation, daemon side (issue 20261110-0ea9).
//!
//! The watch streams one recording as `audio_start` + `audio` blocks + `audio_end`. Each block is
//! self-contained IMA ADPCM at 16 kHz mono: a 4-byte header (first sample, step index), then two
//! samples per byte, low nibble first. On `audio_end` the daemon decodes the blocks in order,
//! writes a WAV (0600, deleted right after), runs whisper.cpp on it, and answers with
//! `transcript { aid, text, autosend_ms: 0 }`. The text is typed into the session only when the
//! watch sends `cmd dictate` (after a hold), with the same refusals as quick replies (typing.rs).
//! Speech and transcripts never reach the trace or the audit log.

use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use crate::registry::clip;

pub const RATE: u32 = 16_000;
/// A recording is capped on the watch at 15 s; allow some slack for a late stop.
const MAX_SAMPLES: usize = RATE as usize * 20;
pub const MAX_TEXT: usize = 240;
/// A transcript waits this long for the user's hold before it is forgotten.
pub const KEEP: Duration = Duration::from_secs(300);

const STEP: [i32; 89] = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
    10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
];
const INDEX_ADJ: [i32; 16] = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

/// One IMA ADPCM block as the watch writes it (voice.c `voice_adpcm_block`).
pub fn decode_block(b: &[u8]) -> Vec<i16> {
    if b.len() < 4 {
        return Vec::new();
    }
    let mut pred = i16::from_le_bytes([b[0], b[1]]) as i32;
    let mut index = (b[2] as i32).clamp(0, 88);
    let mut out = Vec::with_capacity(1 + (b.len() - 4) * 2);
    out.push(pred as i16);
    for &byte in &b[4..] {
        for code in [byte & 0x0f, byte >> 4] {
            let step = STEP[index as usize];
            let mut delta = step >> 3;
            if code & 4 != 0 {
                delta += step;
            }
            if code & 2 != 0 {
                delta += step >> 1;
            }
            if code & 1 != 0 {
                delta += step >> 2;
            }
            pred = if code & 8 != 0 {
                pred - delta
            } else {
                pred + delta
            }
            .clamp(-32768, 32767);
            index = (index + INDEX_ADJ[code as usize]).clamp(0, 88);
            out.push(pred as i16);
        }
    }
    out
}

/// 16-bit mono PCM as a WAV file.
pub fn wav(pcm: &[i16]) -> Vec<u8> {
    let data = (pcm.len() * 2) as u32;
    let mut w = Vec::with_capacity(44 + data as usize);
    w.extend_from_slice(b"RIFF");
    w.extend_from_slice(&(36 + data).to_le_bytes());
    w.extend_from_slice(b"WAVEfmt ");
    w.extend_from_slice(&16u32.to_le_bytes());
    w.extend_from_slice(&1u16.to_le_bytes()); // PCM
    w.extend_from_slice(&1u16.to_le_bytes()); // mono
    w.extend_from_slice(&RATE.to_le_bytes());
    w.extend_from_slice(&(RATE * 2).to_le_bytes());
    w.extend_from_slice(&2u16.to_le_bytes());
    w.extend_from_slice(&16u16.to_le_bytes());
    w.extend_from_slice(b"data");
    w.extend_from_slice(&data.to_le_bytes());
    for s in pcm {
        w.extend_from_slice(&s.to_le_bytes());
    }
    w
}

/// whisper's own annotations ("[BLANK_AUDIO]", "(wind blowing)") are not words the user said.
pub fn clean(text: &str) -> String {
    let mut out = String::new();
    let mut depth = 0;
    for c in text.chars() {
        match c {
            '[' | '(' => depth += 1,
            ']' | ')' if depth > 0 => depth -= 1,
            _ if depth == 0 => out.push(if c.is_control() { ' ' } else { c }),
            _ => {}
        }
    }
    let words: Vec<&str> = out.split_whitespace().collect();
    clip(&words.join(" "), MAX_TEXT)
}

/// One recording being received.
pub struct Recording {
    pub sid: Option<u64>,
    blocks: BTreeMap<u64, Vec<u8>>,
    samples: usize,
}

impl Recording {
    pub fn new(sid: Option<u64>) -> Self {
        Recording {
            sid,
            blocks: BTreeMap::new(),
            samples: 0,
        }
    }

    /// Adds block `seq`; false once the recording is longer than any the watch makes.
    pub fn push(&mut self, seq: u64, data: Vec<u8>) -> bool {
        self.samples += 1 + data.len().saturating_sub(4) * 2;
        if self.samples > MAX_SAMPLES {
            return false;
        }
        self.blocks.insert(seq, data);
        true
    }

    /// The decoded speech, blocks in order (a lost block is simply missing).
    pub fn pcm(&self) -> Vec<i16> {
        self.blocks.values().flat_map(|b| decode_block(b)).collect()
    }
}

/// whisper.cpp: the CLI and a model. `AGENTLINK_WHISPER_CLI` / `AGENTLINK_WHISPER_MODEL`, else
/// `~/tools/whisper.cpp` with the base.en model. Voice is offered only when both exist.
#[derive(Clone, Debug)]
pub struct Stt {
    pub cli: PathBuf,
    pub model: PathBuf,
}

impl Stt {
    pub fn find() -> Option<Self> {
        let home = std::env::var_os("HOME")
            .map(PathBuf::from)
            .unwrap_or_default();
        let root = home.join("tools/whisper.cpp");
        let cli = std::env::var_os("AGENTLINK_WHISPER_CLI")
            .map(PathBuf::from)
            .unwrap_or_else(|| root.join("build/bin/whisper-cli"));
        let model = std::env::var_os("AGENTLINK_WHISPER_MODEL")
            .map(PathBuf::from)
            .unwrap_or_else(|| root.join("models/ggml-base.en.bin"));
        (cli.is_file() && model.is_file()).then_some(Stt { cli, model })
    }

    /// Blocking: transcribes `pcm` (16 kHz mono). The WAV lives in `dir` (0700) only while whisper runs.
    pub fn transcribe(&self, pcm: &[i16], dir: &Path, aid: u64) -> Result<String, String> {
        if pcm.len() < RATE as usize / 4 {
            return Ok(String::new()); // under 250 ms: nothing was said
        }
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let _ = std::fs::set_permissions(dir, std::fs::Permissions::from_mode(0o700));
        }
        let path = dir.join(format!("rec-{}-{aid}.wav", std::process::id()));
        let write = || -> std::io::Result<()> {
            let mut o = std::fs::OpenOptions::new();
            o.write(true).create_new(true);
            #[cfg(unix)]
            {
                use std::os::unix::fs::OpenOptionsExt;
                o.mode(0o600);
            }
            use std::io::Write;
            o.open(&path)?.write_all(&wav(pcm))
        };
        write().map_err(|e| e.to_string())?;
        let threads = std::thread::available_parallelism()
            .map(|n| n.get())
            .unwrap_or(4)
            .min(8);
        let out = std::process::Command::new(&self.cli)
            .args(["-m"])
            .arg(&self.model)
            .args(["-f"])
            .arg(&path)
            .args(["-nt", "-np", "-l", "en", "-t", &threads.to_string()])
            .output();
        let _ = std::fs::remove_file(&path);
        let out = out.map_err(|e| format!("whisper did not run: {e}"))?;
        if !out.status.success() {
            return Err(format!("whisper failed ({})", out.status));
        }
        Ok(clean(&String::from_utf8_lossy(&out.stdout)))
    }
}

/// Transcripts waiting for the user's hold, by recording id.
#[derive(Default)]
pub struct Transcripts {
    map: HashMap<u64, (Option<u64>, String, Instant)>,
}

impl Transcripts {
    pub fn put(&mut self, aid: u64, sid: Option<u64>, text: String) {
        self.map.retain(|_, (_, _, at)| at.elapsed() < KEEP);
        self.map.insert(aid, (sid, text, Instant::now()));
    }

    /// The text recording `aid` became, if it was for `sid` and is still fresh; it is used up.
    pub fn take(&mut self, aid: u64, sid: u64) -> Option<String> {
        let (s, text, at) = self.map.get(&aid)?.clone();
        if at.elapsed() >= KEEP || s.is_some_and(|s| s != sid) || text.is_empty() {
            return None;
        }
        self.map.remove(&aid);
        Some(text)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The watch's encoder (voice.c), ported to check the round trip.
    fn encode_block(pcm: &[i16], index_io: &mut i32) -> Vec<u8> {
        let mut pred = pcm[0] as i32;
        let mut index = *index_io;
        let mut out = vec![
            (pred & 0xff) as u8,
            ((pred >> 8) & 0xff) as u8,
            index as u8,
            0,
        ];
        let mut nib = |sample: i32, pred: &mut i32, index: &mut i32| -> u8 {
            let mut step = STEP[*index as usize];
            let mut diff = sample - *pred;
            let mut code = 0u8;
            if diff < 0 {
                code = 8;
                diff = -diff;
            }
            let mut delta = step >> 3;
            if diff >= step {
                code |= 4;
                diff -= step;
                delta += step;
            }
            step >>= 1;
            if diff >= step {
                code |= 2;
                diff -= step;
                delta += step;
            }
            step >>= 1;
            if diff >= step {
                code |= 1;
                delta += step;
            }
            *pred = if code & 8 != 0 {
                *pred - delta
            } else {
                *pred + delta
            }
            .clamp(-32768, 32767);
            *index = (*index + INDEX_ADJ[code as usize]).clamp(0, 88);
            code
        };
        let mut i = 1;
        while i < pcm.len() {
            let lo = nib(pcm[i] as i32, &mut pred, &mut index);
            let hi = if i + 1 < pcm.len() {
                nib(pcm[i + 1] as i32, &mut pred, &mut index)
            } else {
                0
            };
            out.push(lo | hi << 4);
            i += 2;
        }
        *index_io = index;
        out
    }

    #[test]
    fn adpcm_round_trip_tracks_a_tone() {
        let pcm: Vec<i16> = (0..473)
            .map(|i| ((i as f32 * 0.07).sin() * 8000.0) as i16)
            .collect();
        let mut index = 0;
        let block = encode_block(&pcm, &mut index);
        assert_eq!(block.len(), 240, "473 samples fill one 240-byte block");
        let back = decode_block(&block);
        assert_eq!(back.len(), 473);
        let err: f64 = pcm
            .iter()
            .zip(&back)
            .skip(40)
            .map(|(a, b)| (*a as f64 - *b as f64).abs())
            .sum::<f64>()
            / 433.0;
        assert!(err < 400.0, "mean error {err}");
    }

    /// The whole path on real speech: the watch's block format, decoded here, through whisper.cpp.
    /// Runs only where whisper.cpp and its sample are installed (not in CI).
    #[test]
    fn transcribes_real_speech_through_the_watch_format() {
        let Some(stt) = Stt::find() else { return };
        let sample = stt.cli.parent().unwrap().join("../../samples/jfk.wav");
        let Ok(bytes) = std::fs::read(&sample) else {
            return;
        };
        let pcm: Vec<i16> = bytes[44..]
            .chunks_exact(2)
            .map(|c| i16::from_le_bytes([c[0], c[1]]))
            .collect();
        let mut rec = Recording::new(Some(1));
        let mut index = 0;
        for (seq, chunk) in pcm.chunks(473).enumerate() {
            assert!(rec.push(seq as u64, encode_block(chunk, &mut index)));
        }
        let dir = std::env::temp_dir().join(format!("agentlink-voice-test-{}", std::process::id()));
        let text = stt.transcribe(&rec.pcm(), &dir, 1).unwrap();
        let _ = std::fs::remove_dir_all(&dir);
        assert!(
            text.to_lowercase().contains("ask not what your country"),
            "{text}"
        );
    }

    #[test]
    fn wav_header_is_16k_mono() {
        let w = wav(&[0, 1, -1]);
        assert_eq!(&w[0..4], b"RIFF");
        assert_eq!(u32::from_le_bytes(w[24..28].try_into().unwrap()), 16_000);
        assert_eq!(u16::from_le_bytes(w[22..24].try_into().unwrap()), 1);
        assert_eq!(w.len(), 44 + 6);
    }

    #[test]
    fn whisper_annotations_are_dropped() {
        assert_eq!(clean(" [BLANK_AUDIO]\n"), "");
        assert_eq!(
            clean(" Run the tests again. (keyboard clicking)\n"),
            "Run the tests again."
        );
        assert_eq!(clean("a\u{7}b"), "a b");
    }

    #[test]
    fn blocks_are_put_in_order_and_long_recordings_refused() {
        let mut r = Recording::new(Some(3));
        assert!(r.push(1, vec![10, 0, 0, 0]));
        assert!(r.push(0, vec![5, 0, 0, 0]));
        assert_eq!(r.pcm(), vec![5, 10]);
        let mut r = Recording::new(None);
        let ok = (0..2000).all(|i| r.push(i, vec![0; 240]));
        assert!(!ok, "past 20 s the recording is refused");
    }

    #[test]
    fn a_transcript_is_used_once_and_only_for_its_session() {
        let mut t = Transcripts::default();
        t.put(4, Some(3), "run the tests".into());
        assert_eq!(t.take(4, 9), None, "another session");
        assert_eq!(t.take(4, 3).as_deref(), Some("run the tests"));
        assert_eq!(t.take(4, 3), None, "used up");
        t.put(5, Some(3), String::new());
        assert_eq!(t.take(5, 3), None, "nothing heard: nothing to send");
    }
}
