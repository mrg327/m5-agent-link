# BCM4350C0 Bluetooth LE fix for Linux (MacBook Pro 2016–2017)

The built-in Bluetooth of the MacBookPro14,1 (and other Macs with the UART-attached
**BCM4350C0**, `hci_uart` / `hci_bcm`) scans fine on Linux but can never make a Bluetooth LE
connection: BlueZ reports `br-connection-canceled` / "Connect Failed: Disconnected (0x0e)". So
the watch cannot link to such a PC over BLE.

## Cause

The chip runs its ROM firmware (no patch file exists for the C0 revision, on Linux or in Boot
Camp). That ROM sets reserved upper bits in the **event type** of every LE Advertising Report:
`0x20` instead of `0x00` (ADV_IND, connectable), `0x23` instead of `0x03`, `0x24` instead of
`0x04` (SCAN_RSP). The kernel's Bluetooth core accepts only 0–4, logs

```
Bluetooth: hci0: unknown advertising packet type: 0x20
```

and drops the report. BlueZ connects by scanning for the device's advertisement and then sending
*LE Create Connection*; with every report dropped it never sends it and gives up. A `btmon`
capture shows the advertisement arriving with `Event type: Reserved (0x20)` and no LE Create
Connection after it.

## Fix

`0001-hci_bcm-mask-reserved-adv-report-type-bits.patch`: in `hci_bcm`, before an HCI event goes
to the core, each LE Advertising Report whose type is out of range (> 4) keeps only its defined
bits (`& 0x07`). Valid reports from any other Broadcom chip are never changed. With it, the
"unknown advertising packet type" messages stop and LE connections work (verified 2026-11-10 on a
MacBookPro14,1, Ubuntu 24.04, kernel 7.0.0-42-generic: GATT connect to the watch, and the daemon
links over BLE with a 247-byte MTU).

## Install (DKMS)

```sh
sudo tools/linux/bcm4350c0-le-fix/install.sh            # fetch, patch, build, install, reload
sudo tools/linux/bcm4350c0-le-fix/install.sh --remove   # back to the stock driver
```

The script fetches `drivers/bluetooth` from the upstream tag matching the running kernel's base
version (`v7.0` for `7.0.0-42-generic`), applies the patch, and installs `hci_uart` through DKMS
into `updates/dkms`, so it is rebuilt for each new kernel of the same base. After a jump to a new
base version (say 7.1), run the script again. If a build ever fails, the stock driver loads
(Bluetooth works as before, without LE connections).

To try it without installing anything (until the next reboot): build with
`make -C /lib/modules/$(uname -r)/build M=$PWD modules` in a directory holding the patched
`drivers/bluetooth` sources and this `Kbuild`, then `sudo rmmod hci_uart && sudo insmod hci_uart.ko`.

Needs: kernel headers, `dkms`, `git`, network for the fetch, Secure Boot off (or a signed module).
The proper home for this fix is upstream, as a quirk in `hci_bcm.c`.
