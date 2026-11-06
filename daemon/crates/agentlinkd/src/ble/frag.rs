//! BLE fragmentation (PROTOCOL §1). Each ATT write/notification is `[FIRST|LAST|seq:6] data…`.
//! Pure functions so the transport and the tests share one implementation.

pub const FIRST: u8 = 0x80;
pub const LAST: u8 = 0x40;
const SEQ_MASK: u8 = 0x3f;
/// Largest reassembled message (device `caps.max_msg`).
pub const MAX_MESSAGE: usize = 4096;

/// Splits `msg` into fragments whose total size (header included) is at most `chunk_len`.
/// Returns `None` for an empty/oversize message or a `chunk_len` with no room for data.
pub fn fragment(msg: &[u8], chunk_len: usize) -> Option<Vec<Vec<u8>>> {
    if msg.is_empty() || msg.len() > MAX_MESSAGE || chunk_len < 2 {
        return None;
    }
    let n = chunk_len - 1;
    let count = msg.len().div_ceil(n);
    Some(
        msg.chunks(n)
            .enumerate()
            .map(|(i, part)| {
                let mut hdr = (i as u8) & SEQ_MASK;
                if i == 0 {
                    hdr |= FIRST;
                }
                if i + 1 == count {
                    hdr |= LAST;
                }
                let mut f = Vec::with_capacity(part.len() + 1);
                f.push(hdr);
                f.extend_from_slice(part);
                f
            })
            .collect(),
    )
}

/// Reassembles fragments. A seq gap, a continuation without a FIRST, an oversize message or a
/// FIRST arriving mid-message drops the partial message (a mid-message FIRST starts a new one).
#[derive(Default)]
pub struct Reassembler {
    buf: Vec<u8>,
    active: bool,
    next_seq: u8,
}

impl Reassembler {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn reset(&mut self) {
        self.buf.clear();
        self.active = false;
    }

    /// Feeds one fragment; returns the whole message when its LAST fragment arrives.
    pub fn push(&mut self, frag: &[u8]) -> Option<Vec<u8>> {
        let (&hdr, data) = frag.split_first()?;
        let seq = hdr & SEQ_MASK;
        if hdr & FIRST != 0 {
            self.reset();
            self.active = true;
        } else if !self.active || seq != self.next_seq {
            self.reset();
            return None;
        }
        self.next_seq = (seq + 1) & SEQ_MASK;
        if self.buf.len() + data.len() > MAX_MESSAGE {
            self.reset();
            return None;
        }
        self.buf.extend_from_slice(data);
        if hdr & LAST != 0 {
            self.active = false;
            let msg = std::mem::take(&mut self.buf);
            return (!msg.is_empty()).then_some(msg);
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pattern(n: usize) -> Vec<u8> {
        (0..n).map(|i| (i * 7 + 3) as u8).collect()
    }

    fn feed(r: &mut Reassembler, frags: &[Vec<u8>]) -> Vec<Vec<u8>> {
        frags.iter().filter_map(|f| r.push(f)).collect()
    }

    #[test]
    fn round_trip_various_chunk_sizes() {
        for chunk in [2, 3, 20, 23, 185, 244, 512] {
            for len in [1, 2, 19, 20, 21, 243, 244, 245, 1000, MAX_MESSAGE] {
                let msg = pattern(len);
                let frags = fragment(&msg, chunk).unwrap();
                assert!(frags.iter().all(|f| f.len() <= chunk));
                assert_eq!(frags[0][0] & FIRST, FIRST);
                assert_eq!(frags.last().unwrap()[0] & LAST, LAST);
                let mut r = Reassembler::new();
                assert_eq!(feed(&mut r, &frags), vec![msg], "chunk {chunk} len {len}");
            }
        }
    }

    #[test]
    fn single_fragment_has_both_flags() {
        let f = fragment(&[1, 2, 3], 20).unwrap();
        assert_eq!(f, vec![vec![FIRST | LAST, 1, 2, 3]]);
    }

    #[test]
    fn seq_wraps_past_63() {
        let msg = pattern(200); // 200 fragments of 1 byte
        let frags = fragment(&msg, 2).unwrap();
        assert_eq!(frags[63][0] & SEQ_MASK, 63);
        assert_eq!(frags[64][0] & SEQ_MASK, 0);
        let mut r = Reassembler::new();
        assert_eq!(feed(&mut r, &frags), vec![msg]);
    }

    #[test]
    fn lost_middle_fragment_drops_message_and_recovers() {
        let msg = pattern(100);
        let mut frags = fragment(&msg, 20).unwrap();
        frags.remove(2);
        let mut r = Reassembler::new();
        assert!(feed(&mut r, &frags).is_empty());
        let next = pattern(50);
        assert_eq!(feed(&mut r, &fragment(&next, 20).unwrap()), vec![next]);
    }

    #[test]
    fn unexpected_first_restarts_message() {
        let a = pattern(100);
        let b = pattern(30);
        let fa = fragment(&a, 20).unwrap();
        let mut r = Reassembler::new();
        assert!(r.push(&fa[0]).is_none());
        assert!(r.push(&fa[1]).is_none());
        assert_eq!(feed(&mut r, &fragment(&b, 20).unwrap()), vec![b]);
    }

    #[test]
    fn continuation_without_first_is_ignored() {
        let frags = fragment(&pattern(100), 20).unwrap();
        let mut r = Reassembler::new();
        assert!(feed(&mut r, &frags[1..]).is_empty());
    }

    #[test]
    fn oversize_is_rejected_both_ways() {
        assert!(fragment(&pattern(MAX_MESSAGE + 1), 244).is_none());
        assert!(fragment(&[], 20).is_none());
        assert!(fragment(&[1], 1).is_none());
        // A peer streaming more than MAX_MESSAGE bytes is dropped.
        let mut r = Reassembler::new();
        let mut out = None;
        for i in 0..=(MAX_MESSAGE / 100) + 1 {
            let mut f = vec![(i as u8) & SEQ_MASK];
            if i == 0 {
                f[0] |= FIRST;
            }
            f.extend_from_slice(&[0; 100]);
            out = r.push(&f);
        }
        assert!(out.is_none());
        let mut last = vec![LAST | 0x3f];
        last.push(9);
        assert!(r.push(&last).is_none());
    }
}
