#!/bin/sh
# Runs agentlinkd as a per-user launchd agent on macOS: started at login, restarted if it exits.
#
#   daemon/tools/launchd.sh cert        # once: a local code-signing certificate (asks for your password)
#   daemon/tools/launchd.sh install     # after `cargo install --path crates/agentlinkd`
#   daemon/tools/launchd.sh uninstall
#   daemon/tools/launchd.sh status
#
# install re-signs the binary (identifier dev.agentlink.agentlinkd) so the Info.plist that build.rs
# embeds is bound to it, then writes ~/Library/LaunchAgents/dev.agentlink.agentlinkd.plist with the
# current PATH (the daemon runs kitty, osascript, caffeinate, ioreg …) and any AGENTLINK_* variables
# set now. Under launchd the daemon needs its own Bluetooth and Accessibility (trackpad) grants.
# macOS keeps a grant for the binary's designated requirement. An ad hoc signature's requirement is
# the hash of that one build, so every rebuild loses the grants, although System Settings still
# shows them switched on. `cert` makes a self-signed code-signing certificate in the login keychain
# and trusts it for code signing; install then signs with it, the requirement becomes "this
# identifier, this certificate", and grants survive rebuilds. Without it install signs ad hoc.
# Run install after every `cargo install`. Log: ~/.agentlink/agentlinkd.log.
set -eu

LABEL=dev.agentlink.agentlinkd
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
BIN="${AGENTLINKD:-$HOME/.cargo/bin/agentlinkd}"
LOG="${AGENTLINK_HOME:-$HOME/.agentlink}/agentlinkd.log"
DOMAIN="gui/$(id -u)"
CERT="agentlink local code signing"

xml() { printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'; }

# SHA-1 of the valid (trusted) signing identity named $CERT, or nothing.
identity() { security find-identity -v -p codesigning 2>/dev/null | awk -v n="\"$CERT\"" 'index($0, n) { print $2; exit }'; }

case "${1:-}" in
cert)
  [ "$(uname -s)" = Darwin ] || { echo "launchd.sh: macOS only" >&2; exit 2; }
  if [ -n "$(identity)" ]; then
    echo "launchd.sh: \"$CERT\" is already there; run install"
    exit 0
  fi
  KC="$HOME/Library/Keychains/login.keychain-db"
  TMP=$(mktemp -d)
  trap 'rm -rf "$TMP"' EXIT
  cat >"$TMP/cert.cnf" <<EOF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $CERT
[ext]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
EOF
  # /usr/bin/openssl (LibreSSL) writes a PKCS#12 that `security import` reads; OpenSSL 3+ does not.
  PASS=$(/usr/bin/openssl rand -hex 16)
  /usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -config "$TMP/cert.cnf" \
    -keyout "$TMP/key.pem" -out "$TMP/cert.pem" 2>/dev/null
  /usr/bin/openssl pkcs12 -export -inkey "$TMP/key.pem" -in "$TMP/cert.pem" -name "$CERT" \
    -passout "pass:$PASS" -out "$TMP/id.p12"
  security import "$TMP/id.p12" -k "$KC" -P "$PASS" -T /usr/bin/codesign >/dev/null
  echo "launchd.sh: trusting \"$CERT\" for code signing (macOS asks for your password)"
  security add-trusted-cert -r trustRoot -p codeSign -k "$KC" "$TMP/cert.pem"
  [ -n "$(identity)" ] || { echo "launchd.sh: the certificate is not usable for code signing" >&2; exit 1; }
  echo "launchd.sh: done. Run install, then allow agentlinkd once more (Bluetooth, Accessibility); later rebuilds keep the grants"
  ;;
install)
  [ "$(uname -s)" = Darwin ] || { echo "launchd.sh: macOS only" >&2; exit 2; }
  [ -x "$BIN" ] || { echo "launchd.sh: $BIN not found (cargo install --path crates/agentlinkd)" >&2; exit 1; }
  ID=$(identity)
  if [ -n "$ID" ]; then
    codesign --force --sign "$ID" --identifier "$LABEL" "$BIN"
  else
    codesign --force --sign - --identifier "$LABEL" "$BIN"
    echo "launchd.sh: signed ad hoc, so macOS forgets the Bluetooth and Accessibility grants on every rebuild;" \
      "run \"$0 cert\" once to keep them" >&2
  fi
  mkdir -p "$(dirname "$PLIST")" "$(dirname "$LOG")"
  {
    cat <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array><string>$(xml "$BIN")</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>10</integer>
  <key>ProcessType</key><string>Interactive</string>
  <key>StandardOutPath</key><string>$(xml "$LOG")</string>
  <key>StandardErrorPath</key><string>$(xml "$LOG")</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key><string>$(xml "$PATH")</string>
    <key>RUST_LOG</key><string>$(xml "${RUST_LOG:-agentlinkd=info}")</string>
EOF
    env | grep '^AGENTLINK_' | while IFS='=' read -r k v; do
      printf '    <key>%s</key><string>%s</string>\n' "$k" "$(xml "$v")"
    done
    cat <<EOF
  </dict>
</dict>
</plist>
EOF
  } >"$PLIST"
  plutil -lint "$PLIST" >/dev/null
  launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
  if pgrep -qx agentlinkd; then
    echo "launchd.sh: another agentlinkd is running (pid $(pgrep -x agentlinkd | tr '\n' ' ')); stop it first" >&2
    exit 1
  fi
  launchctl bootstrap "$DOMAIN" "$PLIST"
  echo "installed $PLIST; log: $LOG"
  ;;
uninstall)
  launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
  rm -f "$PLIST"
  echo "removed $LABEL"
  ;;
status)
  launchctl print "$DOMAIN/$LABEL" 2>/dev/null | grep -E '^\s+(state|pid|last exit code|runs) =' || echo "$LABEL is not loaded"
  ;;
*)
  echo "usage: $0 cert|install|uninstall|status" >&2
  exit 2
  ;;
esac
