//! On macOS, embeds an Info.plist in the `agentlinkd` binary (`__TEXT,__info_plist`). Started from a
//! terminal, the daemon borrows the terminal's Bluetooth grant; started by launchd it is its own
//! responsible process, and macOS kills a process that touches CoreBluetooth without
//! `NSBluetoothAlwaysUsageDescription` instead of asking the user.

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("macos") {
        return;
    }
    let version = std::env::var("CARGO_PKG_VERSION").unwrap_or_default();
    let plist = format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleIdentifier</key><string>dev.agentlink.agentlinkd</string>
  <key>CFBundleName</key><string>agentlinkd</string>
  <key>CFBundleVersion</key><string>{version}</string>
  <key>CFBundleShortVersionString</key><string>{version}</string>
  <key>NSBluetoothAlwaysUsageDescription</key><string>agentlinkd talks to the AgentLink watch over Bluetooth LE.</string>
  <key>NSAppleEventsUsageDescription</key><string>agentlinkd brings the agent's terminal window to the front when you ask from the watch.</string>
</dict>
</plist>
"#
    );
    let out = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap()).join("Info.plist");
    std::fs::write(&out, plist).unwrap();
    println!(
        "cargo:rustc-link-arg-bins=-Wl,-sectcreate,__TEXT,__info_plist,{}",
        out.display()
    );
}
