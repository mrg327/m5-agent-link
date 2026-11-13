//! Delta firmware updates (PROTOCOL §6c, issue ef2c): a detools *sequential* patch made with the bsdiff
//! algorithm and compressed with heatshrink, the format the watch's vendored detools decoder applies.
//!
//! Layout: one header byte (patch type 0 = sequential in bits 4-6, compression 4 = heatshrink in bits
//! 0-3), the new image's size (detools size encoding), then the heatshrink stream. That stream starts
//! with one byte `(window_sz2 - 4) << 4 | (lookahead_sz2 - 3)` and decompresses to: the data-format
//! patch size (always 0 here), then steps of diff size, diff bytes (new - old), extra size, extra
//! bytes and the signed seek of the old-image pointer, until the new image is complete.
//!
//! The patches are byte-identical to `detools.create_patch(..., compression='heatshrink',
//! heatshrink_window_sz2=11, heatshrink_lookahead_sz2=10)` (detools 0.53.1).

mod bsdiff;
mod heatshrink;

/// The heatshrink parameters this side produces (the watch's decoder is built for them).
pub const WINDOW_SZ2: u8 = 11;
pub const LOOKAHEAD_SZ2: u8 = 10;

const PATCH_TYPE_SEQUENTIAL: u8 = 0;
const COMPRESSION_HEATSHRINK: u8 = 4;

/// Why a patch could not be applied.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error(pub &'static str);

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.0)
    }
}

impl std::error::Error for Error {}

/// Makes the patch that turns `base` into `new`. Deterministic.
pub fn create_patch(base: &[u8], new: &[u8]) -> Vec<u8> {
    let mut out = vec![PATCH_TYPE_SEQUENTIAL << 4 | COMPRESSION_HEATSHRINK];
    bsdiff::pack_size(&mut out, new.len() as i64);
    if new.is_empty() {
        return out;
    }
    let mut steps = Vec::with_capacity(new.len() + 1024);
    bsdiff::pack_size(&mut steps, 0); // no data-format patch
    bsdiff::diff(&mut steps, base, new);
    out.push((WINDOW_SZ2 - 4) << 4 | (LOOKAHEAD_SZ2 - 3));
    out.extend(heatshrink::compress(&steps, WINDOW_SZ2, LOOKAHEAD_SZ2));
    out
}

struct Reader<'a> {
    data: &'a [u8],
    at: usize,
}

impl<'a> Reader<'a> {
    fn byte(&mut self) -> Result<u8, Error> {
        let b = *self.data.get(self.at).ok_or(Error("patch ends early"))?;
        self.at += 1;
        Ok(b)
    }

    fn take(&mut self, n: usize) -> Result<&'a [u8], Error> {
        let end = self.at.checked_add(n).ok_or(Error("patch ends early"))?;
        let s = self
            .data
            .get(self.at..end)
            .ok_or(Error("patch ends early"))?;
        self.at = end;
        Ok(s)
    }

    fn size(&mut self) -> Result<i64, Error> {
        let mut b = self.byte()?;
        let negative = b & 0x40 != 0;
        let mut v = i64::from(b & 0x3f);
        let mut shift = 6;
        while b & 0x80 != 0 {
            b = self.byte()?;
            if shift > 56 {
                return Err(Error("size too large"));
            }
            v |= i64::from(b & 0x7f) << shift;
            shift += 7;
        }
        Ok(if negative { -v } else { v })
    }
}

/// Applies a sequential heatshrink patch (any window/lookahead the header names) to `base`.
pub fn apply_patch(base: &[u8], patch: &[u8]) -> Result<Vec<u8>, Error> {
    let mut r = Reader { data: patch, at: 0 };
    let header = r.byte()?;
    if (header >> 4) & 7 != PATCH_TYPE_SEQUENTIAL {
        return Err(Error("not a sequential patch"));
    }
    if header & 0xf != COMPRESSION_HEATSHRINK {
        return Err(Error("not heatshrink compressed"));
    }
    let to_size = usize::try_from(r.size()?).map_err(|_| Error("negative image size"))?;
    if to_size == 0 {
        return Ok(Vec::new());
    }
    let params = r.byte()?;
    let (w2, l2) = ((params >> 4) + 4, (params & 0xf) + 3);
    if w2 > 15 || l2 >= w2 {
        return Err(Error("bad heatshrink parameters"));
    }
    let steps = heatshrink::decompress(&patch[r.at..], w2, l2);
    let mut s = Reader {
        data: &steps,
        at: 0,
    };
    if s.size()? != 0 {
        return Err(Error("data-format patches are not supported"));
    }
    let mut out = Vec::with_capacity(to_size.min(64 << 20));
    let mut from = 0i64;
    let chunk = |s: &mut Reader, have: usize| -> Result<usize, Error> {
        let n = usize::try_from(s.size()?).map_err(|_| Error("negative length"))?;
        if have + n > to_size {
            return Err(Error("patch data longer than the image"));
        }
        Ok(n)
    };
    while out.len() < to_size {
        let n = chunk(&mut s, out.len())?;
        let diff = s.take(n)?;
        let old = usize::try_from(from)
            .ok()
            .and_then(|f| base.get(f..f.checked_add(n)?))
            .ok_or(Error("patch reads outside the base image"))?;
        out.extend(diff.iter().zip(old).map(|(d, o)| d.wrapping_add(*o)));
        from += n as i64;
        let n = chunk(&mut s, out.len())?;
        out.extend_from_slice(s.take(n)?);
        from = from
            .checked_add(s.size()?)
            .ok_or(Error("seek out of range"))?;
    }
    if s.at != steps.len() {
        return Err(Error("data after the end of the patch"));
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn fixture(name: &str) -> Vec<u8> {
        let p = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../protocol/fixtures/ota");
        std::fs::read(p.join(name)).unwrap()
    }

    #[test]
    fn applies_the_detools_reference_patch() {
        let (base, new) = (fixture("base.bin"), fixture("new.bin"));
        assert_eq!(apply_patch(&base, &fixture("python.patch")).unwrap(), new);
    }

    #[test]
    fn makes_the_committed_patch_which_round_trips() {
        let (base, new) = (fixture("base.bin"), fixture("new.bin"));
        let p = create_patch(&base, &new);
        // regenerate with AGENTLINK_DELTA_REGEN=1 after a deliberate change
        if std::env::var_os("AGENTLINK_DELTA_REGEN").is_some() {
            let dir =
                PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../protocol/fixtures/ota");
            std::fs::write(dir.join("rust.patch"), &p).unwrap();
        }
        assert_eq!(p, fixture("rust.patch"));
        assert_eq!(apply_patch(&base, &p).unwrap(), new);
    }

    fn noise(n: usize, seed: u32) -> Vec<u8> {
        let mut x = seed;
        (0..n)
            .map(|_| {
                x = x.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
                (x >> 24) as u8
            })
            .collect()
    }

    #[test]
    fn edge_cases_round_trip() {
        let base = noise(30000, 1);
        let mut longer = base.clone();
        longer.splice(5000..5000, noise(700, 2));
        longer.extend(noise(3000, 3));
        let mut shorter = base[..20000].to_vec();
        shorter.drain(100..900);
        let mut same_tail = base.clone();
        same_tail[10] ^= 0xff; // a diff and no extra at the end
        let cases: [(&[u8], &[u8]); 9] = [
            (&base, &base),
            (&base, &longer),
            (&base, &shorter),
            (&base, &same_tail),
            (&base, &[]),
            (&[], &base),
            (&[], &[]),
            (&[7], &[7, 7, 7]),
            (&base, &noise(1000, 9)),
        ];
        for (i, (a, b)) in cases.iter().enumerate() {
            let p = create_patch(a, b);
            assert_eq!(apply_patch(a, &p).as_deref(), Ok(*b), "case {i}");
        }
        assert!(create_patch(&base, &base).len() < 100);
        assert_eq!(create_patch(&base, &[]), [0x04, 0x00]);
    }

    #[test]
    fn refuses_a_patch_for_another_base_or_cut_short() {
        let (base, new) = (fixture("base.bin"), fixture("new.bin"));
        let p = create_patch(&base, &new);
        assert!(apply_patch(&base, &p[..p.len() / 2]).is_err());
        assert!(apply_patch(&base[..1000], &p).is_err());
        let mut bad = p.clone();
        bad[0] = 0x14;
        assert!(apply_patch(&base, &bad).is_err());
    }
}
