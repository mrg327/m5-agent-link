//! heatshrink (LZSS), bit for bit as the reference C encoder (heatshrink_encoder.c with its search
//! index) and decoder do it, so a patch made here equals the one Python detools makes.
//!
//! The stream is a sequence of items, most significant bit first: tag 1 + 8-bit literal, or tag 0 +
//! `window_sz2`-bit (distance - 1) + `lookahead_sz2`-bit (length - 1). The window starts as zeros on
//! both sides, and the encoder does find matches in them. The last byte is padded with zero bits.

struct BitWriter {
    out: Vec<u8>,
    cur: u8,
    mask: u8,
}

impl BitWriter {
    fn push(&mut self, count: u32, bits: u32) {
        for i in (0..count).rev() {
            if (bits >> i) & 1 == 1 {
                self.cur |= self.mask;
            }
            self.mask >>= 1;
            if self.mask == 0 {
                self.out.push(self.cur);
                self.cur = 0;
                self.mask = 0x80;
            }
        }
    }

    fn finish(mut self) -> Vec<u8> {
        if self.mask != 0x80 {
            self.out.push(self.cur);
        }
        self.out
    }
}

/// The encoder's state between input blocks: `buf` holds the previous window (the backlog) and then
/// up to one window of input.
struct Encoder {
    w2: u32,
    l2: u32,
    buf: Vec<u8>,
    index: Vec<i32>,
    input_size: usize,
    msi: usize,
    bits: BitWriter,
}

impl Encoder {
    fn window(&self) -> usize {
        1 << self.w2
    }

    /// Indexing, searching and emitting for the buffered input (the C encoder's FILLED..SAVE_BACKLOG
    /// or FILLED..FLUSH_BITS states).
    fn step(&mut self, fin: bool) {
        let w = self.window();
        let l = 1usize << self.l2;
        let mut last = [-1i32; 256];
        for i in 0..w + self.input_size {
            let v = self.buf[i] as usize;
            self.index[i] = last[v];
            last[v] = i as i32;
        }
        let break_even = ((1 + self.w2 + self.l2) / 8) as usize;
        loop {
            let lim = self.input_size as isize - if fin { 1 } else { l as isize };
            if self.msi as isize > lim {
                break;
            }
            let end = w + self.msi;
            let start = self.msi;
            let maxlen = l.min(self.input_size - self.msi);
            let buf = &self.buf;
            let mut best = 0usize;
            let mut best_pos = 0usize;
            let mut pos = self.index[end];
            while pos >= start as i32 {
                let p = pos as usize;
                pos = self.index[p];
                if buf[p + best] != buf[end + best] {
                    continue;
                }
                let mut len = 1;
                while len < maxlen && buf[p + len] == buf[end + len] {
                    len += 1;
                }
                if len > best {
                    best = len;
                    best_pos = p;
                    if len == maxlen {
                        break;
                    }
                }
            }
            if best > break_even {
                self.bits.push(1, 0);
                self.bits.push(self.w2, (end - best_pos - 1) as u32);
                self.bits.push(self.l2, (best - 1) as u32);
                self.msi += best;
            } else {
                let c = self.buf[end];
                self.bits.push(1, 1);
                self.bits.push(8, c.into());
                self.msi += 1;
            }
        }
    }

    fn save_backlog(&mut self) {
        let w = self.window();
        let rem = w - self.msi;
        self.buf.copy_within(w - rem..2 * w, 0);
        self.input_size -= self.msi;
        self.msi = 0;
    }
}

/// Compresses `data` (without the one-byte parameter header detools puts in front).
pub fn compress(data: &[u8], window_sz2: u8, lookahead_sz2: u8) -> Vec<u8> {
    assert!((4..=15).contains(&window_sz2) && (3..window_sz2).contains(&lookahead_sz2));
    let w = 1usize << window_sz2;
    let mut e = Encoder {
        w2: window_sz2.into(),
        l2: lookahead_sz2.into(),
        buf: vec![0; 2 * w],
        index: vec![-1; 2 * w],
        input_size: 0,
        msi: 0,
        bits: BitWriter {
            out: Vec::with_capacity(data.len() / 4),
            cur: 0,
            mask: 0x80,
        },
    };
    let mut rest = data;
    loop {
        let take = (w - e.input_size).min(rest.len());
        let at = w + e.input_size;
        e.buf[at..at + take].copy_from_slice(&rest[..take]);
        e.input_size += take;
        rest = &rest[take..];
        if e.input_size == w {
            e.step(false);
            e.save_backlog();
        } else {
            e.step(true);
            return e.bits.finish();
        }
    }
}

/// Decompresses a whole stream. Trailing bits too few for an item are the padding.
pub fn decompress(data: &[u8], window_sz2: u8, lookahead_sz2: u8) -> Vec<u8> {
    let (w2, l2) = (usize::from(window_sz2), usize::from(lookahead_sz2));
    let mask = (1usize << w2) - 1;
    let mut window = vec![0u8; 1 << w2];
    let mut head = 0usize;
    let mut out = Vec::with_capacity(data.len() * 4);
    let total = data.len() * 8;
    let mut at = 0usize;
    let read = |n: usize, at: &mut usize| -> usize {
        let mut v = 0;
        for _ in 0..n {
            let bit = (data[*at / 8] >> (7 - *at % 8)) & 1;
            v = (v << 1) | usize::from(bit);
            *at += 1;
        }
        v
    };
    while at < total {
        if read(1, &mut at) == 1 {
            if total - at < 8 {
                break;
            }
            let c = read(8, &mut at) as u8;
            out.push(c);
            window[head & mask] = c;
            head = head.wrapping_add(1);
        } else {
            if total - at < w2 + l2 {
                break;
            }
            let dist = read(w2, &mut at) + 1;
            let count = read(l2, &mut at) + 1;
            for _ in 0..count {
                let c = window[head.wrapping_sub(dist) & mask];
                out.push(c);
                window[head & mask] = c;
                head = head.wrapping_add(1);
            }
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trips_across_block_boundaries() {
        let mut data = Vec::new();
        let mut x = 7u32;
        for i in 0..20000u32 {
            x = x.wrapping_mul(1_103_515_245).wrapping_add(12345);
            data.push(if i % 3 == 0 {
                (x >> 16) as u8
            } else {
                (i / 100) as u8
            });
        }
        for (w, l) in [(8, 7), (11, 10), (4, 3)] {
            for n in [0, 1, 2, 3, 255, 256, 2047, 2048, 2049, 4096, 20000] {
                let c = compress(&data[..n], w, l);
                assert_eq!(decompress(&c, w, l), &data[..n], "w{w} l{l} n{n}");
            }
        }
        // zeros match the zero-filled window from the very first byte
        assert!(compress(&[0; 4096], 11, 10).len() < 16);
    }
}
