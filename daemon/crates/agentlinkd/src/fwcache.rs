//! The firmware images this machine has (`~/.agentlink/fw/<sha256>.bin`): every image the daemon sent to a
//! watch, and what the firmware build drops there. A delta update (ota.rs, PROTOCOL §6c) needs the
//! image the watch runs as its base; it is found by the app version the watch reports in its hello.

use sha2::{Digest, Sha256};
use std::io::Read;
use std::path::PathBuf;
use std::time::SystemTime;

/// Images kept, newest first; older ones are deleted when one is added.
pub const KEEP: usize = 8;

pub struct FwCache {
    dir: PathBuf,
}

/// The app version in an ESP-IDF image: `esp_app_desc_t` follows the image header (24 bytes) and the
/// first segment header (8); its magic word is at 32 and `version[32]` at 48.
pub fn app_version(head: &[u8]) -> Option<String> {
    if head.len() < 80 || head[0] != 0xE9 || head[32..36] != [0x32, 0x54, 0xCD, 0xAB] {
        return None;
    }
    let v = &head[48..80];
    let end = v.iter().position(|&c| c == 0).unwrap_or(v.len());
    Some(String::from_utf8_lossy(&v[..end]).into_owned())
}

/// A cached image that may be the one the watch runs.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Candidate {
    pub path: PathBuf,
    pub version: String,
}

impl FwCache {
    pub fn new(dir: PathBuf) -> Self {
        FwCache { dir }
    }

    fn create_dir(&self) -> std::io::Result<()> {
        let mut b = std::fs::DirBuilder::new();
        b.recursive(true);
        #[cfg(unix)]
        std::os::unix::fs::DirBuilderExt::mode(&mut b, 0o700);
        b.create(&self.dir)
    }

    /// `*.bin` with their mtimes, newest first.
    fn images(&self) -> Vec<(PathBuf, SystemTime)> {
        let Ok(rd) = std::fs::read_dir(&self.dir) else {
            return Vec::new();
        };
        let mut v: Vec<_> = rd
            .flatten()
            .filter(|e| {
                let n = e.file_name();
                let n = n.to_string_lossy();
                n.ends_with(".bin") && !n.starts_with('.')
            })
            .filter_map(|e| {
                let m = e.metadata().ok()?;
                m.is_file()
                    .then(|| (e.path(), m.modified().unwrap_or(SystemTime::UNIX_EPOCH)))
            })
            .collect();
        v.sort_by(|a, b| b.1.cmp(&a.1).then_with(|| a.0.cmp(&b.0)));
        v
    }

    /// Keeps `data` as `<sha>.bin` (written aside and renamed, so a reader never sees half an image),
    /// or marks it as just used when it is there already; then drops all but the newest `KEEP`.
    pub fn store(&self, data: &[u8], sha: &[u8; 32]) -> std::io::Result<()> {
        self.create_dir()?;
        let path = self.dir.join(format!("{}.bin", hex::encode(sha)));
        if std::fs::metadata(&path).is_ok_and(|m| m.len() == data.len() as u64) {
            std::fs::File::options()
                .write(true)
                .open(&path)?
                .set_modified(SystemTime::now())?;
        } else {
            let tmp = self
                .dir
                .join(format!(".{}.tmp{}", hex::encode(sha), std::process::id()));
            std::fs::write(&tmp, data)?;
            if let Err(e) = std::fs::rename(&tmp, &path) {
                let _ = std::fs::remove_file(&tmp);
                return Err(e);
            }
        }
        for (old, _) in self.images().into_iter().skip(KEEP) {
            let _ = std::fs::remove_file(old);
        }
        Ok(())
    }

    /// Images whose app version is `version`, newest first.
    pub fn with_version(&self, version: &str) -> Vec<Candidate> {
        let version = version.trim();
        self.images()
            .into_iter()
            .filter_map(|(path, _)| {
                let mut head = [0u8; 80];
                std::fs::File::open(&path)
                    .and_then(|mut f| f.read_exact(&mut head))
                    .ok()?;
                let v = app_version(&head)?;
                (!version.is_empty() && v.trim() == version)
                    .then_some(Candidate { path, version: v })
            })
            .collect()
    }
}

/// A patch ready to send: built from a cached base and checked by applying it.
#[derive(Debug, Clone)]
pub struct Delta {
    pub patch: Vec<u8>,
    pub base_sha: [u8; 32],
    pub base_len: u64,
    pub base_version: String,
}

/// The patch from the newest cached image with the watch's version to `image`, if there is one and it
/// is worth it (under three quarters of the image). Slow (bsdiff): call off the async runtime.
pub fn delta_for(cache: &FwCache, watch_fw: &str, image: &[u8], sha: &[u8; 32]) -> Option<Delta> {
    let base_path = cache
        .with_version(watch_fw)
        .into_iter()
        .find(|c| c.path.file_stem().and_then(|s| s.to_str()) != Some(hex::encode(sha).as_str()))?;
    let base = std::fs::read(&base_path.path).ok()?;
    let base_sha: [u8; 32] = Sha256::digest(&base).into();
    if &base_sha == sha {
        return None;
    }
    let patch = agentlink_delta::create_patch(&base, image);
    if patch.len() >= image.len() / 4 * 3 {
        return None;
    }
    if agentlink_delta::apply_patch(&base, &patch).ok().as_deref() != Some(image) {
        tracing::warn!(base = %base_path.path.display(), "delta patch does not rebuild the image");
        return None;
    }
    Some(Delta {
        patch,
        base_sha,
        base_len: base.len() as u64,
        base_version: base_path.version,
    })
}

/// A scratch directory under the build's target directory, for tests.
#[cfg(test)]
pub fn scratch(name: &str) -> PathBuf {
    let d = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../target/test-scratch")
        .join(format!("{name}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// An app image whose bytes depend on `seed`, with `ver` in its app description.
    pub fn image(ver: &str, seed: u32, len: usize) -> Vec<u8> {
        let mut x = seed;
        let mut d: Vec<u8> = (0..len)
            .map(|i| {
                x = x.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
                if i % 7 == 0 {
                    (x >> 24) as u8
                } else {
                    (i / 64) as u8
                }
            })
            .collect();
        d[0] = 0xE9;
        d[32..36].copy_from_slice(&[0x32, 0x54, 0xCD, 0xAB]);
        d[48..80].fill(0);
        d[48..48 + ver.len()].copy_from_slice(ver.as_bytes());
        d[80..112].fill(0);
        d[80..89].copy_from_slice(b"agentlink");
        d
    }

    fn sha(d: &[u8]) -> [u8; 32] {
        Sha256::digest(d).into()
    }

    #[test]
    fn keeps_the_newest_images_and_finds_them_by_version() {
        let dir = scratch("fwcache");
        let c = FwCache::new(dir.clone());
        let imgs: Vec<_> = (0..KEEP as u32 + 2)
            .map(|i| image(&format!("v{i}"), i, 8192))
            .collect();
        for (i, d) in imgs.iter().enumerate() {
            c.store(d, &sha(d)).unwrap();
            // mtimes one second apart, oldest first
            let t = SystemTime::UNIX_EPOCH + std::time::Duration::from_secs(1_000_000 + i as u64);
            let p = dir.join(format!("{}.bin", hex::encode(sha(d))));
            std::fs::File::options()
                .write(true)
                .open(p)
                .unwrap()
                .set_modified(t)
                .unwrap();
        }
        c.store(&imgs[0], &sha(&imgs[0])).unwrap(); // re-sent: now the newest again
        assert_eq!(c.images().len(), KEEP);
        assert_eq!(c.with_version("v0").len(), 1);
        assert!(c.with_version("v1").is_empty());
        assert!(c.with_version("v2").is_empty());
        assert_eq!(c.with_version("v9")[0].version, "v9");
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let _ = std::fs::remove_dir_all(&dir);
            c.store(&imgs[1], &sha(&imgs[1])).unwrap();
            let mode = std::fs::metadata(&dir).unwrap().permissions().mode();
            assert_eq!(mode & 0o777, 0o700);
        }
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_delta_needs_a_base_with_the_watchs_version_and_must_pay_off() {
        let dir = scratch("fwdelta");
        let c = FwCache::new(dir.clone());
        let old = image("old", 1, 64 * 1024);
        let mut new = old.clone();
        new[48..51].copy_from_slice(b"new");
        new[30000..30100].fill(0x55);
        c.store(&old, &sha(&old)).unwrap();
        c.store(&new, &sha(&new)).unwrap(); // the new image itself is never its own base

        let d = delta_for(&c, "old", &new, &sha(&new)).unwrap();
        assert_eq!(d.base_sha, sha(&old));
        assert_eq!(d.base_len, old.len() as u64);
        assert_eq!(d.base_version, "old");
        assert_eq!(agentlink_delta::apply_patch(&old, &d.patch).unwrap(), new);
        assert!(delta_for(&c, "other", &new, &sha(&new)).is_none());
        assert!(delta_for(&c, "new", &new, &sha(&new)).is_none());

        // an unrelated image: the patch would not be smaller enough
        let other = image("x", 99, 64 * 1024)
            .iter()
            .enumerate()
            .map(|(i, b)| b ^ (i as u8).wrapping_mul(37))
            .collect::<Vec<_>>();
        assert!(delta_for(&c, "old", &other, &sha(&other)).is_none());
        let _ = std::fs::remove_dir_all(&dir);
    }
}
