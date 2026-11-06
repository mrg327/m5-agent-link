//! agentlink: the CLI window into the daemon (DESIGN §0: auditability).

mod hooks;
mod status;
mod trace;

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(
    name = "agentlink",
    version,
    about = "Inspect and control the agentlink daemon"
)]
struct Cli {
    /// Machine-readable JSON output.
    #[arg(long, global = true)]
    json: bool,
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Ask the running daemon for its status.
    Status,
    /// Pretty-print today's JSONL trace.
    Trace {
        /// Keep printing new lines as they are written.
        #[arg(long, short)]
        follow: bool,
        /// Only show events for this session id.
        #[arg(long)]
        sid: Option<u64>,
    },
    /// Decode a hex-encoded CBOR envelope to JSON.
    Decode { hex: String },
    /// Agent hook configuration.
    Hooks {
        #[command(subcommand)]
        cmd: HooksCmd,
    },
    /// Inspect or drive the connected device.
    Device {
        #[command(subcommand)]
        cmd: DeviceCmd,
    },
}

#[derive(Subcommand)]
enum HooksCmd {
    /// Print the config snippets for Copilot CLI and Claude Code (nothing is installed).
    Print,
}

#[derive(Subcommand)]
enum DeviceCmd {
    /// Report the device's UI state.
    State,
    /// Inject a tap.
    Tap { x: Option<i64>, y: Option<i64> },
    /// Inject a button press.
    Press { button: String },
    /// Inject a wrist-lift.
    Lift,
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.cmd {
        Cmd::Status => status::run(cli.json),
        Cmd::Trace { follow, sid } => trace::run(cli.json, follow, sid),
        Cmd::Decode { hex } => decode(&hex, cli.json),
        Cmd::Hooks {
            cmd: HooksCmd::Print,
        } => hooks::print(cli.json),
        Cmd::Device { cmd } => {
            let name = match cmd {
                DeviceCmd::State => "state",
                DeviceCmd::Tap { .. } => "tap",
                DeviceCmd::Press { .. } => "press",
                DeviceCmd::Lift => "lift",
            };
            if cli.json {
                println!(
                    "{}",
                    serde_json::json!({"ok": false, "command": name, "error": "not yet implemented"})
                );
            } else {
                println!("device {name}: not yet implemented");
            }
            Ok(())
        }
    }
}

fn decode(hex_str: &str, compact: bool) -> Result<()> {
    let clean: String = hex_str
        .trim()
        .trim_start_matches("0x")
        .chars()
        .filter(|c| !c.is_whitespace())
        .collect();
    let bytes = hex::decode(clean).context("invalid hex")?;
    let env =
        agentlink_proto::Envelope::from_bytes(&bytes).context("not a valid agentlink envelope")?;
    let v = serde_json::to_value(&env)?;
    println!(
        "{}",
        if compact {
            v.to_string()
        } else {
            serde_json::to_string_pretty(&v)?
        }
    );
    Ok(())
}

/// `~/.agentlink` (or `$AGENTLINK_HOME`), matching the daemon.
pub(crate) fn home() -> std::path::PathBuf {
    if let Some(h) = std::env::var_os("AGENTLINK_HOME") {
        return h.into();
    }
    let user = std::env::var_os("HOME")
        .or_else(|| std::env::var_os("USERPROFILE"))
        .unwrap_or_default();
    std::path::PathBuf::from(user).join(".agentlink")
}
