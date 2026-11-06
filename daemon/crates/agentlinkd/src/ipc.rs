//! Local IPC endpoint for `agentlink-hook` and the `agentlink` CLI: newline-delimited JSON
//! over a Unix domain socket (`~/.agentlink/hook.sock`).

use anyhow::Result;
use std::path::Path;

#[cfg(unix)]
mod imp {
    use super::*;
    use anyhow::{bail, Context};
    use tokio::net::{UnixListener, UnixStream};

    pub type IpcStream = UnixStream;
    pub struct IpcListener(UnixListener);

    pub fn bind(path: &Path) -> Result<IpcListener> {
        use std::os::unix::fs::PermissionsExt;
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
            std::fs::set_permissions(dir, std::fs::Permissions::from_mode(0o700))?;
        }
        if path.exists() {
            if std::os::unix::net::UnixStream::connect(path).is_ok() {
                bail!(
                    "another agentlinkd is already listening on {}",
                    path.display()
                );
            }
            std::fs::remove_file(path).context("removing stale socket")?;
        }
        let l = UnixListener::bind(path).with_context(|| format!("binding {}", path.display()))?;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o600))?;
        Ok(IpcListener(l))
    }

    impl IpcListener {
        pub async fn accept(&self) -> Result<IpcStream> {
            Ok(self.0.accept().await?.0)
        }
    }
}

// TODO(windows): named pipe `\\.\pipe\agentlink-hook` via tokio::net::windows::named_pipe.
#[cfg(not(unix))]
mod imp {
    use super::*;

    pub type IpcStream = tokio::io::DuplexStream;
    pub struct IpcListener;

    pub fn bind(_path: &Path) -> Result<IpcListener> {
        anyhow::bail!("hook IPC over a named pipe is not implemented on this platform yet")
    }

    impl IpcListener {
        pub async fn accept(&self) -> Result<IpcStream> {
            std::future::pending().await
        }
    }
}

pub use imp::{bind, IpcStream};
