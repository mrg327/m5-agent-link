//! Which finishes buzz on the watch (issue 20261126-27b7, PROTOCOL.md §6j). The PC chooses, the watch
//! decides each finish:
//!
//! - `all` (the default): every turn of 30 s or more that ends done, and every error, as well as the
//!   sessions below. The watch measures the turn from `session.dur`, else from `tst`.
//! - `armed`: only a session armed with Notify, a job, or the session the watch has open (before 0.25).
//!
//! `AGENTLINK_NOTIFY`, else `~/.agentlink/notify` (`agentlink notify` writes it). Read at every welcome
//! and before every delta that enters done / error, so an edit reaches a connected watch with the next
//! finish. The watch gets it as `device_cfg.done_buzz`, only with proto 0.25 or later.

use agentlink_proto::DoneBuzz;

/// The setting's file under the daemon's home.
pub const FILE: &str = "notify";

/// The first word: `all` or `armed`; anything else (an empty or missing file) is the default, `all`.
pub fn parse(text: &str) -> DoneBuzz {
    match text
        .split_whitespace()
        .next()
        .map(str::to_ascii_lowercase)
        .as_deref()
    {
        Some("armed") => DoneBuzz::Armed,
        _ => DoneBuzz::All,
    }
}

/// The setting as it stands now.
pub fn mode() -> DoneBuzz {
    if let Ok(v) = std::env::var("AGENTLINK_NOTIFY") {
        return parse(&v);
    }
    std::fs::read_to_string(crate::paths::home().join(FILE)).map_or(DoneBuzz::All, |t| parse(&t))
}

/// How `agentlink status` names it.
pub fn name(m: DoneBuzz) -> &'static str {
    match m {
        DoneBuzz::All => "all",
        DoneBuzz::Armed => "armed",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_unless_the_file_says_armed() {
        assert_eq!(parse(""), DoneBuzz::All);
        assert_eq!(parse("all\n"), DoneBuzz::All);
        assert_eq!(parse("  Armed \n"), DoneBuzz::Armed);
        assert_eq!(parse("armed 120"), DoneBuzz::Armed);
        assert_eq!(parse("loud"), DoneBuzz::All);
        assert_eq!(name(parse("armed")), "armed");
    }
}
