//! Local IPC endpoint for `agentlink-hook` and the `agentlink` CLI: newline-delimited JSON
//! over a Unix domain socket (`~/.agentlink/hook.sock`) or, on Windows, a named pipe.

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

/// Named pipe `\\.\pipe\agentlink-<user>-<hash>` (see `paths::hook_pipe_name`). The pipe
/// keeps the default DACL (creator/owner, SYSTEM and Administrators; not Everyone) and
/// rejects remote clients, so it is reachable only from this machine by the same user
/// or admins.
#[cfg(windows)]
mod imp {
    use super::*;
    use anyhow::Context;
    use tokio::net::windows::named_pipe::{NamedPipeServer, ServerOptions};
    use tokio::sync::Mutex;

    pub type IpcStream = NamedPipeServer;
    pub struct IpcListener {
        name: String,
        next: Mutex<NamedPipeServer>,
    }

    fn create(name: &str, first: bool) -> std::io::Result<NamedPipeServer> {
        ServerOptions::new()
            .first_pipe_instance(first)
            .reject_remote_clients(true)
            .create(name)
    }

    pub fn bind(path: &Path) -> Result<IpcListener> {
        let name = crate::paths::hook_pipe_name(path);
        let first = create(&name, true).with_context(|| {
            format!("creating pipe {name} (is another agentlinkd already running?)")
        })?;
        Ok(IpcListener {
            name,
            next: Mutex::new(first),
        })
    }

    impl IpcListener {
        pub async fn accept(&self) -> Result<IpcStream> {
            let mut next = self.next.lock().await;
            next.connect().await?;
            // Open the next instance before handing over this one so clients never miss it.
            let fresh = create(&self.name, false)?;
            Ok(std::mem::replace(&mut *next, fresh))
        }
    }
}

#[cfg(not(any(unix, windows)))]
mod imp {
    use super::*;

    pub type IpcStream = tokio::io::DuplexStream;
    pub struct IpcListener;

    pub fn bind(_path: &Path) -> Result<IpcListener> {
        anyhow::bail!("hook IPC is not implemented on this platform")
    }

    impl IpcListener {
        pub async fn accept(&self) -> Result<IpcStream> {
            std::future::pending().await
        }
    }
}

pub use imp::{bind, IpcStream};
