//! The bsdiff algorithm as detools runs it (detools/bsdiff.c, after Colin Percival's bsdiff 4), emitting
//! the sequential patch's uncompressed steps: diff size, diff bytes (new - old, bytewise), extra size,
//! extra bytes, then the signed seek of the old-image pointer.

/// detools' variable-length integer: sign in bit 6 of the first byte, 6 then 7 bits per byte, little
/// end first, bit 7 set while more bytes follow.
pub fn pack_size(out: &mut Vec<u8>, value: i64) {
    if value == 0 {
        out.push(0);
        return;
    }
    let mut v = value.unsigned_abs();
    let sign = if value < 0 { 0x40 } else { 0 };
    out.push(0x80 | sign | (v & 0x3f) as u8);
    v >>= 6;
    while v > 0 {
        out.push(0x80 | (v & 0x7f) as u8);
        v >>= 7;
    }
    *out.last_mut().unwrap() &= 0x7f;
}

fn matchlen(a: &[u8], b: &[u8]) -> i64 {
    a.iter().zip(b).take_while(|(x, y)| x == y).count() as i64
}

/// The longest match of `to` among the old image's suffixes: (length, position).
fn search(sa: &[i32], from: &[u8], to: &[u8], mut lo: usize, mut hi: usize) -> (i64, i64) {
    loop {
        if hi - lo < 2 {
            let (a, b) = (sa[lo] as usize, sa[hi] as usize);
            let x = matchlen(&from[a..], to);
            let y = matchlen(&from[b..], to);
            return if x > y { (x, a as i64) } else { (y, b as i64) };
        }
        let mid = lo + (hi - lo) / 2;
        let s = &from[sa[mid] as usize..];
        let n = s.len().min(to.len());
        if s[..n] < to[..n] {
            lo = mid;
        } else {
            hi = mid;
        }
    }
}

/// The suffix array with the empty suffix first, as detools builds it.
fn suffix_array(from: &[u8]) -> Vec<i32> {
    let n = i32::try_from(from.len()).expect("old image under 2 GB");
    let mut sa = vec![0i32; from.len() + 1];
    sa[0] = n;
    if n > 0 {
        divsufsort::sort_in_place(from, &mut sa[1..]);
    }
    sa
}

struct Last {
    scan: i64,
    pos: i64,
    offset: i64,
}

fn write_step(out: &mut Vec<u8>, from: &[u8], to: &[u8], scan: i64, pos: i64, last: &mut Last) {
    let (n_from, n_to) = (from.len() as i64, to.len() as i64);
    let f = |i: i64| from[i as usize];
    let t = |i: i64| to[i as usize];

    let (mut s, mut sf, mut diff_size, mut i) = (0i64, 0i64, 0i64, 0i64);
    while last.scan + i < scan && last.pos + i < n_from {
        if f(last.pos + i) == t(last.scan + i) {
            s += 1;
        }
        i += 1;
        if s * 2 - i > sf * 2 - diff_size {
            sf = s;
            diff_size = i;
        }
    }

    let mut lenb = 0i64;
    if scan < n_to {
        let (mut s, mut sb) = (0i64, 0i64);
        let mut i = 1;
        while scan >= last.scan + i && pos >= i {
            if f(pos - i) == t(scan - i) {
                s += 1;
            }
            if s * 2 - i > sb * 2 - lenb {
                sb = s;
                lenb = i;
            }
            i += 1;
        }
    }

    let overlap = (last.scan + diff_size) - (scan - lenb);
    if overlap > 0 {
        let (mut s, mut ss, mut lens) = (0i64, 0i64, 0i64);
        for i in 0..overlap {
            if t(last.scan + diff_size - overlap + i) == f(last.pos + diff_size - overlap + i) {
                s += 1;
            }
            if t(scan - lenb + i) == f(pos - lenb + i) {
                s -= 1;
            }
            if s > ss {
                ss = s;
                lens = i + 1;
            }
        }
        diff_size += lens - overlap;
        lenb -= lens;
    }

    pack_size(out, diff_size);
    let (ls, lp) = (last.scan as usize, last.pos as usize);
    let d = diff_size as usize;
    out.extend(
        to[ls..ls + d]
            .iter()
            .zip(&from[lp..lp + d])
            .map(|(a, b)| a.wrapping_sub(*b)),
    );

    let extra_pos = last.scan + diff_size;
    let extra_size = scan - lenb - extra_pos;
    pack_size(out, extra_size);
    out.extend_from_slice(&to[extra_pos as usize..(extra_pos + extra_size) as usize]);

    pack_size(out, (pos - lenb) - (last.pos + diff_size));

    last.scan = scan - lenb;
    last.pos = pos - lenb;
    last.offset = pos - scan;
}

/// Appends the steps that turn `from` into `to`.
pub fn diff(out: &mut Vec<u8>, from: &[u8], to: &[u8]) {
    let sa = suffix_array(from);
    let (n_from, n_to) = (from.len() as i64, to.len() as i64);
    let aligned = |i: i64, off: i64| {
        let j = i + off;
        j >= 0 && j < n_from && from[j as usize] == to[i as usize]
    };
    let (mut scan, mut len, mut pos) = (0i64, 0i64, 0i64);
    let mut last = Last {
        scan: 0,
        pos: 0,
        offset: 0,
    };
    while scan < n_to {
        let mut from_score = 0i64;
        scan += len;
        let mut scsc = scan;
        while scan < n_to {
            (len, pos) = search(&sa, from, &to[scan as usize..], 0, from.len());
            while scsc < scan + len {
                if aligned(scsc, last.offset) {
                    from_score += 1;
                }
                scsc += 1;
            }
            if (len == from_score && len != 0) || len > from_score + 8 {
                break;
            }
            if aligned(scan, last.offset) {
                from_score -= 1;
            }
            scan += 1;
        }
        if len != from_score || scan == n_to {
            write_step(out, from, to, scan, pos, &mut last);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sizes_pack_like_detools() {
        let p = |v| {
            let mut o = Vec::new();
            pack_size(&mut o, v);
            o
        };
        assert_eq!(p(0), [0x00]);
        assert_eq!(p(1), [0x01]);
        assert_eq!(p(-1), [0x41]);
        assert_eq!(p(63), [0x3f]);
        assert_eq!(p(64), [0x80, 0x01]);
        assert_eq!(p(-64), [0xc0, 0x01]);
        assert_eq!(p(49452), [0xac, 0x84, 0x06]);
    }
}
