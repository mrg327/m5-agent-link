//! Battery helpers shared by the daemon and the CLI (docs/design/POWER.md). Not part of the wire format:
//! the LiPo open-circuit-voltage curve that turns a voltage into a state of charge, and the columns of
//! `~/.agentlink/power-reports.csv`, which the daemon writes and `agentlink power` reads.

/// The report log's file name under the agentlink home. `power.csv` there belongs to another tool.
pub const POWER_CSV: &str = "power-reports.csv";

/// The report log's columns, in order (POWER.md "The report log").
pub const POWER_CSV_COLUMNS: &[&str] = &[
    "time_unix", // when the daemon received the report (unix s)
    "time",      // the same, local time, RFC 3339
    "device",    // the device uid (hex)
    "board",     // "m5stopwatch", "sim", ...
    "fw",        // firmware version from the hello
    "transport", // usb | ble | ws
    "seq",       // report number since the device's power-on (0 = the first)
    "mv",        // battery voltage, last raw reading (mV; 0 = none)
    "mvf",       // battery voltage, filtered as the device's own percent (mV)
    "vin",       // input voltage (mV)
    "chg",       // charging (0/1)
    "usb",       // external power present (0/1)
    "pct",       // the device's own percent (linear 3.3-4.2 V)
    "on_ms",     // display on, full look
    "dim_ms",    // display on, dimmed to the ambient face
    "off_ms",    // display off (standby)
    "deep_ms",   // deep sleep
    "link_ms",   // welcomed by a daemon
    "ble_ms",    // a BLE central connected
    "itvl_us",   // BLE connection interval in force (0 = none)
    "nd",        // deep sleeps that ended in this span
    "ocv_pct",   // state of charge from `mv` by the open-circuit curve below (one decimal)
];

/// Resting (open-circuit) voltage of a single LiPo cell against its state of charge, at about 25 °C:
/// a generic table for LiCoO2 / graphite pouch cells, not measured on this watch's 450 mAh cell. Between
/// points the charge is interpolated linearly.
pub const OCV_CURVE: &[(u32, f64)] = &[
    (3270, 0.0),
    (3610, 5.0),
    (3690, 10.0),
    (3710, 15.0),
    (3730, 20.0),
    (3750, 25.0),
    (3770, 30.0),
    (3790, 35.0),
    (3800, 40.0),
    (3820, 45.0),
    (3840, 50.0),
    (3850, 55.0),
    (3870, 60.0),
    (3910, 65.0),
    (3950, 70.0),
    (3980, 75.0),
    (4020, 80.0),
    (4080, 85.0),
    (4110, 90.0),
    (4150, 95.0),
    (4200, 100.0),
];

/// State of charge in percent (0-100) for a resting cell voltage in mV, from [`OCV_CURVE`]. `None` for 0
/// (no reading).
pub fn ocv_percent(mv: u32) -> Option<f64> {
    if mv == 0 {
        return None;
    }
    let (first, last) = (OCV_CURVE[0], OCV_CURVE[OCV_CURVE.len() - 1]);
    if mv <= first.0 {
        return Some(first.1);
    }
    if mv >= last.0 {
        return Some(last.1);
    }
    let i = OCV_CURVE.iter().position(|&(v, _)| v >= mv)?;
    let ((v0, p0), (v1, p1)) = (OCV_CURVE[i - 1], OCV_CURVE[i]);
    Some(p0 + (p1 - p0) * f64::from(mv - v0) / f64::from(v1 - v0))
}

/// The device's own figure, for comparison: linear from 3.3 V (0 %) to 4.2 V (100 %).
pub fn linear_percent(mv: u32) -> f64 {
    (f64::from(mv.clamp(3300, 4200)) - 3300.0) / 9.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn curve_is_monotonic_and_interpolates() {
        assert!(OCV_CURVE
            .windows(2)
            .all(|w| w[0].0 < w[1].0 && w[0].1 < w[1].1));
        assert_eq!(ocv_percent(0), None);
        assert_eq!(ocv_percent(3000), Some(0.0));
        assert_eq!(ocv_percent(4300), Some(100.0));
        assert_eq!(ocv_percent(3840), Some(50.0));
        let mid = ocv_percent(3860).unwrap();
        assert!((mid - 57.5).abs() < 1e-9, "{mid}");
        // the flat middle of the curve: the linear map reads 3.84 V as 60 %, the cell is nearer half full
        assert!((linear_percent(3840) - 60.0).abs() < 1e-9);
    }

    #[test]
    fn columns_are_unique() {
        let mut c = POWER_CSV_COLUMNS.to_vec();
        c.sort_unstable();
        c.dedup();
        assert_eq!(c.len(), POWER_CSV_COLUMNS.len());
        assert_ne!(POWER_CSV, "power.csv");
    }
}
