//! Key geometry, the packed wire forms and stroke points.

use std::fmt;

/// One key. `cp` is a Unicode code point, or a control code for non-text keys
/// (0x0E layer switch, 0x20 space, 0x08 backspace). Coordinates are device pixels,
/// `x`,`y` the top-left corner.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Key {
    pub cp: u32,
    pub x: i16,
    pub y: i16,
    pub w: i16,
    pub h: i16,
}

impl Key {
    pub fn centre(&self) -> [f32; 2] {
        [
            f32::from(self.x) + f32::from(self.w) / 2.0,
            f32::from(self.y) + f32::from(self.h) / 2.0,
        ]
    }

    /// The character typed by this key, if it takes part in word paths
    /// (printable and not a space).
    pub fn glyph(&self) -> Option<char> {
        let c = char::from_u32(self.cp)?;
        if c.is_control() || c.is_whitespace() {
            None
        } else {
            Some(c.to_ascii_lowercase())
        }
    }

    pub fn contains(&self, x: i16, y: i16) -> bool {
        let (x, y) = (i32::from(x), i32::from(y));
        x >= i32::from(self.x)
            && x < i32::from(self.x) + i32::from(self.w)
            && y >= i32::from(self.y)
            && y < i32::from(self.y) + i32::from(self.h)
    }
}

/// One touch sample: position in device pixels and time since the touch-down.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct Point {
    pub x: i16,
    pub y: i16,
    pub t_ms: u16,
}

/// A malformed packed buffer.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    pub len: usize,
    pub unit: usize,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "packed buffer of {} bytes is not a multiple of {}",
            self.len, self.unit
        )
    }
}

impl std::error::Error for Error {}

fn i16_at(b: &[u8], i: usize) -> i16 {
    i16::from_le_bytes([b[i], b[i + 1]])
}

fn u16_at(b: &[u8], i: usize) -> u16 {
    u16::from_le_bytes([b[i], b[i + 1]])
}

/// Decode stroke points: 6 bytes each, little-endian `i16 x, i16 y, u16 t_ms`.
pub fn unpack_points(bytes: &[u8]) -> Result<Vec<Point>, Error> {
    let (chunks, rest) = bytes.as_chunks::<6>();
    if !rest.is_empty() {
        return Err(Error {
            len: bytes.len(),
            unit: 6,
        });
    }
    Ok(chunks
        .iter()
        .map(|c| Point {
            x: i16_at(c, 0),
            y: i16_at(c, 2),
            t_ms: u16_at(c, 4),
        })
        .collect())
}

/// Inverse of [`unpack_points`].
pub fn pack_points(pts: &[Point]) -> Vec<u8> {
    let mut out = Vec::with_capacity(pts.len() * 6);
    for p in pts {
        out.extend_from_slice(&p.x.to_le_bytes());
        out.extend_from_slice(&p.y.to_le_bytes());
        out.extend_from_slice(&p.t_ms.to_le_bytes());
    }
    out
}

/// Turn per-point deltas (the `dt` of the wire form) into times since touch-down, in place.
pub fn integrate_dt(pts: &mut [Point]) {
    let mut t = 0u16;
    for p in pts {
        t = t.saturating_add(p.t_ms);
        p.t_ms = t;
    }
}

/// A set of keys.
#[derive(Clone, Debug)]
pub struct Layout {
    keys: Vec<Key>,
}

impl Layout {
    pub fn new(keys: Vec<Key>) -> Self {
        Layout { keys }
    }

    /// Packed form: 10 bytes per key, little-endian `u16 cp, i16 x, i16 y, u16 w, u16 h`.
    pub fn from_packed(bytes: &[u8]) -> Result<Self, Error> {
        let (chunks, rest) = bytes.as_chunks::<10>();
        if !rest.is_empty() {
            return Err(Error {
                len: bytes.len(),
                unit: 10,
            });
        }
        let keys = chunks
            .iter()
            .map(|c| Key {
                cp: u32::from(u16_at(c, 0)),
                x: i16_at(c, 2),
                y: i16_at(c, 4),
                w: u16_at(c, 6).min(i16::MAX as u16) as i16,
                h: u16_at(c, 8).min(i16::MAX as u16) as i16,
            })
            .collect();
        Ok(Layout { keys })
    }

    pub fn to_packed(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(self.keys.len() * 10);
        for k in &self.keys {
            out.extend_from_slice(&(k.cp as u16).to_le_bytes());
            out.extend_from_slice(&k.x.to_le_bytes());
            out.extend_from_slice(&k.y.to_le_bytes());
            out.extend_from_slice(&(k.w as u16).to_le_bytes());
            out.extend_from_slice(&(k.h as u16).to_le_bytes());
        }
        out
    }

    /// The 466x466 watch QWERTY layer (10/9/7 letters, then layer, space, backspace).
    pub fn stopwatch_qwerty() -> Self {
        let mut keys = Vec::new();
        let rows: [(&str, i16, i16); 3] = [
            ("qwertyuiop", 13, 170),
            ("asdfghjkl", 35, 224),
            ("zxcvbnm", 79, 278),
        ];
        for (letters, x0, y) in rows {
            for (i, c) in letters.chars().enumerate() {
                keys.push(Key {
                    cp: c as u32,
                    x: x0 + 44 * i as i16,
                    y,
                    w: 44,
                    h: 54,
                });
            }
        }
        for (cp, x, w) in [(0x0E, 79, 66), (0x20, 145, 176), (0x08, 321, 66)] {
            keys.push(Key {
                cp,
                x,
                y: 332,
                w,
                h: 54,
            });
        }
        Layout { keys }
    }

    pub fn keys(&self) -> &[Key] {
        &self.keys
    }

    pub fn key_at(&self, x: i16, y: i16) -> Option<&Key> {
        self.keys.iter().find(|k| k.contains(x, y))
    }

    /// Median width of the keys that take part in word paths (a length scale for the decoder).
    pub(crate) fn key_width(&self) -> f32 {
        let mut w: Vec<i16> = self
            .keys
            .iter()
            .filter(|k| k.glyph().is_some())
            .map(|k| k.w)
            .collect();
        if w.is_empty() {
            return 40.0;
        }
        w.sort_unstable();
        f32::from(w[w.len() / 2]).max(1.0)
    }
}

/// Bounding extent (larger of width and height) in pixels.
pub fn extent(pts: &[Point]) -> i32 {
    if pts.is_empty() {
        return 0;
    }
    let (mut x0, mut x1, mut y0, mut y1) = (i32::MAX, i32::MIN, i32::MAX, i32::MIN);
    for p in pts {
        x0 = x0.min(i32::from(p.x));
        x1 = x1.max(i32::from(p.x));
        y0 = y0.min(i32::from(p.y));
        y1 = y1.max(i32::from(p.y));
    }
    (x1 - x0).max(y1 - y0)
}
