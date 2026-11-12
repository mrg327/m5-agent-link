//! `agentlink shell-init zsh|bash`: the snippet that reports a shell's commands to the daemon
//! (docs/design/JOBS.md §4). It calls `agentlink-hook shell …` fully detached, so a command is
//! never delayed and the shell prints no job-control noise; the shim never reads stdin and
//! swallows every error. Nothing happens when `agentlink-hook` is not on PATH, `end` goes out
//! only for a command whose `start` did, and `$?` is what the user's prompt would have seen.

use anyhow::Result;

const ZSH: &str = r#"# agentlink shell integration (agentlink shell-init zsh)
if [[ -o interactive ]] && (( $+commands[agentlink-hook] )) && [[ -z "${_agentlink_loaded-}" ]]; then
  typeset -g _agentlink_loaded=1
  typeset -gi _agentlink_started=0 _agentlink_seq=0
  _agentlink_preexec() {
    # a line the history will not keep is not reported at all (zsh passes it here regardless)
    [[ -o histignorespace && $1 == ' '* ]] && return
    _agentlink_started=1
    _agentlink_seq=$(( _agentlink_seq + 1 ))
    agentlink-hook shell start --pid $$ --seq $_agentlink_seq -- "$1" </dev/null >/dev/null 2>&1 &!
  }
  _agentlink_precmd() {
    local _agentlink_st=$?
    if (( _agentlink_started )); then
      _agentlink_started=0
      agentlink-hook shell end --pid $$ --seq $_agentlink_seq --exit $_agentlink_st </dev/null >/dev/null 2>&1 &!
    fi
    return $_agentlink_st
  }
  _agentlink_zshexit() {
    # zsh also runs zshexit in a subshell that calls exit, as in `(exit 7)`: that is not this shell
    (( ZSH_SUBSHELL )) && return
    agentlink-hook shell exit --pid $$ </dev/null >/dev/null 2>&1 &!
  }
  autoload -Uz add-zsh-hook
  add-zsh-hook preexec _agentlink_preexec
  add-zsh-hook precmd _agentlink_precmd
  add-zsh-hook zshexit _agentlink_zshexit
  # first, so that $? is still the command's own status
  precmd_functions=(_agentlink_precmd ${precmd_functions:#_agentlink_precmd})
fi
"#;

const BASH: &str = r#"# agentlink shell integration (agentlink shell-init bash)
case $- in
*i*)
  if [ -z "${_agentlink_loaded-}" ] && command -v agentlink-hook >/dev/null 2>&1; then
    _agentlink_loaded=1
    _agentlink_started=0 # a start went out for the command now running
    _agentlink_seq=0     # counts reported lines; start and end of a line carry the same one,
                         # so the detached calls may arrive in any order
    _agentlink_hn=       # history number and command of the last reported line
    _agentlink_hc=
    _agentlink_armed=    # the next DEBUG is the first command of a fresh command line
    _agentlink_prev_debug=
    _agentlink_prev_exit=
    _agentlink_hn=$(HISTTIMEFORMAT= builtin history 1 2>/dev/null)
    _agentlink_hn=${_agentlink_hn#"${_agentlink_hn%%[![:space:]]*}"}
    _agentlink_hn=${_agentlink_hn%%[!0-9]*}
    _agentlink_send() { ( agentlink-hook shell "$@" </dev/null & ) >/dev/null 2>&1; }
    _agentlink_debug() {
      # not for completion functions, PROMPT_COMMAND, or later commands of the same line
      if [ -n "$_agentlink_armed" ] && [ -z "${COMP_LINE-}" ]; then
        _agentlink_armed=
        case $BASH_COMMAND in
        _agentlink_*) ;;
        *)
          local _agentlink_c _agentlink_n
          _agentlink_c=$(HISTTIMEFORMAT= builtin history 1 2>/dev/null)
          _agentlink_c=${_agentlink_c#"${_agentlink_c%%[![:space:]]*}"}
          _agentlink_n=${_agentlink_c%%[!0-9]*}
          # same history number, other command: the line was kept out of the history
          # (HISTCONTROL=ignorespace), so it is not reported. A repeat dropped by ignoredups has
          # the same number and the same command, and is.
          if [ -n "$_agentlink_n" ] && [ "$_agentlink_n" = "$_agentlink_hn" ] &&
            [ "$BASH_COMMAND" != "$_agentlink_hc" ]; then
            :
          else
            _agentlink_c=${_agentlink_c#"${_agentlink_c%%[!0-9]*}"}
            _agentlink_c=${_agentlink_c#\*}
            _agentlink_c=${_agentlink_c#"${_agentlink_c%%[![:space:]]*}"}
            # a line that does not hold the running command is not the one typed (aliases)
            case $_agentlink_c in *"$BASH_COMMAND"*) ;; *) _agentlink_c=$BASH_COMMAND ;; esac
            _agentlink_hn=$_agentlink_n
            _agentlink_hc=$BASH_COMMAND
            _agentlink_started=1
            _agentlink_seq=$((_agentlink_seq + 1))
            _agentlink_send start --pid $$ --seq $_agentlink_seq -- "$_agentlink_c"
          fi
          ;;
        esac
      fi
      if [ -n "$_agentlink_prev_debug" ]; then eval "$_agentlink_prev_debug"; fi
    }
    _agentlink_prompt() {
      local _agentlink_st=$?
      if [ "$_agentlink_started" = 1 ]; then
        _agentlink_started=0
        _agentlink_send end --pid $$ --seq $_agentlink_seq --exit $_agentlink_st
      fi
      return $_agentlink_st
    }
    _agentlink_arm() {
      local _agentlink_st=$?
      _agentlink_armed=1
      return $_agentlink_st
    }
    _agentlink_exit() {
      _agentlink_send exit --pid $$
      if [ -n "$_agentlink_prev_exit" ]; then eval "$_agentlink_prev_exit"; fi
    }
    # what the user's traps already do keeps running after ours
    eval "_agentlink_t=($(trap -p DEBUG))"
    _agentlink_prev_debug=${_agentlink_t[2]-}
    eval "_agentlink_t=($(trap -p EXIT))"
    _agentlink_prev_exit=${_agentlink_t[2]-}
    unset _agentlink_t
    trap '_agentlink_debug' DEBUG
    trap '_agentlink_exit' EXIT
    # ours bracket the user's: first sees the command's status, last arms the next DEBUG
    if [[ $(declare -p PROMPT_COMMAND 2>/dev/null) == "declare -a"* ]]; then
      PROMPT_COMMAND=(_agentlink_prompt "${PROMPT_COMMAND[@]}" _agentlink_arm)
    else
      PROMPT_COMMAND="_agentlink_prompt${PROMPT_COMMAND:+;$PROMPT_COMMAND};_agentlink_arm"
    fi
  fi
  ;;
esac
"#;

/// The snippet for `shell`, if there is one.
pub fn snippet(shell: &str) -> Option<&'static str> {
    match shell {
        "zsh" => Some(ZSH),
        "bash" => Some(BASH),
        _ => None,
    }
}

pub fn run(shell: &str) -> Result<()> {
    match snippet(shell) {
        Some(s) => {
            print!("{s}");
            Ok(())
        }
        None => {
            eprintln!("agentlink: shell-init supports zsh and bash, not '{shell}'");
            std::process::exit(2);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;
    use std::process::{Command, Stdio};

    fn has(shell: &str) -> bool {
        Command::new(shell)
            .arg("--version")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .is_ok()
    }

    /// A scratch dir with a fake `agentlink-hook` that appends its arguments to `log`.
    fn rig(name: &str) -> (PathBuf, PathBuf) {
        let d = std::env::temp_dir().join(format!("agentlink-shell-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(d.join("bin")).unwrap();
        let log = d.join("log");
        let hook = d.join("bin/agentlink-hook");
        std::fs::write(
            &hook,
            format!("#!/bin/sh\nprintf '%s\\n' \"$*\" >> '{}'\n", log.display()),
        )
        .unwrap();
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(&hook, std::fs::Permissions::from_mode(0o755)).unwrap();
        }
        (d, log)
    }

    /// Runs `script` in an interactive `shell` with the snippet loaded; returns (stdout, hook log).
    fn drive(
        shell: &str,
        name: &str,
        pre: &str,
        script: &str,
        path_has_hook: bool,
    ) -> (String, String) {
        let (d, log) = rig(name);
        std::fs::write(d.join("init.sh"), snippet(shell).unwrap()).unwrap();
        let path = if path_has_hook {
            format!("{}:/usr/bin:/bin", d.join("bin").display())
        } else {
            "/usr/bin:/bin".into()
        };
        let mut c = Command::new(shell);
        match shell {
            "bash" => c.args(["--norc", "--noprofile", "-i"]),
            _ => c.args(["-f", "-i"]),
        };
        let mut child = c
            .env("PATH", path)
            .env("HOME", &d)
            .env("PS1", "")
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();
        use std::io::Write;
        let src = format!(
            "{pre}\nsource {}\n{script}\nexit\n",
            d.join("init.sh").display()
        );
        child
            .stdin
            .take()
            .unwrap()
            .write_all(src.as_bytes())
            .unwrap();
        let out = child.wait_with_output().unwrap();
        // the hook calls are detached, and the first run of a new script can be slow (macOS scans
        // it): wait until the log has stopped growing
        let (quiet, cap) = (
            std::time::Duration::from_millis(400),
            std::time::Duration::from_secs(10),
        );
        let t0 = std::time::Instant::now();
        let mut logged = String::new();
        let mut since = std::time::Instant::now();
        while t0.elapsed() < cap {
            std::thread::sleep(std::time::Duration::from_millis(50));
            let now = std::fs::read_to_string(&log).unwrap_or_default();
            if now != logged {
                logged = now;
                since = std::time::Instant::now();
            } else if since.elapsed() >= quiet && (!logged.is_empty() || !path_has_hook) {
                break;
            }
        }
        let _ = std::fs::remove_dir_all(&d);
        (String::from_utf8_lossy(&out.stdout).into_owned(), logged)
    }

    /// `["start -- echo hi", "end --exit 0", "exit"]`, sorted (the calls are detached, so their
    /// order in the file is not guaranteed); the pid is dropped.
    fn lines(log: &str) -> Vec<String> {
        let mut v: Vec<String> = log
            .lines()
            .filter_map(|l| {
                let (ev, rest) = l.strip_prefix("shell ")?.split_once(" --pid ")?;
                let rest = rest.split_once(' ').map_or("", |x| x.1);
                // `--seq N` is checked by `seqs`
                let rest = match rest.strip_prefix("--seq ") {
                    Some(r) => r.split_once(' ').map_or("", |x| x.1),
                    None => rest,
                };
                Some(format!("{ev} {rest}").trim_end().to_owned())
            })
            .collect();
        v.sort();
        v
    }

    /// (event, seq) of every start and end, sorted.
    fn seqs(log: &str) -> Vec<(String, u64)> {
        let mut v: Vec<(String, u64)> = log
            .lines()
            .filter_map(|l| {
                let w: Vec<&str> = l.split(' ').collect();
                let i = w.iter().position(|x| *x == "--seq")?;
                Some((w[1].to_owned(), w[i + 1].parse().ok()?))
            })
            .collect();
        v.sort_by_key(|(e, n)| (*n, e.clone()));
        v
    }

    fn check(shell: &str) {
        if !has(shell) {
            eprintln!("skipped: {shell} is not installed");
            return;
        }
        let (out, log) = drive(
            shell,
            &format!("{shell}-flow"),
            "",
            "echo hello world\nfalse\necho \"st=$?\"\ntrue; (exit 7)\necho \"st=$?\"\nenv X=1 sleep 0.01 && echo two",
            true,
        );
        assert!(out.contains("hello world"), "{shell}: {out}");
        assert!(
            out.contains("st=1"),
            "{shell}: $? changed by the integration: {out}"
        );
        assert!(out.contains("st=7"), "{shell}: $? changed: {out}");
        let l = lines(&log);
        // one start per command line (not per simple command) and an end for each; the final
        // `exit` starts but never ends, and the shell reports its exit once
        let count = |p: &str| l.iter().filter(|x| x.starts_with(p)).count();
        assert_eq!(
            (count("start"), count("end"), count("exit")),
            (7, 6, 1),
            "{shell}: {l:?}"
        );
        assert!(l.iter().any(|x| x == "start -- exit"), "{l:?}");
        for w in [
            "start -- echo hello world",
            "start -- false",
            "start -- true; (exit 7)",
            "start -- env X=1 sleep 0.01 && echo two",
            "end --exit 1",
            "end --exit 7",
        ] {
            assert!(l.iter().any(|x| x == w), "{shell}: missing '{w}' in {l:?}");
        }
        assert_eq!(count("start -- echo \"st=$?\""), 2, "{l:?}");
        // a line's start and end carry the same seq; seqs count up from 1 (the final `exit`
        // has a start only)
        let mut want: Vec<(String, u64)> = (1..=6)
            .flat_map(|n| [("end".to_owned(), n), ("start".to_owned(), n)])
            .collect();
        want.push(("start".into(), 7));
        want.sort_by_key(|(e, n)| (*n, e.clone()));
        assert_eq!(seqs(&log), want, "{shell}");
    }

    #[test]
    fn bash_reports_every_command_line_once_and_keeps_status() {
        check("bash");
    }

    /// A line the history keeps out is not reported: no start, so no end.
    fn check_hidden(shell: &str, pre: &str) {
        if !has(shell) {
            eprintln!("skipped: {shell} is not installed");
            return;
        }
        let (_, log) = drive(
            shell,
            &format!("{shell}-hidden"),
            pre,
            "echo one\n echo hidden\necho one\necho two",
            true,
        );
        let l = lines(&log);
        let starts: Vec<&String> = l
            .iter()
            .filter(|x| x.starts_with("start -- echo"))
            .collect();
        assert_eq!(
            starts,
            [
                "start -- echo one",
                "start -- echo one",
                "start -- echo two"
            ],
            "{shell}: {l:?}"
        );
        assert!(!log.contains("hidden"), "{log}");
        // the three reported lines have an end each (seq 1..=3), the hidden one none
        let q = seqs(&log);
        for n in 1..=3 {
            assert!(q.contains(&("end".to_owned(), n)), "{shell}: {q:?}");
        }
        assert!(!q.contains(&("end".to_owned(), 4)), "{shell}: {q:?}");
    }

    #[test]
    fn bash_does_not_report_lines_kept_out_of_the_history() {
        check_hidden("bash", "HISTCONTROL=ignoreboth");
    }

    #[test]
    fn zsh_does_not_report_lines_kept_out_of_the_history() {
        check_hidden("zsh", "setopt histignorespace");
    }

    #[test]
    fn zsh_reports_every_command_line_once_and_keeps_status() {
        check("zsh");
    }

    #[test]
    fn without_the_shim_the_snippet_does_nothing() {
        for shell in ["bash", "zsh"] {
            if !has(shell) {
                continue;
            }
            let (out, log) = drive(
                shell,
                &format!("{shell}-nohook"),
                "",
                "false\necho \"st=$?\"",
                false,
            );
            assert!(out.contains("st=1"), "{shell}: {out}");
            assert!(log.is_empty(), "{shell}: {log}");
        }
    }

    #[test]
    fn snippets_exist_only_for_zsh_and_bash() {
        assert!(snippet("zsh").unwrap().contains("add-zsh-hook preexec"));
        assert!(snippet("bash").unwrap().contains("PROMPT_COMMAND"));
        assert!(snippet("fish").is_none());
    }
}
