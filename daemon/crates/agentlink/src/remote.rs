//! `agentlink remote init`: creates the shared secret that authenticates remote hooks.

use anyhow::Result;
use std::path::Path;

#[cfg(unix)]
pub fn init(json: bool, rotate: bool) -> Result<()> {
    let (path, rotated) = write_token(&crate::home(), rotate)?;
    if json {
        println!(
            "{}",
            serde_json::json!({"ok": true, "path": path, "rotated": rotated})
        );
        return Ok(());
    }
    let p = path.display();
    println!(
        "{} {p}",
        if rotated {
            "rotated remote token:"
        } else {
            "created remote token:"
        }
    );
    if rotated {
        println!("existing tunnels and remotes need the new token (repeat step 1 and 2).");
    }
    println!(
        "next steps for a Windows remote (<win-host> = its ssh name):
  1. ssh <win-host> mkdir .agentlink   (once)
     scp {p} <win-host>:.agentlink/remote.token
  2. ssh <win-host> icacls .agentlink\\remote.token /inheritance:r /grant:r \"%USERNAME%:R\"
  3. on Windows: setx AGENTLINK_REMOTE tcp:127.0.0.1:47533
  4. agentlink ssh <win-host>"
    );
    Ok(())
}

#[cfg(not(unix))]
pub fn init(_json: bool, _rotate: bool) -> Result<()> {
    anyhow::bail!("run this on the Mac that runs agentlinkd")
}

/// Writes `<home>/remote.token` (64 hex chars, dir 0700, file 0600). Returns the path and
/// whether an existing token was replaced.
#[cfg(unix)]
fn write_token(home: &Path, rotate: bool) -> Result<(std::path::PathBuf, bool)> {
    use anyhow::Context;
    use std::io::{Read, Write};
    use std::os::unix::fs::{DirBuilderExt, OpenOptionsExt};

    let path = home.join("remote.token");
    let existed = path.exists();
    if existed && !rotate {
        anyhow::bail!(
            "{} already exists; use --rotate to replace it",
            path.display()
        );
    }
    std::fs::DirBuilder::new()
        .recursive(true)
        .mode(0o700)
        .create(home)
        .with_context(|| format!("cannot create {}", home.display()))?;
    let mut bytes = [0u8; 32];
    std::fs::File::open("/dev/urandom")?.read_exact(&mut bytes)?;
    if existed {
        // a fresh file, so the mode applies even if the old one was loosened
        std::fs::remove_file(&path)?;
    }
    let mut f = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .open(&path)
        .with_context(|| format!("cannot write {}", path.display()))?;
    writeln!(f, "{}", hex::encode(bytes))?;
    Ok((path, existed))
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;
    use std::os::unix::fs::PermissionsExt;

    #[test]
    fn token_is_64_hex_0600_and_needs_rotate_to_replace() {
        let home = std::env::temp_dir().join(format!("agentlink-remote-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&home);
        let (path, rotated) = write_token(&home, false).unwrap();
        assert!(!rotated);
        let t = std::fs::read_to_string(&path).unwrap();
        assert_eq!(t.len(), 65);
        assert!(t
            .trim()
            .bytes()
            .all(|b| b.is_ascii_hexdigit() && !b.is_ascii_uppercase()));
        assert_eq!(
            std::fs::metadata(&path).unwrap().permissions().mode() & 0o777,
            0o600
        );
        assert_eq!(
            std::fs::metadata(&home).unwrap().permissions().mode() & 0o777,
            0o700
        );

        assert!(write_token(&home, false).is_err());
        assert_eq!(std::fs::read_to_string(&path).unwrap(), t);
        let (_, rotated) = write_token(&home, true).unwrap();
        assert!(rotated);
        assert_ne!(std::fs::read_to_string(&path).unwrap(), t);
        let _ = std::fs::remove_dir_all(&home);
    }
}
