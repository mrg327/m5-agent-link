//! agentlink: the CLI window into the daemon (DESIGN §0: auditability).

mod hooks;
mod remote;
mod remotes;
mod ssh;
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
    /// Remote sessions: the shared token and saved remotes.
    Remote {
        #[command(subcommand)]
        cmd: RemoteCmd,
    },
    /// Run ssh with a tunnel to this Mac's daemon, so remote agents reach the watch.
    /// DEST is a saved remote's name (agentlink remote list) or any ssh destination.
    Ssh {
        /// Remote loopback port the hook connects to (`AGENTLINK_REMOTE=tcp:127.0.0.1:<port>`).
        /// Default: the saved remote's port, else 47533.
        #[arg(long)]
        port: Option<u16>,
        /// For a saved remote: open a plain shell instead of running its on-connect command.
        #[arg(long)]
        no_cmd: bool,
        dest: String,
        /// Extra arguments passed to ssh unchanged.
        #[arg(trailing_var_arg = true, allow_hyphen_values = true)]
        args: Vec<String>,
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
enum RemoteCmd {
    /// Create ~/.agentlink/remote.token (the secret remote hooks authenticate with).
    Init {
        /// Replace an existing token.
        #[arg(long)]
        rotate: bool,
    },
    /// Save a remote under a name, with an optional start directory and on-connect command.
    Add {
        name: String,
        /// ssh destination: user@host or an ssh config alias.
        dest: String,
        /// Replace an existing remote with this name.
        #[arg(long)]
        force: bool,
        /// Onboard it right away (same as `agentlink remote setup NAME`).
        #[arg(long)]
        setup: bool,
        #[command(flatten)]
        fields: remotes::Fields,
        #[command(flatten)]
        setup_opts: SetupOpts,
    },
    /// Change a saved remote; options not given keep their value, "" clears --dir/--cmd.
    Edit {
        name: String,
        /// New ssh destination.
        #[arg(long)]
        dest: Option<String>,
        #[command(flatten)]
        fields: remotes::Fields,
    },
    /// List saved remotes.
    List,
    /// Forget a saved remote (nothing on the remote is changed).
    Rm { name: String },
    /// Onboard a saved remote (Windows or Linux) over ssh: installs this Mac's key if needed, copies the
    /// token and agentlink-hook, writes the Copilot hooks config, sets AGENTLINK_REMOTE.
    /// Safe to re-run (e.g. after `remote init --rotate` or a new hook build).
    Setup {
        name: String,
        #[command(flatten)]
        opts: SetupOpts,
    },
}

#[derive(clap::Args)]
struct SetupOpts {
    /// Hook binary for the remote (default ~/.agentlink/dist/windows-x64/agentlink-hook.exe, or
    /// dist/linux-<x64|arm64>/agentlink-hook for Unix).
    #[arg(long)]
    hook_exe: Option<std::path::PathBuf>,
    /// Public key to install when key login fails (default ~/.ssh/id_ed25519.pub).
    #[arg(long)]
    key: Option<std::path::PathBuf>,
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
        Cmd::Remote { cmd } => match cmd {
            RemoteCmd::Init { rotate } => remote::init(cli.json, rotate),
            RemoteCmd::Add {
                name,
                dest,
                force,
                setup,
                fields,
                setup_opts,
            } => {
                remotes::add(cli.json, &name, &dest, force, fields)?;
                if setup {
                    remotes::setup(&name, setup_opts.hook_exe, setup_opts.key)?;
                }
                Ok(())
            }
            RemoteCmd::Edit { name, dest, fields } => remotes::edit(cli.json, &name, dest, fields),
            RemoteCmd::List => remotes::list(cli.json),
            RemoteCmd::Rm { name } => remotes::rm(cli.json, &name),
            RemoteCmd::Setup { name, opts } => remotes::setup(&name, opts.hook_exe, opts.key),
        },
        Cmd::Ssh {
            port,
            no_cmd,
            dest,
            args,
        } => match remotes::lookup(&dest)? {
            Some(p) => {
                let (pre, post) = remotes::connect_args(&p, no_cmd, &args);
                ssh::run(port.unwrap_or(p.port()), &p.dest, &pre, &post)
            }
            None => ssh::run(port.unwrap_or(remotes::DEFAULT_PORT), &dest, &[], &args),
        },
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ssh_passes_hyphenated_args_through() {
        let c = Cli::try_parse_from([
            "agentlink",
            "ssh",
            "--port",
            "1234",
            "win",
            "-p",
            "22",
            "-v",
        ])
        .unwrap();
        let Cmd::Ssh {
            port, dest, args, ..
        } = c.cmd
        else {
            panic!("not ssh")
        };
        assert_eq!(
            (port, dest.as_str(), args),
            (
                Some(1234),
                "win",
                vec!["-p".to_owned(), "22".into(), "-v".into()]
            )
        );
    }
}
