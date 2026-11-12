#!/bin/sh
# Runs agentlinkd as a per-user launchd agent on macOS: started at login, restarted if it exits.
#
#   daemon/tools/launchd.sh install     # after `cargo install --path crates/agentlinkd`
#   daemon/tools/launchd.sh uninstall
#   daemon/tools/launchd.sh status
#
# install re-signs the binary (ad hoc, identifier dev.agentlink.agentlinkd) so the Info.plist that
# build.rs embeds is bound to it, then writes ~/Library/LaunchAgents/dev.agentlink.agentlinkd.plist
# with the current PATH (the daemon runs kitty, osascript, caffeinate, ioreg …) and any AGENTLINK_*
# variables set now. Under launchd the daemon needs its own Bluetooth grant: macOS asks once, and
# again after each rebuild (an ad hoc signature changes with the binary), so run install after
# every `cargo install`. Log: ~/.agentlink/agentlinkd.log.
set -eu

LABEL=dev.agentlink.agentlinkd
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
BIN="${AGENTLINKD:-$HOME/.cargo/bin/agentlinkd}"
LOG="${AGENTLINK_HOME:-$HOME/.agentlink}/agentlinkd.log"
DOMAIN="gui/$(id -u)"

xml() { printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'; }

case "${1:-}" in
install)
  [ "$(uname -s)" = Darwin ] || { echo "launchd.sh: macOS only" >&2; exit 2; }
  [ -x "$BIN" ] || { echo "launchd.sh: $BIN not found (cargo install --path crates/agentlinkd)" >&2; exit 1; }
  codesign --force --sign - --identifier "$LABEL" "$BIN"
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
  echo "usage: $0 install|uninstall|status" >&2
  exit 2
  ;;
esac
