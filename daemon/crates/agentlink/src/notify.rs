//! `agentlink notify`: which finished sessions buzz on the watch (issue 20261126-27b7). The setting is
//! `notify` in the agentlink home, read by agentlinkd (notify.rs there) at every welcome and before
//! every finish, so a change reaches a connected watch with the next finish.

use anyhow::{Context, Result};

/// The two settings, as the file spells them.
#[derive(Clone, Copy, Debug, PartialEq, Eq, clap::ValueEnum)]
pub enum Mode {
    /// Every turn of 30 s or more that ends done, every error, and the armed ones (the default).
    All,
    /// Only a session armed with Notify, a job, or the session the watch has open.
    Armed,
}

impl Mode {
    fn name(self) -> &'static str {
        match self {
            Mode::All => "all",
            Mode::Armed => "armed",
        }
    }

    /// What `agentlink notify` and `agentlink status` print.
    pub fn describe(self) -> &'static str {
        match self {
            Mode::All => "all (turns of 30 s or more, and every error)",
            Mode::Armed => "armed (only sessions armed with Notify, jobs and the open session)",
        }
    }
}

fn path() -> std::path::PathBuf {
    crate::home().join("notify")
}

/// The first word, as agentlinkd parses it: `armed`, else `all`.
pub fn parse(text: &str) -> Mode {
    match text.split_whitespace().next() {
        Some(w) if w.eq_ignore_ascii_case("armed") => Mode::Armed,
        _ => Mode::All,
    }
}

/// With a mode, writes it; either way prints the setting now in force.
pub fn run(json: bool, mode: Option<Mode>) -> Result<()> {
    let p = path();
    if let Some(m) = mode {
        if let Some(dir) = p.parent() {
            std::fs::create_dir_all(dir).with_context(|| format!("creating {}", dir.display()))?;
        }
        std::fs::write(&p, format!("{}\n", m.name()))
            .with_context(|| format!("writing {}", p.display()))?;
    }
    let env = std::env::var("AGENTLINK_NOTIFY").ok();
    let now = match &env {
        Some(v) => parse(v),
        None => parse(&std::fs::read_to_string(&p).unwrap_or_default()),
    };
    if json {
        println!(
            "{}",
            serde_json::json!({"notify": now.name(), "file": p, "env": env.is_some()})
        );
        return Ok(());
    }
    println!("notify: {}", now.describe());
    if env.is_some() {
        println!("  (AGENTLINK_NOTIFY is set and overrides {})", p.display());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_as_the_daemon_does() {
        assert_eq!(parse(""), Mode::All);
        assert_eq!(parse("ARMED\n"), Mode::Armed);
        assert_eq!(parse("all"), Mode::All);
        assert_eq!(parse("loud"), Mode::All);
        assert_eq!(parse(&format!("{}\n", Mode::Armed.name())), Mode::Armed);
    }
}
