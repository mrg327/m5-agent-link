# Power report: where the battery goes

Issue 20261113-ee04. Protocol 0.19 ([PROTOCOL.md](PROTOCOL.md) §6f). Code: `components/core/src/power_report.c`
(accounting and cadence), `boards/stopwatch/board.cpp` and `link_ble.cpp` (voltages, deep-sleep time, BLE interval,
Battery Level notify), `daemon/crates/agentlinkd/src/power.rs` (the log), `daemon/crates/agentlink/src/power.rs`
(`agentlink power`), `daemon/crates/agentlink-proto/src/battery.rs` (the curve and the log's columns).

The watch cannot measure current. The PM1 gives the battery voltage, the input voltage and a charging flag
(`sw_pm1_read`). The watch reports those voltages together with the time it spent in each power state. The daemon
logs every report, and `agentlink power` fits a drain rate per state from many of them. The baseline before this
work was a once-a-minute BlueZ poll of the Battery Level on 2026-11-12: about 25 %/h awake and linked, and about
1.5 %/h in deep sleep. Both figures are coarse, and they are percent on the linear map.

## What the watch reports

`power` (device → daemon, type 27) carries these fields:

| Field | Meaning |
|---|---|
| `mv` | The last raw battery reading (mV). |
| `mvf` | The filtered reading that the watch's own percent comes from (mV). |
| `vin` | The input voltage (mV). |
| `chg`, `usb` | Charging, and external power present. |
| `pct` | The watch's own percent: linear from 3.3 V (0 %) to 4.2 V (100 %). |
| `on` | ms with the display on at full look. This includes the alert glow and every screen except the dimmed HOME. |
| `dim` | ms on the dimmed ambient face: HOME after 30 s without input, with nothing waiting (`ui_dimmed()`). |
| `off` | ms with the display off (standby). |
| `deep` | ms in deep sleep. The RX8130 crystal clock measures it across the wake reboot, or the chip's RTC timer when the clock was never set. |
| `nd` | Deep sleeps that ended. |
| `lk` | ms welcomed by a daemon. |
| `ble` | ms with a BLE central connected. |
| `itvl` | The BLE connection interval in force (µs; 0 = none). |
| `seq` | The report number since power-on. |

`on + dim + off + deep` is the time the report covers. About a second of boot per wake is counted nowhere.

**Cadence and cost.** A report goes out right after a welcome from a daemon with proto ≥ 0.19, then every 5 minutes
while the link stays up. A refused send is tried again 30 s later. Nothing goes out while no daemon listens; the
figures keep adding up. Before a deep sleep the unsent figures are written to RTC memory
(`hal_retained_words()`, 9 words), and the next boot takes them back. A power-on or reset loses them, and `seq`
then starts again at 0. The accounting runs first in every core step (one subtraction and three adds). The battery
is read at least once a minute, with a 2-second cache, and every report is about 60 bytes. Nothing in this adds a
wakeup: the core loop already polls every 50 ms with the display off.

**Battery Level (0x2A19)** is now read + notify. The watch notifies a subscribed central when its own percent
changes by 1 or more. That is the linear figure, sampled at least once a minute. The NimBLE host task reads only a
stored byte and never the PM1. A BlueZ logger can subscribe instead of polling.

## The report log

`agentlinkd` appends one row per report to `~/.agentlink/power-reports.csv` (`AGENTLINK_HOME` moves it). It writes
the header once, when the file is new or empty. It never touches `~/.agentlink/power.csv`, which belongs to the
user's own logger. It also keeps the last report of each device for `agentlink status`. These are the columns
(`POWER_CSV_COLUMNS`):

| Column | Meaning |
|---|---|
| `time_unix`, `time` | When the daemon received the report (unix s, and local RFC 3339). |
| `device`, `board`, `fw`, `transport` | The uid (hex), board, firmware from the hello, and `usb` / `ble` / `ws`. |
| `seq` | As in the report. |
| `mv`, `mvf`, `vin` | mV. |
| `chg`, `usb` | 0 or 1. |
| `pct` | The watch's linear percent. |
| `on_ms`, `dim_ms`, `off_ms`, `deep_ms`, `link_ms`, `ble_ms` | The times in the report. |
| `itvl_us`, `nd` | As in the report. |
| `ocv_pct` | The state of charge from `mv` by the curve below, to one decimal. |

## `agentlink power`

```
agentlink power [--csv PATH] [--device HEXPREFIX] [--hours H | --all] [--capacity-mah 450] [--json]
```

- **Battery now**: the last report. It shows the volts, the open-circuit estimate, the watch's own percent, the
  supply, the age and the BLE interval.
- **Window**: by default, the reports since the last unplug or charge (the trailing run of rows with `usb` = 0 and
  `chg` = 0). On USB now, it uses the last run on battery. `--hours H` and `--all` take battery rows only. The
  device defaults to the newest that is not a simulator.
- **Segments**: consecutive reports (`seq` + 1) form a segment. A lost report, a reset or a stretch on USB starts a
  new one. Time inside a segment is the device's own time (the sum of the state times), not wall-clock time.
- **Overall rate**: the least-squares slope of the state of charge against time, pooled within segments. It is
  given in %/h and mV/h, and in mA at the capacity.
- **Per state**: SoC_k = a_segment − Σ r_s · T_s(k), where T_s is the cumulative hours in state s. The fit removes
  each segment's intercept, then solves non-negative least squares exactly, by trying every subset of the states.
  The output gives each rate ± its standard error, mA, and the drain the state accounts for in the window.
- **When it will not fit**, it says so:
  - a state with under 10 minutes in the window is "too little time";
  - the whole fit needs the charge to have moved at least 1.5 points;
  - it needs 3 or more residual degrees of freedom;
  - a rate whose standard error is larger than the rate is "not resolved".
- States that always occur in the same proportion cannot be told apart. For example, every interval might be 5 s
  on and the rest off. Real use varies enough. A dedicated run, such as an hour face down on battery, separates
  them quickly.

**The curve.** This is a generic open-circuit table for a single LiCoO2 / graphite pouch cell at about 25 °C,
interpolated linearly (`OCV_CURVE`). It is not measured on this cell. Its points, in V and %:

| V | % | V | % | V | % |
|---|---|---|---|---|---|
| 4.20 | 100 | 3.91 | 65 | 3.77 | 30 |
| 4.15 | 95 | 3.87 | 60 | 3.75 | 25 |
| 4.11 | 90 | 3.85 | 55 | 3.73 | 20 |
| 4.08 | 85 | 3.84 | 50 | 3.71 | 15 |
| 4.02 | 80 | 3.82 | 45 | 3.69 | 10 |
| 3.98 | 75 | 3.80 | 40 | 3.61 | 5 |
| 3.95 | 70 | 3.79 | 35 | 3.27 | 0 |

The linear map reads 3.84 V as 60 %, but the cell is nearer half full. Between 3.75 V and 3.85 V the curve is
flat, so a few mV there move the estimate by several points.

## Accuracy limits

- **Voltage only.** The rates are percentage points of an estimated state of charge per hour. The mA figure assumes
  450 mAh (`--capacity-mah`) and a curve that fits this cell. An aged cell has less capacity and a different curve.
- **Sag under load.** The raw reading is taken with the CPU awake. While the display is on, the cell sits tens of
  mV below its resting voltage, so a report sent with the display on reads low. The intervals are short, and the
  fit spreads this error, but the "on" rate is somewhat overstated after a burst of use. Resting readings, with the
  display off, are the trustworthy ones. The filtered value (`mvf`) is an exponential average whose time constant
  depends on how often something reads the battery: about 80 s with the display on, about 8 min off. It is
  logged, but the fit uses the raw reading.
- **Temperature.** Cell voltage drops in the cold, and a warm wrist reads higher than a desk. The curve is for
  about 25 °C.
- **Resolution and noise.** The fit works on whole mV. At about 10 mV per percentage point in the middle of the
  curve, a 5-minute interval in standby moves the voltage by less than the noise. Standby and deep rates therefore
  need hours of data. A small rate shows a large standard error until then.
- **Deep-sleep time.** The crystal clock counts whole seconds; the internal RC timer, used when the clock was never
  set, drifts by a few percent. About a second of boot and reconnect per wake is not counted, and the 90 s reconnect
  window after a timer wake counts as `off`.
- **Lost figures.** A reset (PM1 button, panic, brownout) loses the unsent times since the last report. The next
  report has `seq` 0, and the analysis starts a new segment.
- **Charging.** Rows on USB or charging are excluded. The first report after an unplug starts the window.

## What the report will settle (open decisions)

The issue leaves two constants in `core/power.c` to the user, and this work does not change them:

- `POWER_TIMER_WAKES` (off): when on, a watch with no host wakes after 5, 10, 20, 40, 60, 60 and 60 minutes. Each
  wake is a boot, up to 90 s awake with the display off looking for the host, then deep sleep again.
- `DEEP_AFTER_MS` (10 min): how long the display stays off, unlinked on battery, before deep sleep.

With per-state rates from the report, the cost of each option is arithmetic:

- **Timer wakes** cost about 7 × 90 s × r_off (plus seven boots) over the 4-hour ladder, against the time the watch
  would otherwise stay in deep sleep.
- **A longer `DEEP_AFTER_MS`** costs Δt × (r_off − r_deep) for every unlinked stretch.

With the coarse baseline (r_off unknown, r_deep about 1.5 %/h, awake about 25 %/h), the timer ladder costs about
7 × 1.5 min × r_off. At r_off = 3 %/h that is about 0.5 points per unlinked night. The report's `off` and `deep`
rates replace these guesses.

A deep-sleep rate of 1.5 %/h (about 7 mA) is far above the under 0.1 mA estimated in FIRMWARE.md §4. If the fit
confirms it, something stays powered in deep sleep: the panel and touch rail (kept up for tap-to-wake), IOE1, the
BMI270 left in its 25 Hz mode, or the PM1 LED. That is worth its own issue.

## Checks

- **Simulator.** `ctest`: `power_report_logic` is the unit test of accounting, cadence, refused sends, unlinked
  time, the RTC words and the once-a-minute battery read. The script tests are `sim_power_report_cadence` (BLE,
  interval), `sim_power_report_old_daemon` (none to 0.18), `sim_power_report_states` (on, dim, off),
  `sim_power_report_deep` (`--wake button --slept 7200`) and `sim_power_report_unlinked`.
- **Daemon.** `cargo test`: CSV rows and header in `agentlinkd` (`power::tests`), and the device link test
  `power_reports_are_logged_with_the_device_and_kept_for_status`. In `agentlink`, `power::tests` covers parsing,
  windows, segments, the fit on synthetic data and the not-enough-data reasons.
- **End to end.** `python3 daemon/tools/power_e2e.py` runs with the standard library only, on its own port and
  home: a fake watch and a fake simulator, then the CSV, `agentlink status` and `agentlink power`, compared against
  the synthetic truth.
- **Not verified on hardware yet**: the Battery Level notification with BlueZ, the deep-sleep time across a real
  wake, and the voltages under real load.
