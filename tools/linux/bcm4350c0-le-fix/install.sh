#!/usr/bin/env bash
# Builds and installs hci_uart with the BCM4350C0 LE fix through DKMS (see README.md). Run as root:
#   sudo tools/linux/bcm4350c0-le-fix/install.sh            install for the running kernel
#   sudo tools/linux/bcm4350c0-le-fix/install.sh --remove   back to the stock driver
# It fetches drivers/bluetooth for this kernel's upstream base (e.g. v7.0 for 7.0.0-42-generic),
# applies the patch, registers the module with DKMS (rebuilt on kernel updates) and reloads it.
set -euo pipefail
NAME=hci-uart-bcm4350c0
VER=1.0
SRC=/usr/src/$NAME-$VER
here="$(cd "$(dirname "$0")" && pwd)"
[[ $EUID -eq 0 ]] || { echo "run as root (sudo)"; exit 1; }

reload() {
    modprobe -r hci_uart 2>/dev/null || rmmod hci_uart || true
    modprobe hci_uart
    echo "hci_uart reloaded: $(cat /sys/module/hci_uart/srcversion)"
}

if [[ ${1:-} == --remove ]]; then
    dkms remove -m $NAME -v $VER --all 2>/dev/null || true
    rm -rf "$SRC"
    depmod -a
    reload
    echo "stock hci_uart restored"
    exit 0
fi

base="v$(uname -r | cut -d. -f1,2)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
echo "fetching drivers/bluetooth from linux $base"
git clone -q --depth 1 --filter=blob:none --sparse --branch "$base" https://github.com/torvalds/linux "$work/linux"
git -C "$work/linux" sparse-checkout set drivers/bluetooth
git -C "$work/linux" apply "$here"/0001-*.patch
rm -rf "$SRC" && mkdir -p "$SRC"
cp "$work/linux/drivers/bluetooth/"*.[ch] "$SRC/"
cp "$here/Kbuild" "$here/dkms.conf" "$SRC/"
dkms remove -m $NAME -v $VER --all 2>/dev/null || true
dkms add -m $NAME -v $VER
dkms build -m $NAME -v $VER
dkms install -m $NAME -v $VER --force
depmod -a
reload
echo "installed: $(modinfo -n hci_uart)"
