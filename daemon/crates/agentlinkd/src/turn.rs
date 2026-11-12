//! The summary of the turn that just ended (DONE-SUMMARY.md): files changed, lines added and removed,
//! the last test run's verdict and the turn's length, tallied from Claude Code's hook payloads.
//!
//! Everything here is conservative: a figure is produced only when the payloads settle it, and an
//! unknown one is `None`, never zero. File names are counted and dropped; they never leave this module.

use serde_json::Value;
use std::collections::HashSet;
use std::time::Instant;

/// Distinct paths remembered per turn; past it the file count is no longer known.
const MAX_PATHS: usize = 4096;
/// Largest line diff attempted (cells of the table); past it the lines are unknown.
const MAX_DIFF_CELLS: usize = 4_000_000;

/// What one tool call did to the turn.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Effect {
    /// A file the call changed (an Edit-style call that succeeded).
    pub file: Option<String>,
    /// Lines added / removed by that call; `None` = could not be told.
    pub lines: Option<(u64, u64)>,
    /// The call ran a recognised test command: `Some(None)` = it did, but the verdict is unknown.
    pub test: Option<Option<bool>>,
}

/// A turn's figures as they go on the wire (each absent when unknown).
#[derive(Debug, Clone, Default, PartialEq)]
pub struct DoneSummary {
    pub files: Option<u64>,
    pub lines: Option<(u64, u64)>,
    pub tests: Option<bool>,
    pub secs: Option<u64>,
}

impl DoneSummary {
    pub fn is_empty(&self) -> bool {
        *self == DoneSummary::default()
    }
}

#[derive(Debug, Clone)]
pub struct Tally {
    started: Instant,
    paths: HashSet<String>,
    overflow: bool,
    lines: Option<(u64, u64)>,
    tests: Option<bool>,
}

impl Tally {
    /// A new user prompt: everything starts over.
    pub fn start(now: Instant) -> Self {
        Tally {
            started: now,
            paths: HashSet::new(),
            overflow: false,
            lines: Some((0, 0)),
            tests: None,
        }
    }

    pub fn add(&mut self, fx: &Effect) {
        if let Some(f) = &fx.file {
            if self.paths.len() < MAX_PATHS {
                self.paths.insert(f.clone());
            } else if !self.paths.contains(f) {
                self.overflow = true;
            }
            self.lines = match (self.lines, fx.lines) {
                (Some((a, r)), Some((da, dr))) => Some((a + da, r + dr)),
                _ => None,
            };
        }
        if let Some(t) = fx.test {
            self.tests = t; // the last test run of the turn decides, even when it is unknown
        }
    }

    /// The figures at `now`. Files and lines are only given when at least one edit was seen: a turn
    /// without one may still have changed files through the shell, and "0 files" would be a claim.
    pub fn summary(&self, now: Instant) -> DoneSummary {
        let files = (!self.paths.is_empty() && !self.overflow).then_some(self.paths.len() as u64);
        DoneSummary {
            files,
            lines: self.lines.filter(|_| files.is_some()),
            tests: self.tests,
            secs: Some(
                now.saturating_duration_since(self.started)
                    .as_secs()
                    .min(99 * 3600),
            ),
        }
    }

    /// Elapsed time so far (for tests of the clock).
    #[cfg(test)]
    pub fn age(&self, now: Instant) -> std::time::Duration {
        now.saturating_duration_since(self.started)
    }
}

// ---------------------------------------------------------------------------------------------
// Payloads
// ---------------------------------------------------------------------------------------------

/// What a Claude Code `PostToolUse` / `PostToolUseFailure` payload did to the turn, or `None` when it
/// touched neither a file nor a test command.
pub fn effect_of(event: &str, payload: &Value) -> Option<Effect> {
    let failed = event.eq_ignore_ascii_case("posttoolusefailure");
    let input = &payload["tool_input"];
    match payload["tool_name"].as_str()? {
        "Edit" | "Write" | "MultiEdit" | "NotebookEdit" if failed => None,
        "Edit" => file_effect(input["file_path"].as_str()?, edit_lines(input, payload)),
        "MultiEdit" => file_effect(input["file_path"].as_str()?, multi_lines(input, payload)),
        "Write" => file_effect(input["file_path"].as_str()?, write_lines(input, payload)),
        // a notebook cell is not a text file: the file is known, the lines are not
        "NotebookEdit" => file_effect(input["notebook_path"].as_str()?, None),
        "Bash" => {
            let cmd = input["command"].as_str()?;
            let found = recognise(cmd)?;
            let verdict = if input["run_in_background"].as_bool() == Some(true)
                || payload["tool_response"]["interrupted"].as_bool() == Some(true)
                || payload["is_interrupt"].as_bool() == Some(true)
            {
                None
            } else if failed {
                found.on_failure(exit_code(payload["error"].as_str().unwrap_or("")))
            } else {
                Some(true) // exit status 0: Claude Code reports a non-zero one as a failure event
            };
            Some(Effect {
                test: Some(verdict),
                ..Effect::default()
            })
        }
        _ => None,
    }
}

fn file_effect(path: &str, lines: Option<(u64, u64)>) -> Option<Effect> {
    (!path.is_empty()).then(|| Effect {
        file: Some(path.to_owned()),
        lines,
        test: None,
    })
}

/// `structuredPatch` is not in the documented payload but Claude Code's file tools return it; when it is
/// there it is exact (hunks of `+` / `-` / context lines).
fn patch_lines(payload: &Value) -> Option<(u64, u64)> {
    let hunks = payload["tool_response"]["structuredPatch"].as_array()?;
    let (mut a, mut r) = (0, 0);
    for h in hunks {
        for l in h["lines"].as_array()? {
            match l.as_str()?.as_bytes().first() {
                Some(b'+') => a += 1,
                Some(b'-') => r += 1,
                _ => {}
            }
        }
    }
    Some((a, r))
}

fn edit_lines(input: &Value, payload: &Value) -> Option<(u64, u64)> {
    if let Some(p) = patch_lines(payload) {
        return Some(p);
    }
    if input["replace_all"].as_bool() == Some(true) {
        return None; // how many places changed is not in the payload
    }
    line_delta(input["old_string"].as_str()?, input["new_string"].as_str()?)
}

fn multi_lines(input: &Value, payload: &Value) -> Option<(u64, u64)> {
    if let Some(p) = patch_lines(payload) {
        return Some(p);
    }
    let (mut a, mut r) = (0, 0);
    for e in input["edits"].as_array()? {
        let (da, dr) = edit_lines(e, &Value::Null)?;
        a += da;
        r += dr;
    }
    Some((a, r))
}

fn write_lines(input: &Value, payload: &Value) -> Option<(u64, u64)> {
    if let Some(p) = patch_lines(payload) {
        return Some(p);
    }
    // a new file is all additions; overwriting one needs the old text, which the payload lacks
    (payload["tool_response"]["type"].as_str() == Some("create")).then(|| {
        (
            input["content"]
                .as_str()
                .map_or(0, |c| c.lines().count() as u64),
            0,
        )
    })
}

/// Lines added and removed turning `old` into `new`: the length of the line-wise shortest edit script.
fn line_delta(old: &str, new: &str) -> Option<(u64, u64)> {
    let a: Vec<&str> = old.lines().collect();
    let b: Vec<&str> = new.lines().collect();
    // trim the common head and tail first: most edits are small inside big contexts
    let head = a.iter().zip(&b).take_while(|(x, y)| x == y).count();
    let tail = a[head..]
        .iter()
        .rev()
        .zip(b[head..].iter().rev())
        .take_while(|(x, y)| x == y)
        .count();
    let (a, b) = (&a[head..a.len() - tail], &b[head..b.len() - tail]);
    if a.len().saturating_mul(b.len()) > MAX_DIFF_CELLS {
        return None;
    }
    let mut prev = vec![0u32; b.len() + 1];
    let mut cur = vec![0u32; b.len() + 1];
    for x in a {
        for (j, y) in b.iter().enumerate() {
            cur[j + 1] = if x == y {
                prev[j] + 1
            } else {
                cur[j].max(prev[j + 1])
            };
        }
        std::mem::swap(&mut prev, &mut cur);
    }
    let common = prev[b.len()] as u64;
    Some((b.len() as u64 - common, a.len() as u64 - common))
}

/// `N` of the first line `Exit code N` of a failure's `error` text.
fn exit_code(error: &str) -> Option<i32> {
    error
        .lines()
        .next()?
        .trim()
        .strip_prefix("Exit code ")?
        .trim()
        .parse()
        .ok()
}

// ---------------------------------------------------------------------------------------------
// Test commands
// ---------------------------------------------------------------------------------------------

/// Recognised test commands: a command word followed by the words that make it a test run (flags in
/// between are skipped; a trailing `*` matches any suffix). Add a row to teach the daemon another runner.
const TEST_COMMANDS: &[&[&str]] = &[
    &["cargo", "test"],
    &["cargo", "nextest", "run"],
    &["npm", "test"],
    &["npm", "t"],
    &["npm", "run", "test*"],
    &["npm", "run-script", "test*"],
    &["pnpm", "test"],
    &["pnpm", "t"],
    &["pnpm", "run", "test*"],
    &["yarn", "test"],
    &["yarn", "run", "test*"],
    &["bun", "test"],
    &["bun", "run", "test*"],
    &["pytest"],
    &["py.test"],
    &["python", "-m", "pytest"],
    &["python", "-m", "unittest"],
    &["tox"],
    &["go", "test"],
    &["ctest"],
    &["make", "test"],
    &["make", "tests"],
    &["make", "check"],
    &["ninja", "test"],
    &["just", "test"],
    &["mvn", "test"],
    &["mvn", "verify"],
    &["gradle", "test"],
    &["gradlew", "test"],
    &["dotnet", "test"],
    &["jest"],
    &["vitest", "run"],
    &["phpunit"],
    &["rspec"],
    &["mix", "test"],
    &["swift", "test"],
    &["deno", "test"],
    &["flutter", "test"],
];

/// Flags that make a "test command" not run any test.
const NO_RUN_FLAGS: &[&str] = &[
    "--no-run",
    "--help",
    "-h",
    "--list",
    "--collect-only",
    "--co",
    "--version",
    "--dry-run",
];

/// Commands that may precede the test in a `&&` chain without making a failure ambiguous.
const PRELUDE: &[&str] = &["cd", "export", "set", "source", ".", "unset", "pushd"];

/// A recognised test command line and where the test sits in it.
#[derive(Debug, PartialEq)]
pub struct Found {
    /// More than one test command, or something other than a prelude before / after the test: a
    /// failure could be any of them.
    ambiguous_failure: bool,
}

impl Found {
    /// The verdict of a failed run: tests failed, unless the exit code says the command never ran
    /// (126, 127) or was killed (128 and up), or the chain makes it ambiguous.
    fn on_failure(&self, code: Option<i32>) -> Option<bool> {
        match code {
            Some(c) if !self.ambiguous_failure && c > 0 && c != 126 && c != 127 && c < 128 => {
                Some(false)
            }
            _ => None,
        }
    }
}

/// Whether `cmd` is a test run whose exit status is the tests' status: a single `&&` chain with no
/// pipe, `;`, `||`, background `&`, substitution or newline (any of them makes the status someone
/// else's).
pub fn recognise(cmd: &str) -> Option<Found> {
    let segments = split_chain(cmd)?;
    let tests: Vec<usize> = segments
        .iter()
        .enumerate()
        .filter(|(_, s)| is_test_segment(s))
        .map(|(i, _)| i)
        .collect();
    let first = *tests.first()?;
    let prelude_only = segments[..first].iter().all(|s| {
        s.split_whitespace()
            .next()
            .is_some_and(|w| PRELUDE.contains(&w))
    });
    Some(Found {
        ambiguous_failure: tests.len() > 1 || !prelude_only || first + 1 != segments.len(),
    })
}

/// The `&&`-separated segments, or `None` when the line uses any other shell control operator.
fn split_chain(cmd: &str) -> Option<Vec<String>> {
    let mut out = vec![String::new()];
    let mut quote: Option<char> = None;
    let cs: Vec<char> = cmd.chars().collect();
    let mut i = 0;
    while i < cs.len() {
        let c = cs[i];
        match quote {
            Some(q) => {
                if c == q {
                    quote = None;
                } else if q == '"' && (c == '`' || (c == '$' && cs.get(i + 1) == Some(&'('))) {
                    return None;
                }
                out.last_mut()?.push(c);
            }
            None => match c {
                '\'' | '"' => {
                    quote = Some(c);
                    out.last_mut()?.push(c);
                }
                '\\' => {
                    out.last_mut()?.push(c);
                    if let Some(n) = cs.get(i + 1) {
                        out.last_mut()?.push(*n);
                        i += 1;
                    }
                }
                '|' | ';' | '\n' | '`' | '(' | ')' | '{' | '}' => return None,
                '$' if cs.get(i + 1) == Some(&'(') => return None,
                '&' if cs.get(i + 1) == Some(&'&') => {
                    out.push(String::new());
                    i += 1;
                }
                // `2>&1` and `&>` redirections are fine; a lone `&` backgrounds
                '&' if cs.get(i.wrapping_sub(1)) == Some(&'>') || cs.get(i + 1) == Some(&'>') => {
                    out.last_mut()?.push(c)
                }
                '&' => return None,
                _ => out.last_mut()?.push(c),
            },
        }
        i += 1;
    }
    if quote.is_some() {
        return None;
    }
    let out: Vec<String> = out.into_iter().map(|s| s.trim().to_owned()).collect();
    (out.iter().all(|s| !s.is_empty())).then_some(out)
}

fn is_test_segment(seg: &str) -> bool {
    let mut words: Vec<&str> = seg.split_whitespace().collect();
    // wrappers and assignments in front of the command
    loop {
        let Some(w) = words.first().copied() else {
            return false;
        };
        let is_assign = w.split_once('=').is_some_and(|(k, _)| {
            !k.is_empty() && k.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
        });
        if is_assign
            || matches!(
                w,
                "time" | "nice" | "command" | "env" | "exec" | "npx" | "bunx"
            )
        {
            words.remove(0);
        } else if matches!(w, "uv" | "poetry" | "pipenv" | "pdm" | "hatch" | "rye")
            && words.get(1) == Some(&"run")
        {
            words.drain(..2);
            while words.first().is_some_and(|w| w.starts_with('-')) {
                words.remove(0);
            }
        } else {
            break;
        }
    }
    if words.iter().any(|w| NO_RUN_FLAGS.contains(w)) {
        return false;
    }
    let base = |w: &str| -> String {
        let name = w.rsplit('/').next().unwrap_or(w);
        if name.starts_with("python") {
            "python".into()
        } else {
            name.trim_end_matches(".sh").to_owned()
        }
    };
    let Some(head) = words.first().map(|w| base(w)) else {
        return false;
    };
    TEST_COMMANDS.iter().any(|pat| {
        if pat[0] != head {
            return false;
        }
        let mut need = pat[1..].iter().peekable();
        for w in &words[1..] {
            let Some(n) = need.peek() else {
                return true;
            };
            let hit = match n.strip_suffix('*') {
                Some(prefix) => w.starts_with(prefix),
                None => w == *n,
            };
            if hit {
                need.next();
            } else if w.starts_with('-') || w.starts_with('+') {
                continue;
            } else {
                return false;
            }
        }
        need.peek().is_none()
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::time::Duration;

    fn bash(event: &str, cmd: &str, extra: Value) -> Option<Effect> {
        let mut p = json!({"tool_name": "Bash", "tool_input": {"command": cmd}});
        if let Value::Object(m) = extra {
            for (k, v) in m {
                p[k] = v;
            }
        }
        effect_of(event, &p)
    }

    fn verdict(event: &str, cmd: &str, extra: Value) -> Option<Option<bool>> {
        bash(event, cmd, extra).and_then(|e| e.test)
    }

    #[test]
    fn recognises_the_documented_runners() {
        for c in [
            "cargo test",
            "cargo test --workspace -- --nocapture",
            "cargo +nightly test -p foo",
            "cargo nextest run",
            "npm test",
            "npm run test:unit",
            "npm run test -- --watch=false",
            "pnpm test",
            "pnpm run test",
            "yarn test",
            "bun test",
            "pytest -x tests/",
            "python3 -m pytest",
            "uv run pytest",
            "uv run --no-sync python -m pytest",
            "go test ./...",
            "ctest --test-dir build -j 4",
            "make test",
            "make -j4 check",
            "RUST_LOG=debug cargo test",
            "time cargo test",
            "./gradlew test",
            "dotnet test",
            "npx vitest run",
            "cd daemon && cargo test --workspace",
        ] {
            assert!(recognise(c).is_some(), "{c}");
        }
    }

    #[test]
    fn leaves_other_commands_alone() {
        for c in [
            "cargo build",
            "cargo test --no-run",
            "cargo test -- --list",
            "pytest --collect-only",
            "npm install",
            "npm run build",
            "ls tests",
            "echo cargo test",
            "grep -r 'go test' .",
            "make",
            "git commit -m 'cargo test'",
            "python script.py",
            "vitest",
        ] {
            assert!(recognise(c).is_none(), "{c}");
        }
    }

    #[test]
    fn a_pipe_or_an_or_makes_the_status_someone_elses() {
        for c in [
            "cargo test | tail -5",
            "cargo test || true",
            "cargo test; echo done",
            "cargo test &",
            "cargo test $(echo x)",
            "cargo test\necho",
            "(cargo test)",
        ] {
            assert!(recognise(c).is_none(), "{c}");
        }
        // redirections are not control operators
        assert!(recognise("cargo test 2>&1").is_some());
        assert!(recognise("cargo test &> out.txt").is_some());
    }

    #[test]
    fn success_is_a_pass_and_failure_needs_an_exit_code() {
        assert_eq!(
            verdict("PostToolUse", "cargo test", json!({})),
            Some(Some(true))
        );
        let err = |s: &str| json!({"error": s});
        assert_eq!(
            verdict(
                "PostToolUseFailure",
                "cargo test",
                err("Exit code 101\nfailures")
            ),
            Some(Some(false))
        );
        assert_eq!(
            verdict("PostToolUseFailure", "pytest", err("Exit code 1\n...")),
            Some(Some(false))
        );
        // never ran, killed, no code: unknown
        for e in [
            "Exit code 127\nnot found",
            "Exit code 126",
            "Exit code 137",
            "Exit code 0",
            "boom",
            "",
        ] {
            assert_eq!(
                verdict("PostToolUseFailure", "cargo test", err(e)),
                Some(None),
                "{e}"
            );
        }
    }

    #[test]
    fn interrupted_and_background_runs_have_no_verdict() {
        assert_eq!(
            verdict(
                "PostToolUse",
                "cargo test",
                json!({"tool_response": {"interrupted": true}})
            ),
            Some(None)
        );
        assert_eq!(
            verdict(
                "PostToolUseFailure",
                "cargo test",
                json!({"error": "Exit code 1", "is_interrupt": true})
            ),
            Some(None)
        );
        let p = json!({"tool_name": "Bash", "tool_input": {"command": "cargo test", "run_in_background": true}});
        assert_eq!(effect_of("PostToolUse", &p).unwrap().test, Some(None));
    }

    #[test]
    fn a_failure_inside_a_chain_is_ambiguous_unless_only_cd_comes_first() {
        let err = json!({"error": "Exit code 1"});
        assert_eq!(
            verdict("PostToolUseFailure", "cd x && cargo test", err.clone()),
            Some(Some(false))
        );
        // the build may be what failed
        assert_eq!(
            verdict(
                "PostToolUseFailure",
                "cargo build && cargo test",
                err.clone()
            ),
            Some(None)
        );
        // the lint after the tests may be what failed
        assert_eq!(
            verdict(
                "PostToolUseFailure",
                "cargo test && cargo clippy",
                err.clone()
            ),
            Some(None)
        );
        // but a pass of the whole chain is a pass of the tests
        assert_eq!(
            verdict(
                "PostToolUse",
                "cargo build && cargo test && echo ok",
                json!({})
            ),
            Some(Some(true))
        );
    }

    #[test]
    fn edits_count_files_and_lines() {
        let edit = json!({"tool_name": "Edit", "tool_input": {
            "file_path": "/p/a.rs", "old_string": "a\nb\nc", "new_string": "a\nB\nc\nd"}});
        let fx = effect_of("PostToolUse", &edit).unwrap();
        assert_eq!(fx.file.as_deref(), Some("/p/a.rs"));
        assert_eq!(fx.lines, Some((2, 1)));
        let w = json!({"tool_name": "Write", "tool_input": {"file_path": "/p/n.txt", "content": "1\n2\n3\n"},
            "tool_response": {"type": "create"}});
        assert_eq!(effect_of("PostToolUse", &w).unwrap().lines, Some((3, 0)));
        let multi = json!({"tool_name": "MultiEdit", "tool_input": {"file_path": "/p/m", "edits": [
            {"old_string": "x", "new_string": "y"}, {"old_string": "", "new_string": "q\nr"}]}});
        assert_eq!(
            effect_of("PostToolUse", &multi).unwrap().lines,
            Some((3, 1))
        );
    }

    #[test]
    fn a_structured_patch_beats_the_inputs() {
        let w = json!({"tool_name": "Write", "tool_input": {"file_path": "/p/f", "content": "a\nb\n"},
            "tool_response": {"type": "update", "structuredPatch": [
                {"oldStart": 1, "lines": [" keep", "-gone", "-gone2", "+new"]}]}});
        assert_eq!(effect_of("PostToolUse", &w).unwrap().lines, Some((1, 2)));
    }

    #[test]
    fn what_the_payload_cannot_tell_stays_unknown() {
        // overwriting a file: the old text is not in the payload
        let w = json!({"tool_name": "Write", "tool_input": {"file_path": "/p/f", "content": "a"},
            "tool_response": {"type": "update"}});
        let fx = effect_of("PostToolUse", &w).unwrap();
        assert!(fx.file.is_some());
        assert_eq!(fx.lines, None);
        let all = json!({"tool_name": "Edit", "tool_input": {"file_path": "/p/f",
            "old_string": "a", "new_string": "b", "replace_all": true}});
        assert_eq!(effect_of("PostToolUse", &all).unwrap().lines, None);
        let nb =
            json!({"tool_name": "NotebookEdit", "tool_input": {"notebook_path": "/p/n.ipynb"}});
        assert_eq!(effect_of("PostToolUse", &nb).unwrap().lines, None);
        // a failed edit changed nothing; reads and searches are not changes
        assert!(effect_of("PostToolUseFailure", &w).is_none());
        let read = json!({"tool_name": "Read", "tool_input": {"file_path": "/p/f"}});
        assert!(effect_of("PostToolUse", &read).is_none());
    }

    fn fx(path: &str, lines: Option<(u64, u64)>) -> Effect {
        Effect {
            file: Some(path.into()),
            lines,
            test: None,
        }
    }

    #[test]
    fn the_tally_counts_distinct_files_and_sums_lines() {
        let t0 = Instant::now();
        let mut t = Tally::start(t0);
        t.add(&fx("/a", Some((3, 1))));
        t.add(&fx("/a", Some((2, 0))));
        t.add(&fx("/b", Some((1, 4))));
        let s = t.summary(t0 + Duration::from_secs(252));
        assert_eq!(
            s,
            DoneSummary {
                files: Some(2),
                lines: Some((6, 5)),
                tests: None,
                secs: Some(252)
            }
        );
    }

    #[test]
    fn unknown_lines_poison_the_total_but_not_the_file_count() {
        let t0 = Instant::now();
        let mut t = Tally::start(t0);
        t.add(&fx("/a", Some((3, 1))));
        t.add(&fx("/b", None));
        t.add(&fx("/c", Some((1, 1))));
        let s = t.summary(t0);
        assert_eq!((s.files, s.lines), (Some(3), None));
    }

    #[test]
    fn a_turn_without_edits_claims_no_files_and_no_zero() {
        let t0 = Instant::now();
        let t = Tally::start(t0);
        let s = t.summary(t0 + Duration::from_secs(9));
        assert_eq!(
            s,
            DoneSummary {
                files: None,
                lines: None,
                tests: None,
                secs: Some(9)
            }
        );
    }

    #[test]
    fn the_last_test_run_decides_and_an_unknown_one_erases_the_earlier() {
        let t0 = Instant::now();
        let mut t = Tally::start(t0);
        let test = |v| Effect {
            test: Some(v),
            ..Effect::default()
        };
        t.add(&test(Some(false)));
        t.add(&test(Some(true)));
        assert_eq!(t.summary(t0).tests, Some(true));
        t.add(&test(None));
        assert_eq!(t.summary(t0).tests, None);
        t.add(&test(Some(false)));
        assert_eq!(t.summary(t0).tests, Some(false));
    }

    #[test]
    fn a_new_prompt_starts_over() {
        let t0 = Instant::now();
        let mut t = Tally::start(t0);
        t.add(&fx("/a", Some((1, 1))));
        t.add(&Effect {
            test: Some(Some(false)),
            ..Effect::default()
        });
        let later = t0 + Duration::from_secs(60);
        let t = Tally::start(later);
        assert_eq!(t.age(later), Duration::ZERO);
        let s = t.summary(later + Duration::from_secs(5));
        assert_eq!(
            (s.files, s.lines, s.tests, s.secs),
            (None, None, None, Some(5))
        );
    }

    #[test]
    fn too_many_paths_make_the_count_unknown() {
        let t0 = Instant::now();
        let mut t = Tally::start(t0);
        for i in 0..MAX_PATHS + 1 {
            t.add(&fx(&format!("/f{i}"), Some((1, 0))));
        }
        assert_eq!(t.summary(t0).files, None);
    }

    #[test]
    fn line_delta_is_a_real_diff() {
        assert_eq!(line_delta("a\nb\nc", "a\nb\nc"), Some((0, 0)));
        assert_eq!(line_delta("", "x\ny"), Some((2, 0)));
        assert_eq!(line_delta("x\ny", ""), Some((0, 2)));
        assert_eq!(line_delta("a\nb\nc\nd", "a\nc\nd\ne"), Some((1, 1)));
        assert_eq!(line_delta("a", "b"), Some((1, 1)));
    }

    #[test]
    fn nothing_in_the_summary_names_a_file() {
        let t0 = Instant::now();
        let mut t = Tally::start(t0);
        t.add(&fx("/secret/path/main.rs", Some((1, 0))));
        let s = format!("{:?}", t.summary(t0));
        assert!(!s.contains("secret") && !s.contains("main.rs"), "{s}");
    }
}
