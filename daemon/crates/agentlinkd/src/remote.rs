//! Remote sessions (docs/design/REMOTE.md): a hook on another machine reaches this daemon
//! through an `ssh -R` tunnel ending on `remote.sock` or on a per-window attach socket.
//! Every such connection starts with an auth line, then speaks the normal hook protocol.

use crate::hook_source::ConnEnv;
use crate::registry::Via;
use serde_json::{json, Value};
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;
use tokio::io::{AsyncBufRead, Lines};

const AUTH_TIMEOUT: Duration = Duration::from_secs(10);

/// The token, trimmed; `None` when the file is missing, unreadable or empty.
pub fn read_token(path: &Path) -> Option<String> {
    let t = std::fs::read_to_string(path).ok()?;
    let t = t.trim();
    (!t.is_empty()).then(|| t.to_owned())
}

/// Compares without stopping at the first differing byte.
pub fn ct_eq(a: &[u8], b: &[u8]) -> bool {
    let mut diff = a.len() ^ b.len();
    for i in 0..a.len().max(b.len()) {
        diff |= usize::from(a.get(i).copied().unwrap_or(0) ^ b.get(i).copied().unwrap_or(0));
    }
    diff == 0
}

/// Keeps `[A-Za-z0-9._-]`, at most 32 chars; `remote` when nothing is left.
pub fn sanitize_host(s: &str) -> String {
    let h: String = s
        .chars()
        .filter(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-'))
        .take(32)
        .collect();
    if h.is_empty() {
        "remote".into()
    } else {
        h
    }
}

/// Checks an auth line against the token. The error carries the sanitized host for the audit.
fn check_auth(line: &str, token: Option<&str>) -> Result<(String, String), (String, &'static str)> {
    let v: Value = serde_json::from_str(line).map_err(|_| ("remote".to_owned(), "bad json"))?;
    let host = sanitize_host(v["host"].as_str().unwrap_or(""));
    let Some(token) = token else {
        return Err((host, "no token configured"));
    };
    let Some(given) = v["auth"].as_str() else {
        return Err((host, "missing auth"));
    };
    if !ct_eq(given.as_bytes(), token.as_bytes()) {
        return Err((host, "bad token"));
    }
    let os = v["os"]
        .as_str()
        .filter(|o| matches!(*o, "win" | "mac" | "linux"))
        .unwrap_or("other");
    Ok((host, os.to_owned()))
}

/// Reads and checks the first line of a remote connection (the token file is read afresh, so
/// a rotated token applies at once). `None` = rejected and audited; the caller drops the
/// connection.
pub async fn authenticate<R: AsyncBufRead + Unpin>(
    lines: &mut Lines<R>,
    env: &ConnEnv,
    attach: Option<u64>,
) -> Option<Via> {
    let first = tokio::time::timeout(AUTH_TIMEOUT, lines.next_line()).await;
    let outcome = match first {
        Ok(Ok(Some(line))) => check_auth(&line, read_token(&env.token_file).as_deref()),
        _ => Err(("remote".to_owned(), "no auth line")),
    };
    match outcome {
        Ok((host, os)) => {
            env.trace.audit(
                "remote_auth_ok",
                None,
                None,
                json!({"host": host, "os": os, "attach": attach}),
            );
            Some(Via::Remote { host, os, attach })
        }
        Err((host, reason)) => {
            env.trace.audit(
                "remote_auth_fail",
                None,
                None,
                json!({"host": host, "reason": reason, "attach": attach}),
            );
            None
        }
    }
}

#[cfg(unix)]
pub use unix::{open_attach, spawn_remote_socket};

#[cfg(unix)]
mod unix {
    use super::*;
    use crate::hook_source::{handle_conn, host_ctx, Peer};
    use crate::ipc;
    use crate::trace::Dir;
    use tokio::task::JoinHandle;

    /// An open attach: the socket exists and its sessions are routable while this lives.
    pub struct AttachGuard {
        env: Arc<ConnEnv>,
        id: u64,
        socket: std::path::PathBuf,
        accept: JoinHandle<()>,
    }

    impl Drop for AttachGuard {
        fn drop(&mut self) {
            self.accept.abort();
            let _ = std::fs::remove_file(&self.socket);
            self.env.registry.drop_attach(self.id);
            self.env
                .trace
                .audit("remote_attach_close", None, None, json!({"id": self.id}));
        }
    }

    /// Serves one listener; every connection is a `Peer::Remote`.
    fn spawn_accept(
        listener: ipc::IpcListener,
        env: Arc<ConnEnv>,
        attach: Option<u64>,
    ) -> JoinHandle<()> {
        tokio::spawn(async move {
            while let Ok(stream) = listener.accept().await {
                tokio::spawn(handle_conn(stream, env.clone(), Peer::Remote { attach }));
            }
        })
    }

    /// Binds `remote.sock` next to `hook.sock`; failing to (e.g. another daemon owns it) is
    /// logged, not fatal.
    pub fn spawn_remote_socket(path: &Path, env: Arc<ConnEnv>) {
        match ipc::bind(path) {
            Ok(l) => {
                tracing::info!("remote socket listening on {}", path.display());
                spawn_accept(l, env, None);
            }
            Err(e) => tracing::warn!("remote socket disabled: {e:#}"),
        }
    }

    /// `{"ctl":"remote_attach","ctx":{..}}` on the local socket: the reply, and the guard to
    /// hold for as long as the ctl connection stays open.
    pub fn open_attach(env: &Arc<ConnEnv>, req: &Value) -> (Value, Option<AttachGuard>) {
        let err = |m: &str| (json!({"ok": false, "error": m}), None);
        if read_token(&env.token_file).is_none() {
            return err("no remote token: run `agentlink remote init`");
        }
        let id = env.registry.new_attach_id();
        let socket = env.home.join(format!("remote-{id}.sock"));
        let listener = match ipc::bind(&socket) {
            Ok(l) => l,
            Err(e) => return err(&format!("{e:#}")),
        };
        let ctx = &req["ctx"];
        let cwd = ctx["cwd"].as_str().map(str::to_owned);
        env.registry
            .add_attach(id, socket.clone(), host_ctx(ctx, cwd));
        env.trace.record(
            "remote",
            Dir::Internal,
            "attach",
            None,
            None,
            json!({"id": id, "socket": socket}),
            None,
        );
        let accept = spawn_accept(listener, env.clone(), Some(id));
        let reply = json!({"ok": true, "id": id, "socket": socket});
        let guard = AttachGuard {
            env: env.clone(),
            id,
            socket,
            accept,
        };
        (reply, Some(guard))
    }
}

#[cfg(not(unix))]
pub use other::{open_attach, spawn_remote_socket};

#[cfg(not(unix))]
mod other {
    use super::*;

    pub struct AttachGuard;

    pub fn spawn_remote_socket(_: &Path, _: Arc<ConnEnv>) {
        tracing::warn!("remote sessions need a Unix daemon; remote socket disabled");
    }

    pub fn open_attach(_: &Arc<ConnEnv>, _: &Value) -> (Value, Option<AttachGuard>) {
        (
            json!({"ok": false, "error": "remote sessions need a Unix daemon"}),
            None,
        )
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;
    use crate::broker::Broker;
    use crate::hook_source::{handle_conn, Peer};
    use crate::registry::Registry;
    use crate::trace::Trace;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::{UnixListener, UnixStream};

    const TOKEN: &str = "ab12";

    fn scratch(name: &str) -> std::path::PathBuf {
        let d = std::env::temp_dir().join(format!("al-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn constant_time_compare_and_token_file() {
        assert!(ct_eq(b"abc", b"abc"));
        assert!(!ct_eq(b"abc", b"abd"));
        assert!(!ct_eq(b"abc", b"abcd"));
        assert!(!ct_eq(b"", b"a"));
        let d = scratch("tok");
        let f = d.join("remote.token");
        assert_eq!(read_token(&f), None);
        std::fs::write(&f, " \n").unwrap();
        assert_eq!(read_token(&f), None);
        std::fs::write(&f, format!("{TOKEN}\n")).unwrap();
        assert_eq!(read_token(&f).as_deref(), Some(TOKEN));
        let _ = std::fs::remove_dir_all(d);
    }

    #[test]
    fn host_names_are_sanitized() {
        assert_eq!(sanitize_host("WIN-01.corp_x"), "WIN-01.corp_x");
        assert_eq!(sanitize_host("a b/\"c\u{e9}\n"), "abc");
        assert_eq!(sanitize_host("  "), "remote");
        assert_eq!(sanitize_host(&"x".repeat(50)).len(), 32);
    }

    #[test]
    fn auth_lines_are_checked() {
        let ok = format!(r#"{{"auth":"{TOKEN}","host":"W 1","os":"win"}}"#);
        assert_eq!(
            check_auth(&ok, Some(TOKEN)),
            Ok(("W1".into(), "win".into()))
        );
        assert_eq!(check_auth(&ok, None).unwrap_err().1, "no token configured");
        assert_eq!(
            check_auth(r#"{"host":"h"}"#, Some(TOKEN)).unwrap_err().1,
            "missing auth"
        );
        assert_eq!(
            check_auth(r#"{"auth":"zz"}"#, Some(TOKEN)).unwrap_err().1,
            "bad token"
        );
    }

    fn env(d: &Path) -> Arc<ConnEnv> {
        let trace = Trace::null();
        let registry = Arc::new(Registry::new(trace.clone()));
        Arc::new(ConnEnv {
            broker: Arc::new(Broker::new(registry.clone())),
            registry,
            trace,
            status: Arc::new(|| json!({})),
            token_file: d.join("remote.token"),
            home: d.to_path_buf(),
        })
    }

    const HOOK: &str = r#"{"agent":"copilot","event":"UserPromptSubmitted","cwd":"C:\\w\\proj","branch":"main","payload":{"sessionId":"s1"}}"#;

    async fn eventually(f: impl Fn() -> bool) -> bool {
        for _ in 0..100 {
            if f() {
                return true;
            }
            tokio::time::sleep(Duration::from_millis(20)).await;
        }
        false
    }

    #[tokio::test]
    async fn remote_socket_rejects_bad_auth_and_accepts_good() {
        let d = scratch("e2e");
        std::fs::write(d.join("remote.token"), TOKEN).unwrap();
        let env = env(&d);
        let sock = d.join("remote.sock");
        let l = UnixListener::bind(&sock).unwrap();
        let e2 = env.clone();
        tokio::spawn(async move {
            while let Ok((s, _)) = l.accept().await {
                tokio::spawn(handle_conn(s, e2.clone(), Peer::Remote { attach: None }));
            }
        });

        let mut bad = UnixStream::connect(&sock).await.unwrap();
        let line = format!("{{\"auth\":\"nope\",\"host\":\"w\",\"os\":\"win\"}}\n{HOOK}\n");
        bad.write_all(line.as_bytes()).await.unwrap();
        let mut buf = Vec::new();
        assert_eq!(
            bad.read_to_end(&mut buf).await.unwrap(),
            0,
            "closed, no reply"
        );
        assert!(env.registry.snapshot().is_empty());

        let mut good = UnixStream::connect(&sock).await.unwrap();
        let line = format!("{{\"auth\":\"{TOKEN}\",\"host\":\"Win1\",\"os\":\"win\"}}\n{HOOK}\n");
        good.write_all(line.as_bytes()).await.unwrap();
        assert!(eventually(|| !env.registry.snapshot().is_empty()).await);
        let s = &env.registry.snapshot()[0];
        assert_eq!(
            (s.host, s.hn.as_deref(), s.repo.as_deref()),
            (Some(1), Some("Win1"), Some("proj"))
        );
        assert_eq!(s.br.as_deref(), Some("main"));
        assert_eq!(env.registry.remotes()[0]["name"], "Win1");
        let _ = std::fs::remove_dir_all(d);
    }

    #[tokio::test]
    async fn attach_lives_exactly_as_long_as_its_ctl_connection() {
        let d = scratch("att");
        let env = env(&d);
        let req = json!({"ctl":"remote_attach","ctx":{"env":{"KITTY_WINDOW_ID":"3"}}});
        let (reply, guard) = open_attach(&env, &req);
        assert_eq!(reply["ok"], false, "no token yet");
        std::fs::write(d.join("remote.token"), TOKEN).unwrap();
        let (reply, guard2) = open_attach(&env, &req);
        assert!(guard.is_none());
        assert_eq!(reply["id"], 1);
        let sock = d.join("remote-1.sock");
        assert_eq!(reply["socket"], sock.to_str().unwrap());

        let mut c = UnixStream::connect(&sock).await.unwrap();
        let line = format!("{{\"auth\":\"{TOKEN}\",\"host\":\"w\",\"os\":\"win\"}}\n{HOOK}\n");
        c.write_all(line.as_bytes()).await.unwrap();
        assert!(eventually(|| !env.registry.snapshot().is_empty()).await);
        assert!(matches!(
            env.registry.route(1),
            crate::registry::Route::Ready(..)
        ));

        drop(guard2);
        assert!(!sock.exists());
        assert!(
            env.registry.snapshot().is_empty(),
            "its sessions end with the tunnel"
        );
        assert!(env.registry.attaches().is_empty());
        let _ = std::fs::remove_dir_all(d);
    }
}
