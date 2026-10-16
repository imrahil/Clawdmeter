//! The device's System page: this computer's CPU, memory, temperature, disk
//! and uptime → one `{"k":"sys", ...}` write every 30 s. Linux only (from
//! `/proc` and `/sys`); elsewhere [`SUPPORTED`] is false and the feed never
//! runs. The parsers are pure; [`collect`] reads the host.

use std::path::Path;

use serde::Serialize;

use crate::feeds::{fold_ascii, py_round, Schedule};

/// Every key [`SystemPayload`] can emit, for the firmware cross-check.
pub const SYSTEM_KEYS: &[&str] = &["k", "hn", "cpu", "ld", "nc", "mem", "mu", "tmp", "dsk", "du", "up"];

/// The host stats can be read here.
pub const SUPPORTED: bool = cfg!(target_os = "linux");
/// Seconds between readings.
pub const INTERVAL_SECS: i64 = 30;

/// hwmon chip names whose inputs are the CPU package / die temperature.
const CPU_HWMON: &[&str] = &["coretemp", "k10temp", "zenpower", "cpu_thermal", "soc_thermal"];
/// hwmon labels preferred within such a chip.
const CPU_LABELS: &[&str] = &["Package id 0", "Tctl", "Tdie"];

/// One system write.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct SystemPayload {
    /// Always `"sys"`.
    pub k: &'static str,
    /// Host name, up to the first dot (ASCII, ≤ 20).
    pub hn: String,
    /// CPU busy %, over half a second.
    pub cpu: i64,
    /// 1-minute load average, `"0.42"`.
    pub ld: String,
    /// CPU count.
    pub nc: usize,
    /// Memory used %.
    pub mem: i64,
    /// Memory used / total, `"6.1/15 GB"`.
    pub mu: String,
    /// CPU temperature, °C; `-1` = unknown.
    pub tmp: i64,
    /// Root filesystem used %.
    pub dsk: i64,
    /// Root filesystem used / total, `"112/456 GB"`.
    pub du: String,
    /// Uptime, `"3d 4h"`, `"5h 12m"`, `"42m"`.
    pub up: String,
}

impl SystemPayload {
    /// Compact JSON for the RX characteristic.
    pub fn encode(&self) -> Vec<u8> {
        serde_json::to_vec(self).unwrap_or_default()
    }
}

/// When to read.
pub fn schedule() -> Schedule {
    Schedule::every(INTERVAL_SECS, INTERVAL_SECS)
}

/// `/proc/stat`'s first line → (total jiffies, idle + iowait), and the
/// number of `cpuN` lines.
pub fn parse_stat(text: &str) -> Option<((u64, u64), usize)> {
    let first = text.lines().next()?;
    let vals: Vec<u64> = first.split_whitespace().skip(1).filter_map(|v| v.parse().ok()).collect();
    let idle = vals.get(3)? + vals.get(4).copied().unwrap_or(0);
    let cpus = text
        .lines()
        .filter(|l| l.strip_prefix("cpu").is_some_and(|r| r.starts_with(|c: char| c.is_ascii_digit())))
        .count();
    Some(((vals.iter().sum(), idle), cpus))
}

/// CPU busy % between two `(total, idle)` snapshots.
pub fn cpu_percent(a: (u64, u64), b: (u64, u64)) -> i64 {
    let total = b.0.saturating_sub(a.0);
    let idle = b.1.saturating_sub(a.1);
    py_round(100.0 * (1.0 - idle as f64 / total.max(1) as f64))
}

/// `/proc/meminfo` → (used, total) bytes; used = total − available.
pub fn parse_meminfo(text: &str) -> Option<(u64, u64)> {
    let field = |name: &str| {
        text.lines().find_map(|l| {
            let (k, v) = l.split_once(':')?;
            (k == name).then(|| v.split_whitespace().next()?.parse::<u64>().ok().map(|kb| kb * 1024))?
        })
    };
    let total = field("MemTotal")?;
    let avail = field("MemAvailable").or_else(|| field("MemFree")).unwrap_or(0);
    Some((total.saturating_sub(avail), total))
}

/// Bytes as GB, one decimal under 10.
pub fn human_gb(n: u64) -> String {
    let g = n as f64 / 1024f64.powi(3);
    if g < 10.0 {
        format!("{g:.1}")
    } else {
        format!("{g:.0}")
    }
}

/// `"42m"`, `"5h 12m"`, `"3d 4h"`.
pub fn duration_text(secs: f64) -> String {
    let m = (secs / 60.0).floor() as i64;
    if m < 60 {
        format!("{m}m")
    } else if m < 1440 {
        format!("{}h {}m", m / 60, m % 60)
    } else {
        format!("{}d {}h", m / 1440, (m % 1440) / 60)
    }
}

fn read_trim(p: &Path) -> Option<String> {
    std::fs::read_to_string(p).ok().map(|s| s.trim().to_owned())
}

fn sorted_entries(dir: &Path, prefix: &str) -> Vec<std::path::PathBuf> {
    let mut v: Vec<_> = std::fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.file_name().is_some_and(|n| n.to_string_lossy().starts_with(prefix)))
        .collect();
    v.sort();
    v
}

/// °C of the CPU package under a sysfs root (`/sys`): a known hwmon chip
/// first (its package / Tctl / Tdie input, else `temp1`), then the
/// `x86_pkg_temp` thermal zone, else the hottest plausible zone.
pub fn cpu_temp(sys: &Path) -> Option<i64> {
    for hw in sorted_entries(&sys.join("class/hwmon"), "hwmon") {
        let Some(name) = read_trim(&hw.join("name")) else { continue };
        if !CPU_HWMON.contains(&name.as_str()) {
            continue;
        }
        let labelled = sorted_entries(&hw, "temp")
            .into_iter()
            .filter(|p| p.to_string_lossy().ends_with("_label"))
            .find(|p| read_trim(p).is_some_and(|l| CPU_LABELS.contains(&l.as_str())))
            .map(|p| std::path::PathBuf::from(p.to_string_lossy().replace("_label", "_input")));
        let pick = labelled.unwrap_or_else(|| hw.join("temp1_input"));
        if let Some(milli) = read_trim(&pick).and_then(|s| s.parse::<i64>().ok()) {
            return Some(py_round(milli as f64 / 1000.0));
        }
    }
    let mut best: Option<f64> = None;
    for z in sorted_entries(&sys.join("class/thermal"), "thermal_zone") {
        let (Some(kind), Some(t)) =
            (read_trim(&z.join("type")), read_trim(&z.join("temp")).and_then(|s| s.parse::<i64>().ok()))
        else {
            continue;
        };
        let t = t as f64 / 1000.0;
        if kind == "x86_pkg_temp" {
            return Some(py_round(t));
        }
        if 0.0 < t && t < 125.0 {
            best = Some(best.map_or(t, |b| b.max(t)));
        }
    }
    best.map(py_round)
}

/// The pieces read from the host, for [`payload`].
#[derive(Debug, Clone, PartialEq)]
pub struct Readings {
    /// `/proc/sys/kernel/hostname`.
    pub hostname: String,
    /// CPU busy %.
    pub cpu: i64,
    /// 1-minute load average.
    pub load1: f64,
    /// CPU count.
    pub cpus: usize,
    /// Memory (used, total) bytes.
    pub mem: (u64, u64),
    /// CPU °C.
    pub temp: Option<i64>,
    /// Root filesystem (used, total) bytes.
    pub disk: (u64, u64),
    /// Seconds since boot.
    pub uptime: f64,
}

/// The write from the host's readings.
pub fn payload(r: &Readings) -> SystemPayload {
    let (used, total) = r.mem;
    let (d_used, d_total) = r.disk;
    SystemPayload {
        k: "sys",
        hn: fold_ascii(r.hostname.split('.').next().unwrap_or(""), 20),
        cpu: r.cpu,
        ld: format!("{:.2}", r.load1),
        nc: r.cpus.max(1),
        mem: if total > 0 { py_round(used as f64 / total as f64 * 100.0) } else { 0 },
        mu: format!("{}/{} GB", human_gb(used), human_gb(total)),
        tmp: r.temp.unwrap_or(-1),
        dsk: if d_total > 0 { py_round(d_used as f64 / d_total as f64 * 100.0) } else { 0 },
        du: format!("{}/{} GB", human_gb(d_used), human_gb(d_total)),
        up: duration_text(r.uptime),
    }
}

/// (used, total) bytes of the filesystem holding `path`.
#[cfg(target_os = "linux")]
fn disk_usage(path: &str) -> Option<(u64, u64)> {
    let c = std::ffi::CString::new(path).ok()?;
    // SAFETY: `statvfs` only writes into the zeroed struct we hand it; `c`
    // is a valid NUL-terminated string that outlives the call.
    let st = unsafe {
        let mut st: libc::statvfs = std::mem::zeroed();
        if libc::statvfs(c.as_ptr(), &mut st) != 0 {
            return None;
        }
        st
    };
    let frsize = st.f_frsize as u64;
    let total = st.f_blocks as u64 * frsize;
    Some((total.saturating_sub(st.f_bavail as u64 * frsize), total))
}

/// Read the host (blocking: it samples the CPU for half a second, so run it
/// off the daemon's loop).
#[cfg(target_os = "linux")]
pub fn collect() -> Result<SystemPayload, String> {
    let read = |p: &str| std::fs::read_to_string(p).map_err(|e| format!("{p}: {e}"));
    let (t0, cpus) = parse_stat(&read("/proc/stat")?).ok_or("/proc/stat: unreadable")?;
    std::thread::sleep(std::time::Duration::from_millis(500));
    let (t1, _) = parse_stat(&read("/proc/stat")?).ok_or("/proc/stat: unreadable")?;
    let mem = parse_meminfo(&read("/proc/meminfo")?).ok_or("/proc/meminfo: no MemTotal")?;
    let first_num = |s: String| s.split_whitespace().next().and_then(|v| v.parse::<f64>().ok());
    let r = Readings {
        hostname: read("/proc/sys/kernel/hostname").or_else(|_| read("/etc/hostname")).unwrap_or_default(),
        cpu: cpu_percent(t0, t1),
        load1: read("/proc/loadavg").ok().and_then(first_num).unwrap_or(0.0),
        cpus,
        mem,
        temp: cpu_temp(Path::new("/sys")),
        disk: disk_usage("/").unwrap_or((0, 0)),
        uptime: read("/proc/uptime").ok().and_then(first_num).unwrap_or(0.0),
    };
    Ok(payload(&r))
}

/// Not on this OS.
#[cfg(not(target_os = "linux"))]
pub fn collect() -> Result<SystemPayload, String> {
    Err("system stats are read on Linux only".to_owned())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::feeds::WRITE_LIMIT;

    #[test]
    fn stat_and_cpu() {
        let a = "cpu  100 0 100 700 100 0 0 0 0 0\ncpu0 50 0 50 350 50 0 0 0\ncpu1 50 0 50 350 50 0 0 0\nintr 1\n";
        let b = "cpu  150 0 150 800 100 0 0 0 0 0\n";
        let (sa, n) = parse_stat(a).unwrap();
        assert_eq!((sa, n), ((1000, 800), 2));
        let (sb, _) = parse_stat(b).unwrap();
        assert_eq!(cpu_percent(sa, sb), 50);
        assert_eq!(cpu_percent(sa, sa), 100, "no ticks: like the old daemon's `or 1`");
        assert!(parse_stat("").is_none());
    }

    #[test]
    fn meminfo() {
        let t = "MemTotal:       16000000 kB\nMemFree:         1000000 kB\nMemAvailable:    8000000 kB\n";
        assert_eq!(parse_meminfo(t), Some((8_000_000 * 1024, 16_000_000 * 1024)));
        let old = "MemTotal: 2000 kB\nMemFree: 500 kB\n";
        assert_eq!(parse_meminfo(old), Some((1500 * 1024, 2000 * 1024)));
        assert_eq!(parse_meminfo("nothing"), None);
    }

    #[test]
    fn text() {
        assert_eq!(human_gb(6 * 1024u64.pow(3) + 1024u64.pow(3) / 10), "6.1");
        assert_eq!(human_gb(456 * 1024u64.pow(3)), "456");
        assert_eq!(duration_text(59.0 * 60.0 + 59.0), "59m");
        assert_eq!(duration_text(5.0 * 3600.0 + 12.0 * 60.0), "5h 12m");
        assert_eq!(duration_text(3.0 * 86400.0 + 4.0 * 3600.0 + 59.0), "3d 4h");
    }

    #[test]
    fn a_payload() {
        let gb = 1024u64.pow(3);
        let r = Readings {
            hostname: "wörk-station.lan".into(),
            cpu: 12,
            load1: 0.4166,
            cpus: 8,
            mem: (6 * gb, 16 * gb),
            temp: None,
            disk: (112 * gb, 456 * gb),
            uptime: 3.0 * 86400.0 + 3700.0,
        };
        let p = payload(&r);
        assert_eq!(
            String::from_utf8(p.encode()).unwrap(),
            r#"{"k":"sys","hn":"work-station","cpu":12,"ld":"0.42","nc":8,"mem":38,"mu":"6.0/16 GB","tmp":-1,"dsk":25,"du":"112/456 GB","up":"3d 1h"}"#
        );
        // The worst case still fits one write.
        let big = Readings { hostname: "\"".repeat(40), load1: 9999.99, mem: (u64::MAX, u64::MAX), ..r };
        assert!(payload(&big).encode().len() <= WRITE_LIMIT);
    }

    #[test]
    fn temperatures_from_sysfs() {
        let root = std::env::temp_dir().join(format!("cm-sys-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let w = |rel: &str, v: &str| {
            let p = root.join(rel);
            std::fs::create_dir_all(p.parent().unwrap()).unwrap();
            std::fs::write(p, v).unwrap();
        };
        // Only thermal zones: x86_pkg_temp wins, else the hottest plausible.
        w("class/thermal/thermal_zone0/type", "acpitz\n");
        w("class/thermal/thermal_zone0/temp", "45000\n");
        w("class/thermal/thermal_zone1/type", "iwlwifi\n");
        w("class/thermal/thermal_zone1/temp", "130000\n");
        assert_eq!(cpu_temp(&root), Some(45));
        w("class/thermal/thermal_zone2/type", "x86_pkg_temp\n");
        w("class/thermal/thermal_zone2/temp", "51500\n");
        assert_eq!(cpu_temp(&root), Some(52));
        // A CPU hwmon chip beats the zones; its package label beats temp1.
        w("class/hwmon/hwmon0/name", "nvme\n");
        w("class/hwmon/hwmon0/temp1_input", "30000\n");
        w("class/hwmon/hwmon1/name", "coretemp\n");
        w("class/hwmon/hwmon1/temp1_input", "40000\n");
        assert_eq!(cpu_temp(&root), Some(40));
        w("class/hwmon/hwmon1/temp2_label", "Core 0\n");
        w("class/hwmon/hwmon1/temp2_input", "61000\n");
        w("class/hwmon/hwmon1/temp3_label", "Package id 0\n");
        w("class/hwmon/hwmon1/temp3_input", "66400\n");
        assert_eq!(cpu_temp(&root), Some(66));
        assert_eq!(cpu_temp(&root.join("nowhere")), None);
        let _ = std::fs::remove_dir_all(&root);
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn reads_this_host() {
        let p = collect().unwrap();
        assert_eq!(p.k, "sys");
        assert!(p.nc >= 1 && (0..=100).contains(&p.cpu) && (0..=100).contains(&p.mem));
        assert!(p.encode().len() <= WRITE_LIMIT);
    }
}
