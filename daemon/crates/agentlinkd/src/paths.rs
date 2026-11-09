//! Filesystem locations. `AGENTLINK_HOME` overrides `~/.agentlink` (tests, smoke runs).

use std::path::PathBuf;

pub fn home() -> PathBuf {
    if let Some(h) = std::env::var_os("AGENTLINK_HOME") {
        return PathBuf::from(h);
    }
    let user = std::env::var_os("HOME").or_else(|| std::env::var_os("USERPROFILE"));
    PathBuf::from(user.unwrap_or_default()).join(".agentlink")
}

pub fn hook_socket() -> PathBuf {
    home().join("hook.sock")
}

pub fn remote_token() -> PathBuf {
    home().join("remote.token")
}

pub fn remote_socket() -> PathBuf {
    home().join("remote.sock")
}

pub fn trace_dir() -> PathBuf {
    home().join("trace")
}

pub fn audit_log() -> PathBuf {
    home().join("audit.jsonl")
}

/// Per-user named pipe for hook IPC on Windows. `AGENTLINK_PIPE` overrides it; otherwise
/// the name is derived from the socket path so `AGENTLINK_HOME` gets its own pipe.
/// Must match `pipe_name` in agentlink-hook.
#[cfg_attr(not(windows), allow(dead_code))]
pub fn hook_pipe_name(socket: &std::path::Path) -> String {
    if let Some(n) = std::env::var_os("AGENTLINK_PIPE") {
        return n.to_string_lossy().into_owned();
    }
    let user = std::env::var("USERNAME").unwrap_or_default();
    let user: String = user
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
        .collect();
    let key = socket.to_string_lossy().to_lowercase();
    let hash = key.bytes().fold(0xcbf2_9ce4_8422_2325u64, |h, b| {
        (h ^ u64::from(b)).wrapping_mul(0x100_0000_01b3)
    });
    format!(r"\\.\pipe\agentlink-{user}-{hash:016x}")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pipe_name_is_stable_and_case_insensitive() {
        let a = hook_pipe_name(std::path::Path::new("C:\\Users\\a\\.agentlink\\hook.sock"));
        assert!(a.starts_with(r"\\.\pipe\agentlink-"));
        let b = hook_pipe_name(std::path::Path::new("c:\\users\\a\\.agentlink\\hook.sock"));
        assert_eq!(a, b);
    }
}
