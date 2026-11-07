//! End to end: spawn the daemon, talk to it as the simulator would (hello vector over
//! WebSocket), push a hook event over the Unix socket, and check the trace.

use agentlink_proto::{Envelope, Message, SessState};
use futures_util::{SinkExt, StreamExt};
use std::process::{Child, Command, Stdio};
use std::time::Duration;
use tokio_tungstenite::tungstenite::Message as Ws;

struct Daemon(Child);
impl Drop for Daemon {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

fn vector_bytes(name: &str) -> Vec<u8> {
    let p = format!(
        "{}/../../../protocol/vectors/{name}.json",
        env!("CARGO_MANIFEST_DIR")
    );
    let v: serde_json::Value = serde_json::from_slice(&std::fs::read(p).unwrap()).unwrap();
    hex::decode(v["cbor_hex"].as_str().unwrap()).unwrap()
}

async fn next_env<S>(ws: &mut S) -> Envelope
where
    S: StreamExt<Item = Result<Ws, tokio_tungstenite::tungstenite::Error>> + Unpin,
{
    let m = tokio::time::timeout(Duration::from_secs(5), ws.next())
        .await
        .expect("timeout")
        .unwrap()
        .unwrap();
    Envelope::from_bytes(&m.into_data()).unwrap()
}

/// Named pipe for the hook IPC on Windows (the daemon honours `AGENTLINK_PIPE`); unused on Unix.
fn pipe_name(port: u16) -> String {
    format!(r"\\.\pipe\agentlink-smoke-{port}")
}

/// Writes one hook line over the platform IPC and returns the open connection.
#[cfg(unix)]
async fn send_hook(
    home: &std::path::Path,
    _port: u16,
    line: &str,
) -> std::os::unix::net::UnixStream {
    use std::io::Write;
    let mut s = std::os::unix::net::UnixStream::connect(home.join("hook.sock")).unwrap();
    writeln!(s, "{line}").unwrap();
    s
}

#[cfg(windows)]
async fn send_hook(
    _home: &std::path::Path,
    port: u16,
    line: &str,
) -> tokio::net::windows::named_pipe::NamedPipeClient {
    use tokio::io::AsyncWriteExt;
    let mut c = loop {
        match tokio::net::windows::named_pipe::ClientOptions::new().open(pipe_name(port)) {
            Ok(c) => break c,
            Err(_) => tokio::time::sleep(Duration::from_millis(50)).await,
        }
    };
    c.write_all(format!("{line}\n").as_bytes()).await.unwrap();
    c
}

#[tokio::test]
async fn handshake_hook_event_and_focus() {
    let home = std::path::PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("smoke-home");
    let _ = std::fs::remove_dir_all(&home);
    let port = std::net::TcpListener::bind("127.0.0.1:0")
        .unwrap()
        .local_addr()
        .unwrap()
        .port();
    let _d = Daemon(
        Command::new(env!("CARGO_BIN_EXE_agentlinkd"))
            .env("AGENTLINK_HOME", &home)
            .env("AGENTLINK_USB", "0")
            .env("AGENTLINK_BLE", "0")
            .env("AGENTLINK_WS_ADDR", format!("127.0.0.1:{port}"))
            .env("AGENTLINK_PIPE", pipe_name(port))
            .stderr(Stdio::null())
            .spawn()
            .unwrap(),
    );

    let url = format!("ws://127.0.0.1:{port}");
    let mut ws = loop {
        match tokio_tungstenite::connect_async(&url).await {
            Ok((ws, _)) => break ws,
            Err(_) => tokio::time::sleep(Duration::from_millis(50)).await,
        }
    };
    ws.send(Ws::Binary(vector_bytes("hello_sim").into()))
        .await
        .unwrap();
    let welcome = next_env(&mut ws).await;
    assert!(matches!(welcome.msg, Message::Welcome(_)));
    assert_eq!(welcome.re, Some(1));
    let Message::Snapshot(snap) = next_env(&mut ws).await.msg else {
        panic!("expected snapshot")
    };
    assert!(snap.sessions.is_empty());

    // A hook event creates a session, pushed as session_upd.
    let _hook = send_hook(
        &home,
        port,
        r#"{"agent":"copilot","event":"preToolUse","cwd":"/x/proj","payload":{"sessionId":"a","toolName":"bash"}}"#,
    )
    .await;
    let Message::SessionUpd(sess) = next_env(&mut ws).await.msg else {
        panic!("expected session_upd")
    };
    assert_eq!((sess.sid, sess.st), (1, Some(SessState::Work)));

    // cmd focus (vector targets sid 2, which does not exist) is acked with a stale result.
    ws.send(Ws::Binary(vector_bytes("cmd_focus").into()))
        .await
        .unwrap();
    let res = next_env(&mut ws).await;
    assert_eq!(res.re, Some(5));
    assert!(matches!(res.msg, Message::Result(r) if !r.ok));

    let day = std::fs::read_dir(home.join("trace"))
        .unwrap()
        .next()
        .unwrap()
        .unwrap()
        .path();
    let text = std::fs::read_to_string(day).unwrap();
    for kind in [
        "hello",
        "welcome",
        "snapshot",
        "preToolUse",
        "session_upd",
        "cmd",
        "result",
    ] {
        assert!(
            text.contains(&format!("\"kind\":\"{kind}\"")),
            "trace lacks {kind}"
        );
    }
}

#[tokio::test]
async fn second_hello_is_a_full_rehandshake() {
    let home = std::path::PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("rehello-home");
    let _ = std::fs::remove_dir_all(&home);
    let port = std::net::TcpListener::bind("127.0.0.1:0")
        .unwrap()
        .local_addr()
        .unwrap()
        .port();
    let _d = Daemon(
        Command::new(env!("CARGO_BIN_EXE_agentlinkd"))
            .env("AGENTLINK_HOME", &home)
            .env("AGENTLINK_USB", "0")
            .env("AGENTLINK_BLE", "0")
            .env("AGENTLINK_WS_ADDR", format!("127.0.0.1:{port}"))
            .stderr(Stdio::null())
            .spawn()
            .unwrap(),
    );
    let url = format!("ws://127.0.0.1:{port}");
    let mut ws = loop {
        match tokio_tungstenite::connect_async(&url).await {
            Ok((ws, _)) => break ws,
            Err(_) => tokio::time::sleep(Duration::from_millis(50)).await,
        }
    };
    for round in 0..2 {
        ws.send(Ws::Binary(vector_bytes("hello_sim").into()))
            .await
            .unwrap();
        let welcome = next_env(&mut ws).await;
        assert!(matches!(welcome.msg, Message::Welcome(_)), "round {round}");
        assert_eq!((welcome.id, welcome.re), (1, Some(1)), "round {round}");
        let snap = next_env(&mut ws).await;
        assert!(matches!(snap.msg, Message::Snapshot(_)), "round {round}");
        assert_eq!(snap.id, 2, "round {round}");
    }

    let day = std::fs::read_dir(home.join("trace"))
        .unwrap()
        .next()
        .unwrap()
        .unwrap()
        .path();
    let text = std::fs::read_to_string(day).unwrap();
    assert!(text.contains("\"kind\":\"rehandshake\""));
}
