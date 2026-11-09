//! Saved remotes (`~/.agentlink/remotes.json`): a name for an ssh destination, the remote OS,
//! and what to run on connect. `agentlink ssh <name>` uses them; `agentlink remote setup <name>`
//! onboards a Windows remote (key, token, hook, hooks config, `AGENTLINK_REMOTE`).

use anyhow::{bail, Context, Result};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

pub const DEFAULT_PORT: u16 = 47533;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize, clap::ValueEnum)]
#[serde(rename_all = "lowercase")]
pub enum Os {
    #[default]
    Windows,
    Unix,
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Profile {
    /// ssh destination (`user@host` or an ssh config alias).
    pub dest: String,
    #[serde(default)]
    pub os: Os,
    /// Directory to start in on the remote.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub dir: Option<String>,
    /// Command run on connect (PowerShell on Windows, the login shell on Unix). The ssh
    /// session ends when it exits.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cmd: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub port: Option<u16>,
    /// Extra ssh options, placed before the destination (e.g. `-i`, `-o Key=Value`, `-p`).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub ssh_args: Vec<String>,
}

impl std::fmt::Display for Os {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Os::Windows => "windows",
            Os::Unix => "unix",
        })
    }
}

impl Profile {
    pub fn port(&self) -> u16 {
        self.port.unwrap_or(DEFAULT_PORT)
    }
}

#[derive(Debug, Default, Serialize, Deserialize)]
struct Store {
    #[serde(default)]
    remotes: BTreeMap<String, Profile>,
}

fn store_path(home: &Path) -> PathBuf {
    home.join("remotes.json")
}

fn load(home: &Path) -> Result<Store> {
    let p = store_path(home);
    match std::fs::read_to_string(&p) {
        Ok(s) => serde_json::from_str(&s).with_context(|| format!("cannot parse {}", p.display())),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(Store::default()),
        Err(e) => Err(e).with_context(|| format!("cannot read {}", p.display())),
    }
}

fn save(home: &Path, st: &Store) -> Result<()> {
    std::fs::create_dir_all(home).with_context(|| format!("cannot create {}", home.display()))?;
    let p = store_path(home);
    let tmp = p.with_extension("json.tmp");
    let mut o = std::fs::OpenOptions::new();
    o.write(true).create(true).truncate(true);
    #[cfg(unix)]
    std::os::unix::fs::OpenOptionsExt::mode(&mut o, 0o600);
    let mut f = o
        .open(&tmp)
        .with_context(|| format!("cannot write {}", tmp.display()))?;
    std::io::Write::write_all(
        &mut f,
        (serde_json::to_string_pretty(st)? + "\n").as_bytes(),
    )?;
    drop(f);
    std::fs::rename(&tmp, &p)?;
    Ok(())
}

fn check_name(name: &str) -> Result<()> {
    let ok = !name.is_empty()
        && name.len() <= 32
        && name
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_');
    if !ok {
        bail!("remote name must be 1-32 characters of A-Z a-z 0-9 - _");
    }
    Ok(())
}

/// A saved remote by name, if `name` is one.
pub fn lookup(name: &str) -> Result<Option<Profile>> {
    if check_name(name).is_err() {
        return Ok(None);
    }
    Ok(load(&crate::home())?.remotes.remove(name))
}

/// Fields for `remote add` / `remote edit`. For `edit`, `None` keeps the stored value and an
/// empty `dir`/`cmd` clears it.
#[derive(Debug, Default, clap::Args)]
pub struct Fields {
    /// Remote OS (decides how the on-connect command and setup run).
    #[arg(long, value_enum)]
    pub os: Option<Os>,
    /// Directory to start in on the remote ("" clears it).
    #[arg(long)]
    pub dir: Option<String>,
    /// Command to run on connect, e.g. `copilot` ("" clears it). The session ends when it exits.
    #[arg(long)]
    pub cmd: Option<String>,
    /// Remote loopback port for the tunnel (default 47533).
    #[arg(long)]
    pub port: Option<u16>,
    /// Extra ssh option, repeatable (e.g. --ssh-arg=-i --ssh-arg=~/.ssh/other). Replaces the
    /// stored list.
    #[arg(long = "ssh-arg", allow_hyphen_values = true)]
    pub ssh_args: Vec<String>,
}

fn apply(p: &mut Profile, f: Fields) {
    let opt = |s: String| if s.is_empty() { None } else { Some(s) };
    if let Some(os) = f.os {
        p.os = os;
    }
    if let Some(d) = f.dir {
        p.dir = opt(d);
    }
    if let Some(c) = f.cmd {
        p.cmd = opt(c);
    }
    if f.port.is_some() {
        p.port = f.port;
    }
    if !f.ssh_args.is_empty() {
        p.ssh_args = f.ssh_args;
    }
}

pub fn add(json: bool, name: &str, dest: &str, force: bool, f: Fields) -> Result<()> {
    check_name(name)?;
    let home = crate::home();
    let mut st = load(&home)?;
    if st.remotes.contains_key(name) && !force {
        bail!("remote '{name}' exists; use `agentlink remote edit {name}` or --force");
    }
    let mut p = Profile {
        dest: dest.to_owned(),
        ..Default::default()
    };
    apply(&mut p, f);
    st.remotes.insert(name.to_owned(), p.clone());
    save(&home, &st)?;
    report(json, "saved", name, &p);
    Ok(())
}

pub fn edit(json: bool, name: &str, dest: Option<String>, f: Fields) -> Result<()> {
    let home = crate::home();
    let mut st = load(&home)?;
    let Some(p) = st.remotes.get_mut(name) else {
        bail!("no remote named '{name}' (agentlink remote list)");
    };
    if let Some(d) = dest {
        p.dest = d;
    }
    apply(p, f);
    let p = p.clone();
    save(&home, &st)?;
    report(json, "updated", name, &p);
    Ok(())
}

pub fn rm(json: bool, name: &str) -> Result<()> {
    let home = crate::home();
    let mut st = load(&home)?;
    if st.remotes.remove(name).is_none() {
        bail!("no remote named '{name}'");
    }
    save(&home, &st)?;
    if json {
        println!("{}", serde_json::json!({"ok": true, "removed": name}));
    } else {
        println!("removed {name}");
    }
    Ok(())
}

pub fn list(json: bool) -> Result<()> {
    let st = load(&crate::home())?;
    if json {
        println!("{}", serde_json::to_string(&st.remotes)?);
        return Ok(());
    }
    if st.remotes.is_empty() {
        println!("no saved remotes (agentlink remote add NAME USER@HOST --cmd copilot)");
    }
    for (n, p) in &st.remotes {
        println!("{n}\n{}", describe(p));
    }
    Ok(())
}

fn describe(p: &Profile) -> String {
    let mut s = format!("  dest  {}\n  os    {}\n  port  {}", p.dest, p.os, p.port());
    s.push_str(&format!(
        "\n  dir   {}\n  cmd   {}",
        p.dir.as_deref().unwrap_or("-"),
        p.cmd.as_deref().unwrap_or("- (interactive shell)")
    ));
    if !p.ssh_args.is_empty() {
        s.push_str(&format!("\n  ssh   {}", p.ssh_args.join(" ")));
    }
    s
}

fn report(json: bool, what: &str, name: &str, p: &Profile) {
    if json {
        println!(
            "{}",
            serde_json::json!({"ok": true, what: name, "remote": p})
        );
    } else {
        println!("{what} remote {name}\n{}", describe(p));
        println!("connect with: agentlink ssh {name}");
    }
}

// ---- connect -------------------------------------------------------------------------------

/// Remote command line that starts in `dir` and runs `cmd`, or `None` for a plain shell.
pub fn remote_command(p: &Profile) -> Option<String> {
    if p.cmd.is_none() && p.dir.is_none() {
        return None;
    }
    match p.os {
        Os::Windows => {
            // Win32-OpenSSH's default shell may be cmd or PowerShell; -EncodedCommand is
            // parsed the same by both, so nothing in the script needs shell quoting.
            // Progress records otherwise leak to the terminal as CLIXML.
            let mut s = format!(
                "$ProgressPreference = 'SilentlyContinue'\n$env:AGENTLINK_REMOTE = 'tcp:127.0.0.1:{}'\n",
                p.port()
            );
            if let Some(d) = &p.dir {
                s.push_str(&format!("Set-Location -LiteralPath {}\n", ps_quote(d)));
            }
            match &p.cmd {
                Some(c) => s.push_str(&format!("{c}\nexit $LASTEXITCODE\n")),
                None => s.push_str("powershell -NoLogo\n"),
            }
            Some(format!(
                "powershell -NoLogo -EncodedCommand {}",
                ps_encode(&s)
            ))
        }
        Os::Unix => {
            let cd = p
                .dir
                .as_deref()
                .map(|d| format!("cd -- {} && ", sh_path(d)))
                .unwrap_or_default();
            let env = format!("export AGENTLINK_REMOTE=tcp:127.0.0.1:{}; ", p.port());
            Some(match &p.cmd {
                // An interactive login shell, so tools that rc files put on PATH (nvm) resolve.
                // The env is set again inside it because rc files (setup's line) run after.
                Some(c) => format!(
                    "{env}{cd}exec \"${{SHELL:-sh}}\" -lic {}",
                    sh_quote(&format!("{env}{c}"))
                ),
                None => format!("{env}{cd}exec \"${{SHELL:-sh}}\" -l"),
            })
        }
    }
}

/// ssh args for `agentlink ssh <name>`: (before dest, after dest).
pub fn connect_args(p: &Profile, no_cmd: bool, extra: &[String]) -> (Vec<String>, Vec<String>) {
    let mut pre = p.ssh_args.clone();
    let mut post = extra.to_vec();
    if !no_cmd && extra.is_empty() {
        if let Some(rc) = remote_command(p) {
            pre.insert(0, "-t".into());
            post.push(rc);
        }
    }
    (pre, post)
}

fn ps_quote(s: &str) -> String {
    format!("'{}'", s.replace('\'', "''"))
}

fn sh_quote(s: &str) -> String {
    format!("'{}'", s.replace('\'', r"'\''"))
}

/// Quotes a path for sh, keeping a leading `~` meaning the remote home.
fn sh_path(d: &str) -> String {
    match d {
        "~" => "\"$HOME\"".into(),
        _ => match d.strip_prefix("~/") {
            Some(rest) => format!("\"$HOME\"/{}", sh_quote(rest)),
            None => sh_quote(d),
        },
    }
}

/// `powershell -EncodedCommand` payload: base64 of the UTF-16LE script.
fn ps_encode(script: &str) -> String {
    let bytes: Vec<u8> = script.encode_utf16().flat_map(u16::to_le_bytes).collect();
    base64(&bytes)
}

fn base64(b: &[u8]) -> String {
    const T: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut o = String::with_capacity(b.len().div_ceil(3) * 4);
    for c in b.chunks(3) {
        let n = (c[0] as u32) << 16
            | (*c.get(1).unwrap_or(&0) as u32) << 8
            | *c.get(2).unwrap_or(&0) as u32;
        for i in 0..4 {
            if i <= c.len() {
                o.push(T[(n >> (18 - 6 * i) & 63) as usize] as char);
            } else {
                o.push('=');
            }
        }
    }
    o
}

// ---- setup (onboarding) --------------------------------------------------------------------

#[cfg(unix)]
pub fn setup(name: &str, hook_exe: Option<PathBuf>, key: Option<PathBuf>) -> Result<()> {
    let p = lookup(name)?.with_context(|| format!("no remote named '{name}'"))?;
    let key = key.unwrap_or_else(|| {
        PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".ssh/id_ed25519.pub")
    });
    match p.os {
        Os::Windows => setup_windows(&p, hook_exe, &key)?,
        Os::Unix => setup_unix(&p, hook_exe, &key)?,
    }
    println!("{name} is set up. Connect with: agentlink ssh {name}");
    Ok(())
}

#[cfg(unix)]
fn ensure_token(home: &Path) -> Result<PathBuf> {
    let token = home.join("remote.token");
    if !token.is_file() {
        crate::remote::write_token_quiet(home)?;
        eprintln!("created {}", token.display());
    }
    Ok(token)
}

#[cfg(unix)]
fn read_key(key: &Path) -> Result<String> {
    let k = std::fs::read_to_string(key)
        .with_context(|| format!("cannot read {} (use --key)", key.display()))?;
    Ok(k.trim().to_owned())
}

#[cfg(unix)]
fn setup_windows(p: &Profile, hook_exe: Option<PathBuf>, key: &Path) -> Result<()> {
    let home = crate::home();
    let exe = hook_exe.unwrap_or_else(|| default_hook_exe(&home));
    if !exe.is_file() {
        bail!(
            "no Windows hook at {}\nbuild it with:\n  cargo xwin build --release -p agentlink-hook --target x86_64-pc-windows-msvc\nthen pass --hook-exe PATH, or copy it to that path",
            exe.display()
        );
    }
    let token = ensure_token(&home)?;

    eprintln!("[1/4] key login to {}", p.dest);
    if run_ps(p, "'ok'", true).is_err() {
        let k = read_key(key)?;
        eprintln!(
            "      no key login yet: installing {} (enter the Windows password once)",
            key.display()
        );
        let out = run_ps(p, &install_key_script(&k), false)?;
        eprintln!("      {}", out.trim());
        run_ps(p, "'ok'", true).context("key login still fails after installing the key")?;
    }

    eprintln!("[2/4] preparing folders");
    let profile_dir = run_ps(p, PREP_SCRIPT, true)?.trim().to_owned();
    if profile_dir.is_empty() {
        bail!("remote did not report its profile folder");
    }

    eprintln!("[3/4] copying hook and token");
    scp(p, &exe, ".agentlink/bin/agentlink-hook.exe")?;
    scp(p, &token, ".agentlink/remote.token")?;

    eprintln!("[4/4] token permissions, AGENTLINK_REMOTE, Copilot hooks");
    let hook_path = format!(r"{profile_dir}\.agentlink\bin\agentlink-hook.exe");
    let hooks = serde_json::to_string_pretty(&crate::hooks::copilot_with(&hook_path))?;
    let out = run_ps(p, &finish_script(p.port(), &hooks), true)?;
    print_indented(&out);
    Ok(())
}

#[cfg(unix)]
fn setup_unix(p: &Profile, hook_exe: Option<PathBuf>, key: &Path) -> Result<()> {
    let home = crate::home();
    let token = ensure_token(&home)?;

    eprintln!("[1/4] key login to {}", p.dest);
    if run_sh(p, "true", true).is_err() {
        let k = read_key(key)?;
        eprintln!(
            "      no key login yet: installing {} (enter the password once)",
            key.display()
        );
        let out = run_sh(p, &unix_install_key_script(&k), false)?;
        print_indented(&out);
        run_sh(p, "true", true).context("key login still fails after installing the key")?;
    }

    eprintln!("[2/4] preparing folders");
    let out = run_sh(p, UNIX_PREP_SCRIPT, true)?;
    let mut lines = out.lines().map(str::trim);
    let (remote_home, arch) = match (lines.next(), lines.next()) {
        (Some(h), Some(a)) if h.starts_with('/') => (h.to_owned(), a.to_owned()),
        _ => bail!("remote did not report its home folder and machine type"),
    };
    let exe = hook_exe.unwrap_or_else(|| default_unix_hook(&home, &arch));
    if !exe.is_file() {
        bail!(
            "no Linux hook for {arch} at {}\nbuild it with:\n  cargo zigbuild --release -p agentlink-hook --target {}-unknown-linux-musl\nthen pass --hook-exe PATH, or copy it to that path",
            exe.display(),
            arch
        );
    }

    eprintln!("[3/4] copying hook and token");
    scp(p, &exe, ".agentlink/bin/agentlink-hook")?;
    scp(p, &token, ".agentlink/remote.token")?;

    eprintln!("[4/4] token permissions, AGENTLINK_REMOTE, Copilot hooks");
    let hook_path = format!("{remote_home}/.agentlink/bin/agentlink-hook");
    let hooks = serde_json::to_string_pretty(&crate::hooks::copilot_with(&hook_path))?;
    let out = run_sh(p, &unix_finish_script(p.port(), &hooks), true)?;
    print_indented(&out);
    Ok(())
}

#[cfg(unix)]
fn print_indented(out: &str) {
    for l in out.lines().filter(|l| !l.trim().is_empty()) {
        eprintln!("      {}", l.trim());
    }
}

#[cfg(not(unix))]
pub fn setup(_name: &str, _hook_exe: Option<PathBuf>, _key: Option<PathBuf>) -> Result<()> {
    bail!("run this on the Mac that runs agentlinkd")
}

/// Where `setup` looks for the cross-compiled hook by default.
pub fn default_hook_exe(home: &Path) -> PathBuf {
    home.join("dist/windows-x64/agentlink-hook.exe")
}

/// Default Linux hook for a remote whose `uname -m` is `arch`.
pub fn default_unix_hook(home: &Path, arch: &str) -> PathBuf {
    let a = match arch {
        "x86_64" | "amd64" => "x64",
        "aarch64" | "arm64" => "arm64",
        other => other,
    };
    home.join(format!("dist/linux-{a}/agentlink-hook"))
}

/// Prints `$HOME` and `uname -m`; makes an existing token writable so scp can replace it.
const UNIX_PREP_SCRIPT: &str = r#"set -e
mkdir -p "$HOME/.agentlink/bin" "$HOME/.copilot/hooks"
chmod 700 "$HOME/.agentlink"
[ ! -f "$HOME/.agentlink/remote.token" ] || chmod 600 "$HOME/.agentlink/remote.token"
printf '%s\n%s\n' "$HOME" "$(uname -m)"
"#;

/// The env line is tagged so a re-run replaces it instead of adding another.
fn unix_finish_script(port: u16, hooks_json: &str) -> String {
    format!(
        r#"set -e
chmod 755 "$HOME/.agentlink/bin/agentlink-hook"
chmod 400 "$HOME/.agentlink/remote.token"
line='export AGENTLINK_REMOTE=tcp:127.0.0.1:{port} # agentlink-remote'
for f in "$HOME/.profile" "$HOME/.bashrc" "$HOME/.zshrc"; do
  [ -f "$f" ] || [ "$f" = "$HOME/.profile" ] || continue
  touch "$f"
  grep -v '# agentlink-remote$' "$f" > "$f.agentlink" || true
  cat "$f.agentlink" > "$f" && rm -f "$f.agentlink"
  printf '%s
' "$line" >> "$f"
done
printf '%s
' {json} > "$HOME/.copilot/hooks/agentlink.json"
echo "token readable only by $(id -un); AGENTLINK_REMOTE=tcp:127.0.0.1:{port} (login shells); hooks -> ~/.copilot/hooks/agentlink.json"
"#,
        json = sh_quote(hooks_json)
    )
}

fn unix_install_key_script(key: &str) -> String {
    format!(
        r#"set -e
umask 077
mkdir -p "$HOME/.ssh"
touch "$HOME/.ssh/authorized_keys"
k={k}
grep -qxF "$k" "$HOME/.ssh/authorized_keys" || printf '%s
' "$k" >> "$HOME/.ssh/authorized_keys"
echo "key added for $(id -un)"
"#,
        k = sh_quote(key)
    )
}

const PREP_SCRIPT: &str = r#"$ProgressPreference = 'SilentlyContinue'
$a = Join-Path $env:USERPROFILE '.agentlink'
New-Item -Force -ItemType Directory (Join-Path $a 'bin'), (Join-Path $env:USERPROFILE '.copilot\hooks') | Out-Null
$t = Join-Path $a 'remote.token'
if (Test-Path $t) { icacls $t /grant:r "$($env:USERNAME):F" | Out-Null }
$env:USERPROFILE
"#;

fn finish_script(port: u16, hooks_json: &str) -> String {
    format!(
        r#"$ProgressPreference = 'SilentlyContinue'
$t = Join-Path $env:USERPROFILE '.agentlink\remote.token'
icacls $t /inheritance:r /grant:r "$($env:USERNAME):R" | Out-Null
setx AGENTLINK_REMOTE 'tcp:127.0.0.1:{port}' | Out-Null
$j = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('{b64}'))
[IO.File]::WriteAllText((Join-Path $env:USERPROFILE '.copilot\hooks\agentlink.json'), $j)
'token readable only by ' + $env:USERNAME + '; AGENTLINK_REMOTE=tcp:127.0.0.1:{port} (new shells); hooks -> .copilot\hooks\agentlink.json'
"#,
        b64 = base64(hooks_json.as_bytes())
    )
}

/// Adds the key for this user, and for admins also to administrators_authorized_keys (the
/// only file Win32-OpenSSH reads for members of Administrators).
fn install_key_script(key: &str) -> String {
    format!(
        r#"$ProgressPreference = 'SilentlyContinue'
$k = {k}
$d = Join-Path $env:USERPROFILE '.ssh'
New-Item -Force -ItemType Directory $d | Out-Null
$f = Join-Path $d 'authorized_keys'
if (-not ((Test-Path $f) -and (Select-String -Quiet -SimpleMatch $k $f))) {{ Add-Content $f $k }}
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrators')
if ($admin) {{
  $af = 'C:\ProgramData\ssh\administrators_authorized_keys'
  if (-not ((Test-Path $af) -and (Select-String -Quiet -SimpleMatch $k $af))) {{ Add-Content $af $k }}
  icacls $af /inheritance:r /grant 'Administrators:F' /grant 'SYSTEM:F' | Out-Null
  'admin account: key added to ' + $af
}} else {{ 'key added for ' + $env:USERNAME }}
"#,
        k = ps_quote(key)
    )
}

fn ssh_bin() -> std::ffi::OsString {
    std::env::var_os("AGENTLINK_SSH").unwrap_or_else(|| "ssh".into())
}

/// Runs a PowerShell script on the remote and returns its stdout. `batch` forbids password
/// prompts.
#[cfg(unix)]
fn run_ps(p: &Profile, script: &str, batch: bool) -> Result<String> {
    let mut c = std::process::Command::new(ssh_bin());
    if batch {
        c.args(["-o", "BatchMode=yes"]);
    }
    c.args(["-o", "ConnectTimeout=15"])
        .args(&p.ssh_args)
        .arg(&p.dest)
        .arg(format!(
            "powershell -NoProfile -NonInteractive -EncodedCommand {}",
            ps_encode(script)
        ))
        .stdin(if batch {
            std::process::Stdio::null()
        } else {
            std::process::Stdio::inherit()
        });
    let o = c.output().context("cannot run ssh")?;
    if !o.status.success() {
        bail!("{}", clean_stderr(&String::from_utf8_lossy(&o.stderr)));
    }
    Ok(String::from_utf8_lossy(&o.stdout).into_owned())
}

/// Runs a POSIX sh script on the remote and returns its stdout. `batch` forbids password
/// prompts.
#[cfg(unix)]
fn run_sh(p: &Profile, script: &str, batch: bool) -> Result<String> {
    let mut c = std::process::Command::new(ssh_bin());
    if batch {
        c.args(["-o", "BatchMode=yes"]);
    }
    c.args(["-o", "ConnectTimeout=15"])
        .args(&p.ssh_args)
        .arg(&p.dest)
        .arg(format!("sh -c {}", sh_quote(script)))
        .stdin(if batch {
            std::process::Stdio::null()
        } else {
            std::process::Stdio::inherit()
        });
    let o = c.output().context("cannot run ssh")?;
    if !o.status.success() {
        bail!("{}", String::from_utf8_lossy(&o.stderr).trim());
    }
    Ok(String::from_utf8_lossy(&o.stdout).into_owned())
}

#[cfg(unix)]
fn scp(p: &Profile, src: &Path, rel_dest: &str) -> Result<()> {
    let bin = std::env::var_os("AGENTLINK_SCP").unwrap_or_else(|| "scp".into());
    let st = std::process::Command::new(&bin)
        .args(["-q", "-o", "BatchMode=yes"])
        .args(scp_args(&p.ssh_args))
        .arg(src)
        .arg(format!("{}:{rel_dest}", p.dest))
        .status()
        .context("cannot run scp")?;
    if !st.success() {
        bail!("scp {} -> {rel_dest} failed", src.display());
    }
    Ok(())
}

/// ssh options as scp takes them: scp spells the port `-P`.
fn scp_args(ssh_args: &[String]) -> Vec<String> {
    ssh_args
        .iter()
        .map(|a| if a == "-p" { "-P".into() } else { a.clone() })
        .collect()
}

/// PowerShell 5 writes errors to a non-interactive ssh as CLIXML; keep just the text.
fn clean_stderr(s: &str) -> String {
    if !s.contains("#< CLIXML") {
        return s.trim().to_owned();
    }
    let mut out = String::new();
    for part in s.split("<S S=\"Error\">").skip(1) {
        if let Some(t) = part.split("</S>").next() {
            out.push_str(
                &t.replace("_x000D__x000A_", "\n")
                    .replace("&amp;", "&")
                    .replace("&lt;", "<")
                    .replace("&gt;", ">"),
            );
        }
    }
    out.trim().to_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn decode_ps(enc: &str) -> String {
        const T: &str = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        let mut bits = 0u32;
        let mut n = 0;
        let mut bytes = Vec::new();
        for c in enc.chars().filter(|&c| c != '=') {
            bits = bits << 6 | T.find(c).unwrap() as u32;
            n += 6;
            if n >= 8 {
                n -= 8;
                bytes.push((bits >> n) as u8);
            }
        }
        let u: Vec<u16> = bytes
            .chunks(2)
            .map(|c| u16::from_le_bytes([c[0], c[1]]))
            .collect();
        String::from_utf16(&u).unwrap()
    }

    #[test]
    fn base64_matches_rfc4648() {
        assert_eq!(base64(b""), "");
        assert_eq!(base64(b"f"), "Zg==");
        assert_eq!(base64(b"fo"), "Zm8=");
        assert_eq!(base64(b"foo"), "Zm9v");
        assert_eq!(base64(b"foobar"), "Zm9vYmFy");
    }

    #[test]
    fn windows_command_cds_runs_and_exits_with_its_code() {
        let p = Profile {
            dest: "u@h".into(),
            dir: Some(r"C:\work\it's".into()),
            cmd: Some("copilot".into()),
            ..Default::default()
        };
        let rc = remote_command(&p).unwrap();
        let enc = rc
            .strip_prefix("powershell -NoLogo -EncodedCommand ")
            .unwrap();
        assert_eq!(
            decode_ps(enc),
            "$ProgressPreference = 'SilentlyContinue'\n$env:AGENTLINK_REMOTE = 'tcp:127.0.0.1:47533'\nSet-Location -LiteralPath 'C:\\work\\it''s'\ncopilot\nexit $LASTEXITCODE\n"
        );
    }

    #[test]
    fn unix_command_quotes_dir() {
        let p = Profile {
            os: Os::Unix,
            dir: Some("/w/it's".into()),
            cmd: Some("copilot".into()),
            ..Default::default()
        };
        assert_eq!(
            remote_command(&p).unwrap(),
            r#"export AGENTLINK_REMOTE=tcp:127.0.0.1:47533; cd -- '/w/it'\''s' && exec "${SHELL:-sh}" -lic 'export AGENTLINK_REMOTE=tcp:127.0.0.1:47533; copilot'"#
        );
        let home = Profile {
            os: Os::Unix,
            dir: Some("~/rfm_virt".into()),
            ..Default::default()
        };
        assert_eq!(
            remote_command(&home).unwrap(),
            r#"export AGENTLINK_REMOTE=tcp:127.0.0.1:47533; cd -- "$HOME"/'rfm_virt' && exec "${SHELL:-sh}" -l"#
        );
        let bare = Profile {
            os: Os::Unix,
            ..Default::default()
        };
        assert_eq!(remote_command(&bare), None);
    }

    #[test]
    fn unix_setup_scripts_parse_and_quote_values() {
        let hooks = r#"{"exec": "/home/it's/.agentlink/bin/agentlink-hook"}"#;
        for script in [
            UNIX_PREP_SCRIPT.to_owned(),
            unix_finish_script(47999, hooks),
            unix_install_key_script("ssh-ed25519 AAAA it's@mac"),
        ] {
            let st = std::process::Command::new("sh")
                .args(["-n", "-c", &script])
                .status()
                .unwrap();
            assert!(st.success(), "{script}");
        }
        let fin = unix_finish_script(47999, hooks);
        assert!(fin.contains(r#"'{"exec": "/home/it'\''s/.agentlink/bin/agentlink-hook"}'"#));
        assert!(fin.contains("tcp:127.0.0.1:47999 # agentlink-remote"));
        assert_eq!(
            default_unix_hook(Path::new("/h"), "x86_64"),
            Path::new("/h/dist/linux-x64/agentlink-hook")
        );
    }

    #[test]
    fn connect_args_add_tty_and_command_unless_overridden() {
        let p = Profile {
            dest: "u@h".into(),
            cmd: Some("copilot".into()),
            ssh_args: vec!["-p".into(), "2222".into()],
            ..Default::default()
        };
        let (pre, post) = connect_args(&p, false, &[]);
        assert_eq!(pre, ["-t", "-p", "2222"]);
        assert_eq!(post.len(), 1);
        let (pre, post) = connect_args(&p, true, &[]);
        assert_eq!((pre, post), (vec!["-p".to_owned(), "2222".into()], vec![]));
        let (pre, post) = connect_args(&p, false, &["dir".to_owned()]);
        assert_eq!(
            (pre, post),
            (vec!["-p".to_owned(), "2222".into()], vec!["dir".to_owned()])
        );
    }

    #[test]
    fn store_roundtrip_edit_and_names() {
        let dir = std::env::temp_dir().join(format!("alk-remotes-{}", std::process::id()));
        let mut st = Store::default();
        let mut p = Profile {
            dest: "u@h".into(),
            ..Default::default()
        };
        apply(
            &mut p,
            Fields {
                cmd: Some("copilot".into()),
                port: Some(4000),
                ..Default::default()
            },
        );
        st.remotes.insert("speech".into(), p);
        save(&dir, &st).unwrap();
        let mut back = load(&dir).unwrap();
        let p = back.remotes.get_mut("speech").unwrap();
        assert_eq!(
            (p.cmd.as_deref(), p.port(), p.os),
            (Some("copilot"), 4000, Os::Windows)
        );
        apply(
            p,
            Fields {
                cmd: Some(String::new()),
                ..Default::default()
            },
        );
        assert_eq!((p.cmd.clone(), p.port()), (None, 4000));
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let mode = std::fs::metadata(store_path(&dir))
                .unwrap()
                .permissions()
                .mode();
            assert_eq!(mode & 0o777, 0o600);
        }
        std::fs::remove_dir_all(&dir).ok();
        assert!(check_name("speech-1_a").is_ok());
        for bad in ["", "a b", "u@h", "x/y", &"n".repeat(33)] {
            assert!(check_name(bad).is_err(), "{bad}");
        }
    }

    #[test]
    fn setup_scripts_embed_values_safely() {
        let s = finish_script(4000, "{\"a\":\"'\"}");
        assert!(s.contains("tcp:127.0.0.1:4000"));
        assert!(s.contains(&base64(b"{\"a\":\"'\"}")));
        let k = install_key_script("ssh-ed25519 AAAA me@mac");
        assert!(k.contains("$k = 'ssh-ed25519 AAAA me@mac'"));
        assert_eq!(
            scp_args(&["-p".into(), "22".into(), "-i".into(), "k".into()]),
            ["-P", "22", "-i", "k"]
        );
        let e = clean_stderr(
            "#< CLIXML\n<Objs><S S=\"Error\">boom &amp; bust_x000D__x000A_</S></Objs>",
        );
        assert_eq!(e, "boom & bust");
    }
}
