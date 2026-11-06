//! Wire framing for the USB CDC stream: `0x00 ‖ COBS(payload ‖ crc16_be) ‖ 0x00`, mixed with
//! console log text that never contains 0x00.

/// Largest accepted payload (one CBOR envelope).
pub const MAX_PAYLOAD: usize = 4096;
/// Largest chunk between two 0x00 bytes: payload + CRC + COBS overhead.
const MAX_CHUNK: usize = MAX_PAYLOAD + 2 + (MAX_PAYLOAD + 2) / 254 + 2;

/// CRC-16/CCITT-FALSE.
pub fn crc16(data: &[u8]) -> u16 {
    let mut crc = 0xFFFFu16;
    for &b in data {
        crc ^= (b as u16) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ 0x1021
            } else {
                crc << 1
            };
        }
    }
    crc
}

pub fn cobs_encode(data: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(data.len() + data.len() / 254 + 2);
    out.push(0);
    let mut code_at = 0;
    let mut code = 1u8;
    for &b in data {
        if b == 0 {
            out[code_at] = code;
            code_at = out.len();
            out.push(0);
            code = 1;
        } else {
            out.push(b);
            code += 1;
            if code == 0xFF {
                out[code_at] = code;
                code_at = out.len();
                out.push(0);
                code = 1;
            }
        }
    }
    out[code_at] = code;
    out
}

/// `None` if the input is not valid COBS (contains 0x00 or a block overruns).
pub fn cobs_decode(data: &[u8]) -> Option<Vec<u8>> {
    let mut out = Vec::with_capacity(data.len());
    let mut i = 0;
    while i < data.len() {
        let code = data[i] as usize;
        if code == 0 {
            return None;
        }
        let end = i + code;
        if end > data.len() {
            return None;
        }
        if data[i + 1..end].contains(&0) {
            return None;
        }
        out.extend_from_slice(&data[i + 1..end]);
        i = end;
        if code != 0xFF && i < data.len() {
            out.push(0);
        }
    }
    Some(out)
}

pub fn encode_frame(payload: &[u8]) -> Vec<u8> {
    let mut body = Vec::with_capacity(payload.len() + 2);
    body.extend_from_slice(payload);
    body.extend_from_slice(&crc16(payload).to_be_bytes());
    let mut out = vec![0];
    out.extend(cobs_encode(&body));
    out.push(0);
    out
}

fn decode_chunk(chunk: &[u8]) -> Option<Vec<u8>> {
    let mut body = cobs_decode(chunk)?;
    if body.len() < 3 || body.len() - 2 > MAX_PAYLOAD {
        return None;
    }
    let crc = u16::from_be_bytes([body[body.len() - 2], body[body.len() - 1]]);
    body.truncate(body.len() - 2);
    (crc16(&body) == crc).then_some(body)
}

#[derive(Debug, PartialEq, Eq)]
pub enum Event {
    Frame(Vec<u8>),
    /// One complete line of console text, without the line ending.
    Text(String),
}

/// Incremental splitter for the mixed frame/text stream.
#[derive(Default)]
pub struct Decoder {
    buf: Vec<u8>,
    discarding: bool,
}

fn text_like(b: u8) -> bool {
    matches!(b, 0x20..=0x7e | b'\r' | b'\n' | b'\t' | 0x1b)
}

fn push_lines(bytes: &[u8], out: &mut Vec<Event>) {
    for line in String::from_utf8_lossy(bytes).split('\n') {
        let line = line.trim_end_matches('\r');
        if !line.is_empty() {
            out.push(Event::Text(line.to_owned()));
        }
    }
}

impl Decoder {
    pub fn feed(&mut self, data: &[u8]) -> Vec<Event> {
        let mut out = Vec::new();
        for &b in data {
            if b == 0 {
                if self.discarding {
                    self.discarding = false;
                } else if !self.buf.is_empty() {
                    match decode_chunk(&self.buf) {
                        Some(p) => out.push(Event::Frame(p)),
                        None => push_lines(&self.buf, &mut out),
                    }
                }
                self.buf.clear();
            } else if !self.discarding {
                self.buf.push(b);
                if self.buf.len() > MAX_CHUNK {
                    self.buf.clear();
                    self.discarding = true;
                }
            }
        }
        // Log text has no 0x00 terminator, so flush complete lines once the pending
        // bytes up to the last newline are unmistakably text.
        if let Some(nl) = self.buf.iter().rposition(|&b| b == b'\n') {
            if self.buf[..=nl].iter().all(|&b| text_like(b)) {
                push_lines(&self.buf[..=nl], &mut out);
                self.buf.drain(..=nl);
            }
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc_known_vector() {
        assert_eq!(crc16(b"123456789"), 0x29B1);
    }

    #[test]
    fn cobs_round_trip_edge_cases() {
        let mut cases: Vec<Vec<u8>> = vec![
            vec![],
            vec![0],
            vec![0, 0],
            vec![1, 0, 2],
            vec![7; 253],
            vec![7; 254],
            vec![7; 255],
            vec![7; 508],
            vec![7; 600],
        ];
        let mut mixed = vec![9u8; 254];
        mixed.push(0);
        mixed.extend(vec![3u8; 254]);
        cases.push(mixed);
        cases.push((0..=255u8).cycle().take(2000).collect());
        for c in cases {
            let enc = cobs_encode(&c);
            assert!(!enc.contains(&0), "encoded contains zero");
            assert_eq!(
                cobs_decode(&enc).as_deref(),
                Some(&c[..]),
                "len {}",
                c.len()
            );
        }
        assert_eq!(cobs_encode(&[0]), [1, 1]);
        assert_eq!(cobs_encode(&[1, 2, 0, 3]), [3, 1, 2, 2, 3]);
    }

    #[test]
    fn cobs_rejects_malformed() {
        assert_eq!(cobs_decode(&[5, 1, 2]), None);
        assert_eq!(cobs_decode(&[3, 1, 0]), None);
    }

    #[test]
    fn frame_round_trip_with_interleaved_text() {
        let a = vec![0xA1, 0, 0, 5];
        let b = vec![7u8; 300];
        let mut stream = b"boot log\r\n".to_vec();
        stream.extend(encode_frame(&a));
        stream.extend(b"I (12) x: hi\r\n#{\"cat\":\"ui\"}\n");
        stream.extend(encode_frame(&b));
        // Feed byte by byte to exercise every split position.
        let mut d = Decoder::default();
        let mut ev = Vec::new();
        for byte in &stream {
            ev.extend(d.feed(std::slice::from_ref(byte)));
        }
        assert_eq!(
            ev,
            vec![
                Event::Text("boot log".into()),
                Event::Frame(a),
                Event::Text("I (12) x: hi".into()),
                Event::Text("#{\"cat\":\"ui\"}".into()),
                Event::Frame(b),
            ]
        );
    }

    #[test]
    fn corrupt_crc_is_text_not_frame() {
        let mut f = encode_frame(&[1, 2, 3, 4]);
        let n = f.len();
        f[n - 2] ^= 0x55;
        let ev = Decoder::default().feed(&f);
        assert!(ev.iter().all(|e| !matches!(e, Event::Frame(_))));
    }

    #[test]
    fn garbage_and_oversize_are_bounded() {
        let mut d = Decoder::default();
        assert!(d.feed(&vec![0xAA; 100_000]).is_empty());
        assert!(d.buf.len() <= MAX_CHUNK);
        // The stream recovers at the next delimiter.
        let ev = d.feed(&encode_frame(&[9, 9, 9]));
        assert_eq!(ev, vec![Event::Frame(vec![9, 9, 9])]);

        let big = encode_frame(&vec![1u8; MAX_PAYLOAD + 1]);
        assert!(d.feed(&big).iter().all(|e| !matches!(e, Event::Frame(_))));
        let max = encode_frame(&vec![1u8; MAX_PAYLOAD]);
        assert_eq!(d.feed(&max).len(), 1);
    }
}
