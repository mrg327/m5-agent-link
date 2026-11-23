# tools

Standalone developer tools, each with its own README, run from the repo root. A helper for one
component lives in that component's `tools/`; a repo-wide entry point lives in
[`scripts/`](../scripts/README.md) (structure.md S-NEW-06).

| Path | What it is |
|---|---|
| [`board/`](board/README.md) | The design board: a local page and server that talk to a running Claude Code session ([BOARD.md](../docs/design/BOARD.md)) |
| [`linux/bcm4350c0-le-fix/`](linux/bcm4350c0-le-fix/README.md) | A DKMS patch to `hci_bcm` so the BCM4350C0 Bluetooth in some MacBook Pros can make BLE connections on Linux |

`board/` is dependency-free Node; follow [js.md](../docs/conventions/js.md). The Linux fix is kernel
C built by DKMS and is not covered by `scripts/check`.
