//! The device's Pace page: one `{"k":"h", ...}` write after each fresh usage
//! poll: the session's % across its 5-hour window, where it is headed at
//! this pace, and the weekly % added on each of the last 7 days.
//!
//! The usage sources only report the current utilisation, so the app keeps
//! its own record: one `[epoch, session %, weekly %]` line per poll in
//! `<state>/usage-history.jsonl`, pruned to 8 days. Only subscription
//! accounts (`acct` `pro`) have the windows; spend-capped ones get no write.
//! Local-time text (the ETA clock, the weekday buckets) takes the UTC
//! offset and hour format as parameters. Everything but [`history`] is pure.

use std::fs;
use std::io::{self, BufRead, Write as _};
use std::path::Path;

use serde::Serialize;
use serde_json::Value;

use crate::feeds::py_round;
use crate::payload::Payload;

/// Every key [`PacePayload`] can emit, for the firmware cross-check.
pub const PACE_KEYS: &[&str] = &["k", "sh", "fr", "pr", "pz", "eta", "wd", "wp"];

/// Samples older than this are pruned (s).
pub const HISTORY_KEEP: i64 = 8 * 86_400;
/// A sample this soon after the last one is skipped (s).
const MIN_GAP: i64 = 30;
/// The 5-hour window (min).
const SESSION_MIN: i64 = 300;
/// The weekly window (min).
const WEEK_MIN: i64 = 7 * 1440;
/// Sparkline slots across the 5-hour window (10 min each).
pub const SESS_POINTS: usize = 30;
/// "At this pace" = the last 30 minutes...
const RATE_SPAN: f64 = 30.0 * 60.0;
/// ...once there are at least 10 of them.
const RATE_MIN_SPAN: f64 = 10.0 * 60.0;
const DAY_NAMES: [&str; 7] = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];

/// One poll: `(unix seconds, session %, weekly %)`.
pub type Sample = (i64, f64, f64);

/// One pace write. `None` fields are omitted.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct PacePayload {
    /// Always `"h"`.
    pub k: &'static str,
    /// Session % per 10-minute slot of the window so far (≤ 30 values).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sh: Option<Vec<i64>>,
    /// % of the 5-hour window elapsed.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub fr: Option<i64>,
    /// Projected session % at the reset, at this pace (≤ 999).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pr: Option<i64>,
    /// Projection zone: 0 under 80 %, 1 under 100 %, 2 at or over.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pz: Option<u8>,
    /// Local clock time the session hits 100 % at this pace, `"14:35"` or
    /// `"2:35 PM"`; only when it will before the reset.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub eta: Option<String>,
    /// Weekly % added on each of the last 7 local days, today last:
    /// `[day, pct]`, `-1` for a day with no samples.
    pub wd: Vec<(String, i64)>,
    /// Projected weekly % at the reset (≤ 999); once 5 % of the week is gone.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub wp: Option<i64>,
}

impl PacePayload {
    /// Compact JSON for the RX characteristic.
    pub fn encode(&self) -> Vec<u8> {
        serde_json::to_vec(self).unwrap_or_default()
    }
}

/// The usage numbers a pace write is built from.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Usage {
    /// Session %.
    pub s: f64,
    /// Minutes until the session resets; -1 = not started.
    pub sr: i64,
    /// Weekly %.
    pub w: f64,
    /// Minutes until the weekly reset; -1 = unknown.
    pub wr: i64,
}

impl Usage {
    /// From a usage payload just written: only a valid, fresh (not a
    /// replay) subscription reading counts.
    pub fn from_payload(p: &Payload) -> Option<Self> {
        if !p.ok || p.rp.is_some() || p.acct.as_deref().unwrap_or("pro") != "pro" {
            return None;
        }
        Some(Usage {
            s: f64::from(p.s.unwrap_or(0)),
            sr: p.sr.unwrap_or(-1),
            w: f64::from(p.w.unwrap_or(0)),
            wr: p.wr.unwrap_or(-1),
        })
    }
}

/// History file lines → samples within [`HISTORY_KEEP`] of `now`; bad lines
/// are skipped.
pub fn parse_history(text: &str, now: i64) -> Vec<Sample> {
    text.lines().filter_map(parse_line).filter(|s| now - s.0 <= HISTORY_KEEP).collect()
}

fn parse_line(line: &str) -> Option<Sample> {
    let v: Value = serde_json::from_str(line).ok()?;
    let a = v.as_array()?;
    Some((a.first()?.as_f64()? as i64, a.get(1)?.as_f64()?, a.get(2)?.as_f64()?))
}

fn line_of(s: &Sample) -> String {
    let mut l = serde_json::to_string(&(s.0, s.1, s.2)).unwrap_or_default();
    l.push('\n');
    l
}

/// Read the history file (missing = empty).
pub fn load_history(path: &Path, now: i64) -> Vec<Sample> {
    fs::read_to_string(path).map(|t| parse_history(&t, now)).unwrap_or_default()
}

/// Append one sample (skipping one within 30 s of the last) and rewrite the
/// file only when its first line is old enough to prune. Returns the samples.
pub fn record_sample(path: &Path, mut samples: Vec<Sample>, sample: Sample, now: i64) -> io::Result<Vec<Sample>> {
    if samples.last().is_some_and(|l| sample.0 - l.0 < MIN_GAP) {
        return Ok(samples);
    }
    samples.push(sample);
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    let stale = fs::File::open(path)
        .ok()
        .and_then(|f| io::BufReader::new(f).lines().next()?.ok())
        .and_then(|first| parse_line(&first))
        .is_some_and(|first| now - first.0 > HISTORY_KEEP);
    if stale {
        let tmp = path.with_extension("jsonl.tmp");
        fs::write(&tmp, samples.iter().map(line_of).collect::<String>())?;
        fs::rename(&tmp, path)?;
    } else {
        fs::OpenOptions::new().create(true).append(true).open(path)?.write_all(line_of(&sample).as_bytes())?;
    }
    Ok(samples)
}

/// Session % across the current 5-hour window: [`SESS_POINTS`] slots,
/// filled up to now. Returns (values, % of the window elapsed). Before the
/// first sample the line ramps up from 0 at the window start (a window
/// starts empty); gaps between samples are interpolated.
pub fn session_series(samples: &[Sample], now: i64, s_now: f64, sr: i64) -> (Vec<i64>, i64) {
    let elapsed = ((SESSION_MIN - sr).clamp(0, SESSION_MIN) * 60) as f64;
    let now_f = now as f64;
    let start = now_f - elapsed;
    let slot = (SESSION_MIN * 60) as f64 / SESS_POINTS as f64;
    let filled = ((elapsed / slot).ceil() as usize).clamp(1, SESS_POINTS);
    let mut pts: Vec<(f64, f64)> = vec![(start, 0.0)];
    pts.extend(samples.iter().filter(|s| start <= s.0 as f64 && s.0 < now).map(|s| (s.0 as f64, s.1)));
    pts.push((now_f, s_now));
    let mut values = Vec::with_capacity(filled);
    for i in 0..filled {
        let at = now_f.min(start + (i + 1) as f64 * slot); // end of slot i
        let mut j = 0;
        while j + 1 < pts.len() && pts[j + 1].0 <= at {
            j += 1;
        }
        let v = if j + 1 < pts.len() && pts[j + 1].0 > pts[j].0 {
            let ((t0, v0), (t1, v1)) = (pts[j], pts[j + 1]);
            v0 + (v1 - v0) * (at - t0) / (t1 - t0)
        } else {
            pts[j].1
        };
        values.push(py_round(v).clamp(0, 100));
    }
    (values, py_round(elapsed / (SESSION_MIN * 60) as f64 * 100.0))
}

/// %/min: over the last 30 minutes when at least 10 of them are in this
/// window, else the window's average so far. `None` while it can't be told.
pub fn session_rate(samples: &[Sample], now: i64, s_now: f64, sr: i64) -> Option<f64> {
    let now_f = now as f64;
    let start = now_f - ((SESSION_MIN - sr).max(0) * 60) as f64;
    let first = samples.iter().find(|s| start <= s.0 as f64 && s.0 < now && now_f - s.0 as f64 <= RATE_SPAN).copied();
    if let Some((t, s, _)) = first {
        if now_f - t as f64 >= RATE_MIN_SPAN {
            return Some(((s_now - s) / ((now_f - t as f64) / 60.0)).max(0.0));
        }
    }
    let elapsed_min = (now_f - start) / 60.0;
    (elapsed_min >= 10.0).then(|| (s_now / elapsed_min).max(0.0))
}

/// Local clock text for a unix time: `"14:35"`, or `"2:35 PM"` when `tf`
/// is 12.
pub fn clock_text(epoch: f64, utc_offset_secs: i64, tf: u8) -> String {
    let secs = (epoch.floor() as i64 + utc_offset_secs).rem_euclid(86_400);
    let (h, m) = (secs / 3600, secs % 3600 / 60);
    if tf == 12 {
        let h12 = if h % 12 == 0 { 12 } else { h % 12 };
        format!("{h12}:{m:02} {}", if h < 12 { "AM" } else { "PM" })
    } else {
        format!("{h:02}:{m:02}")
    }
}

/// Days since the epoch of a unix time's local date.
fn local_day(t: i64, utc_offset_secs: i64) -> i64 {
    (t + utc_offset_secs).div_euclid(86_400)
}

/// Weekday name of an epoch day number (1970-01-01 was a Thursday).
fn day_name(day: i64) -> &'static str {
    DAY_NAMES[(day + 3).rem_euclid(7) as usize]
}

/// Weekly % added on each of the last 7 local days (today last): the sum of
/// the rises between consecutive samples, so a weekly reset doesn't count.
/// `-1` for a day with no samples at all (the app wasn't running).
pub fn week_days(samples: &[Sample], now: i64, w_now: f64, utc_offset_secs: i64) -> Vec<(String, i64)> {
    let today = local_day(now, utc_offset_secs);
    let first = today - 6;
    let mut added = [0.0f64; 7];
    let mut seen = [false; 7];
    let mut prev: Option<f64> = None;
    for &(t, _, w) in samples.iter().chain(std::iter::once(&(now, 0.0, w_now))) {
        let d = local_day(t, utc_offset_secs);
        if (first..=today).contains(&d) {
            let i = (d - first) as usize;
            seen[i] = true;
            if prev.is_some_and(|p| w > p) {
                added[i] += w - prev.unwrap_or(0.0);
            }
        }
        prev = Some(w);
    }
    (0..7).map(|i| (day_name(first + i as i64).to_owned(), if seen[i] { py_round(added[i]) } else { -1 })).collect()
}

/// The pace write for `usage`, from the samples before it. `tf` is 12 or 24.
pub fn payload(samples: &[Sample], usage: &Usage, now: i64, utc_offset_secs: i64, tf: u8) -> PacePayload {
    let Usage { s: s_now, sr, w: w_now, wr } = *usage;
    let mut out = PacePayload {
        k: "h",
        sh: None,
        fr: None,
        pr: None,
        pz: None,
        eta: None,
        wd: week_days(samples, now, w_now, utc_offset_secs),
        wp: None,
    };
    if sr >= 0 {
        let (sh, fr) = session_series(samples, now, s_now, sr);
        out.sh = Some(sh);
        out.fr = Some(fr);
        let rate = session_rate(samples, now, s_now, sr);
        if s_now >= 100.0 {
            out.pr = Some(100);
            out.pz = Some(2);
        } else if let Some(rate) = rate {
            let proj = s_now + rate * sr as f64;
            out.pr = Some(py_round(proj).min(999));
            out.pz = Some(if proj < 80.0 {
                0
            } else if proj < 100.0 {
                1
            } else {
                2
            });
            if proj >= 100.0 && rate > 0.0 {
                out.eta = Some(clock_text(now as f64 + (100.0 - s_now) / rate * 60.0, utc_offset_secs, tf));
            }
        }
    }
    if wr >= 0 {
        let frac = 1.0 - wr as f64 / WEEK_MIN as f64;
        if frac >= 0.05 {
            out.wp = Some(py_round(w_now / frac).min(999));
        }
    }
    out
}

/// Record the poll behind `usage_payload` in the history file at `path` and
/// build the pace write. `None` for a payload with no 5h/7d windows (spend
/// cap), no data, or a replay.
pub fn history(
    path: &Path,
    usage_payload: &Payload,
    now: i64,
    utc_offset_secs: i64,
    tf: u8,
) -> io::Result<Option<PacePayload>> {
    let Some(u) = Usage::from_payload(usage_payload) else { return Ok(None) };
    let samples = load_history(path, now);
    let samples = record_sample(path, samples, (now, u.s, u.w), now)?;
    let prior = match samples.last() {
        Some(l) if l.0 == now => &samples[..samples.len() - 1],
        _ => &samples[..],
    };
    Ok(Some(payload(prior, &u, now, utc_offset_secs, tf)))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::feeds::WRITE_LIMIT;

    /// Tue 2026-10-13 12:00:00 UTC.
    const NOW: i64 = 1_791_892_800;

    fn usage(s: f64, sr: i64, w: f64, wr: i64) -> Usage {
        Usage { s, sr, w, wr }
    }

    #[test]
    fn now_is_a_tuesday_noon() {
        assert_eq!(clock_text(NOW as f64, 0, 24), "12:00");
        assert_eq!(day_name(local_day(NOW, 0)), "Tue");
    }

    #[test]
    fn series_ramps_from_the_window_start() {
        // 2 h into the window (sr 180), no samples: a straight ramp to 24 %.
        let (v, fr) = session_series(&[], NOW, 24.0, 180);
        assert_eq!(fr, 40);
        assert_eq!(v, vec![2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24]);
        // A flat stretch then a jump, from samples.
        let samples = [(NOW - 7200 + 600, 0.0, 0.0), (NOW - 1200, 0.0, 0.0), (NOW - 600, 20.0, 0.0)];
        let (v, _) = session_series(&samples, NOW, 24.0, 180);
        assert_eq!(&v[..10], &[0; 10]);
        assert_eq!(&v[10..], &[20, 24]);
        // At the very start: one slot.
        assert_eq!(session_series(&[], NOW, 0.0, 300), (vec![0], 0));
        // Values clamp to 0..=100.
        assert_eq!(session_series(&[], NOW, 140.0, 0).0.last(), Some(&100));
    }

    #[test]
    fn rate_uses_the_last_half_hour() {
        let samples = [(NOW - 1800, 10.0, 0.0), (NOW - 900, 15.0, 0.0)];
        // 10 % over 30 min.
        assert_eq!(session_rate(&samples, NOW, 20.0, 180), Some(10.0 / 30.0));
        // Too recent for the 10-minute minimum: the window average (20 % over 120 min).
        assert_eq!(session_rate(&[(NOW - 300, 18.0, 0.0)], NOW, 20.0, 180), Some(20.0 / 120.0));
        // Under 10 minutes into the window: unknown.
        assert_eq!(session_rate(&[], NOW, 5.0, 295), None);
        // Falling usage is no negative pace.
        assert_eq!(session_rate(&[(NOW - 1200, 30.0, 0.0)], NOW, 20.0, 180), Some(0.0));
    }

    #[test]
    fn projection_and_eta() {
        // 50 % after 2 h, 1 %/min for the last 30 min, 180 min to go: way over.
        let samples = [(NOW - 1800, 20.0, 0.0)];
        let p = payload(&samples, &usage(50.0, 180, 10.0, 5040), NOW, 0, 24);
        assert_eq!((p.pr, p.pz), (Some(230), Some(2)));
        assert_eq!(p.eta.as_deref(), Some("12:50"));
        let p = payload(&samples, &usage(50.0, 180, 10.0, 5040), NOW, 3600, 12);
        assert_eq!(p.eta.as_deref(), Some("1:50 PM"));
        // On the low side: zone 0, no ETA.
        let p = payload(&[(NOW - 1800, 9.0, 0.0)], &usage(10.0, 180, 10.0, 5040), NOW, 0, 24);
        assert_eq!((p.pr, p.pz, p.eta), (Some(16), Some(0), None));
        // Already at 100 %.
        let p = payload(&[], &usage(100.0, 60, 10.0, 5040), NOW, 0, 24);
        assert_eq!((p.pr, p.pz, p.eta), (Some(100), Some(2), None));
        // Window not started: no session fields at all.
        let p = payload(&[], &usage(0.0, -1, 10.0, -1), NOW, 0, 24);
        assert_eq!((&p.sh, p.fr, p.pr, p.wp), (&None, None, None, None));
        let json = String::from_utf8(p.encode()).unwrap();
        assert!(json.starts_with(r#"{"k":"h","wd":[["Wed",-1]"#), "{json}");
    }

    #[test]
    fn weekly_projection() {
        // Half the week gone, 30 % used: 60 % at this pace.
        assert_eq!(payload(&[], &usage(0.0, -1, 30.0, 5040), NOW, 0, 24).wp, Some(60));
        // Under 5 % of the week gone: too early to say.
        assert_eq!(payload(&[], &usage(0.0, -1, 3.0, 10_000), NOW, 0, 24).wp, None);
        assert_eq!(payload(&[], &usage(0.0, -1, 99.0, 9000), NOW, 0, 24).wp, Some(924));
    }

    #[test]
    fn week_days_sum_the_rises() {
        let day = 86_400;
        let samples = [
            (NOW - 2 * day, 0.0, 10.0),
            (NOW - 2 * day + 3600, 0.0, 14.0),
            (NOW - day, 0.0, 15.0),
            (NOW - day + 3600, 0.0, 2.0), // weekly reset: not a rise
            (NOW - day + 7200, 0.0, 4.6),
        ];
        let wd = week_days(&samples, NOW, 5.0, 0);
        let names: Vec<&str> = wd.iter().map(|(d, _)| d.as_str()).collect();
        assert_eq!(names, ["Wed", "Thu", "Fri", "Sat", "Sun", "Mon", "Tue"]);
        let pcts: Vec<i64> = wd.iter().map(|d| d.1).collect();
        // Sun: 4 (10→14; its first sample has nothing before it). Mon: 1 + 2.6.
        // Tue: 0.4 → 0.
        assert_eq!(pcts, [-1, -1, -1, -1, 4, 4, 0]);
        // The day boundary is local: 11 h ahead puts noon UTC late on Tuesday
        // but the sample from 13 h earlier on Tuesday too.
        let wd = week_days(&[(NOW - 13 * 3600, 0.0, 1.0)], NOW, 3.0, 11 * 3600);
        assert_eq!(wd.last().unwrap(), &("Tue".to_owned(), 2));
        assert_eq!(wd[5].1, -1);
    }

    #[test]
    fn only_fresh_subscription_readings() {
        let base = Payload { s: Some(5), sr: Some(100), w: Some(1), wr: Some(10), ok: true, ..Default::default() };
        assert!(Usage::from_payload(&base).is_some(), "no acct = pro");
        let pro = Payload { acct: Some("pro".into()), ..base.clone() };
        assert_eq!(Usage::from_payload(&pro), Some(usage(5.0, 100, 1.0, 10)));
        assert!(Usage::from_payload(&Payload { acct: Some("ent".into()), ..base.clone() }).is_none());
        assert!(Usage::from_payload(&Payload { rp: Some(1), ..base.clone() }).is_none());
        assert!(Usage::from_payload(&Payload::no_data()).is_none());
    }

    #[test]
    fn history_file_round_trip() {
        let dir = std::env::temp_dir().join(format!("cm-pace-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let path = dir.join("usage-history.jsonl");
        let p =
            |s: u32| Payload { s: Some(s), sr: Some(200), w: Some(3), wr: Some(5000), ok: true, ..Default::default() };
        let a = history(&path, &p(10), NOW - 600, 0, 24).unwrap().unwrap();
        assert_eq!(a.sh.as_ref().map(Vec::len), Some(10));
        // Within 30 s of the last sample: not recorded again.
        history(&path, &p(11), NOW - 590, 0, 24).unwrap();
        history(&path, &p(12), NOW, 0, 24).unwrap();
        let lines = fs::read_to_string(&path).unwrap();
        assert_eq!(lines.lines().count(), 2, "{lines}");
        assert_eq!(load_history(&path, NOW), vec![(NOW - 600, 10.0, 3.0), (NOW, 12.0, 3.0)]);
        // A first line past 8 days gets the file rewritten without it.
        let later = NOW + HISTORY_KEEP - 100;
        history(&path, &p(1), later, 0, 24).unwrap();
        let samples = load_history(&path, later);
        assert_eq!(samples, vec![(NOW, 12.0, 3.0), (later, 1.0, 3.0)]);
        assert_eq!(fs::read_to_string(&path).unwrap().lines().count(), 2);
        // Spend-capped: nothing recorded.
        let ent = Payload { acct: Some("ent".into()), ..p(5) };
        assert_eq!(history(&path, &ent, later + 100, 0, 24).unwrap(), None);
        assert_eq!(load_history(&path, later + 100).len(), 2);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn bad_history_lines_are_skipped() {
        let text = format!("[{},1,2]\nnot json\n[1]\n{{}}\n[{},3.5,4]\n", NOW - 100, NOW - 50);
        assert_eq!(parse_history(&text, NOW), vec![(NOW - 100, 1.0, 2.0), (NOW - 50, 3.5, 4.0)]);
    }

    #[test]
    fn the_biggest_write_fits() {
        let samples: Vec<Sample> = (0..2000).map(|i| (NOW - 7 * 86_400 + i * 300, 55.5, (i % 100) as f64)).collect();
        let p = payload(&samples, &usage(99.0, 1, 99.0, 100), NOW, 0, 12);
        assert_eq!(p.sh.as_ref().map(Vec::len), Some(SESS_POINTS));
        let n = p.encode().len();
        assert!(n <= WRITE_LIMIT, "{n} bytes");
    }
}
