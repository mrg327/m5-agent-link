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

pub fn trace_dir() -> PathBuf {
    home().join("trace")
}

pub fn audit_log() -> PathBuf {
    home().join("audit.jsonl")
}
