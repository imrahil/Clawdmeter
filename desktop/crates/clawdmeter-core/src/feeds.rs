//! What the device's extra pages share: when to refresh a feed
//! ([`Schedule`]), the ASCII fold, and the HTTP helper.
//!
//! Each page gets its own tagged write (`{"k":"wx"}`, `"h"`, `"a"`, `"ss"`,
//! `"sys"`, `"q"`), separate from the usage payload: the firmware routes on
//! `k`, so they never count as fresh usage, a rate sample or a heartbeat.

use std::time::Duration;

use serde_json::Value;

use crate::attention::to_ascii;

/// The longest write the app sends (`clawdmeter_ble::MAX_WRITE`; the
/// firmware's RX buffer is 512 bytes with the null).
pub const WRITE_LIMIT: usize = 500;

/// A `next_at` that never comes: an on-demand feed waits for
/// [`Schedule::wake`].
const NEVER: i64 = i64::MAX / 4;

/// When to refresh one feed, and its last good writes. Pure: the daemon asks
/// [`Schedule::due`] each tick.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Schedule {
    interval: i64,
    retry: i64,
    next_at: i64,
    /// A wake came in while a refresh ran: keep it due.
    woken: bool,
    /// The last good write of each slot (a slot is one write: a stock
    /// symbol, or the feed's single write), re-sent after a reconnect (a
    /// rebooted device starts without them).
    last: Vec<Option<Vec<u8>>>,
}

impl Schedule {
    /// Refresh every `interval` seconds; a failure tries again after `retry`.
    /// The first refresh is due at once.
    pub const fn every(interval: i64, retry: i64) -> Self {
        Schedule { interval, retry, next_at: 0, woken: false, last: Vec::new() }
    }

    /// Refresh only when [`Schedule::wake`] asks.
    pub const fn on_demand() -> Self {
        Schedule { interval: NEVER, retry: NEVER, next_at: NEVER, woken: false, last: Vec::new() }
    }

    /// A refresh should start now.
    pub fn due(&self, now: i64) -> bool {
        now >= self.next_at
    }

    /// A refresh just started: don't start another until it reports.
    pub fn started(&mut self, now: i64) {
        self.woken = false;
        self.next_at = now.saturating_add(self.interval);
    }

    /// It worked. `slots` holds each slot's new write, `None` where that slot
    /// failed (it keeps its previous write). Returns the new writes, to send.
    pub fn succeeded(&mut self, now: i64, slots: Vec<Option<Vec<u8>>>) -> Vec<Vec<u8>> {
        if !self.woken {
            self.next_at = now.saturating_add(self.interval);
        }
        let mut fresh = Vec::new();
        let mut last = Vec::with_capacity(slots.len());
        for (i, slot) in slots.into_iter().enumerate() {
            match slot {
                Some(w) => {
                    fresh.push(w.clone());
                    last.push(Some(w));
                }
                None => last.push(self.last.get(i).cloned().flatten()),
            }
        }
        self.last = last;
        fresh
    }

    /// It failed: try again soon.
    pub fn failed(&mut self, now: i64) {
        if !self.woken {
            self.next_at = now.saturating_add(self.retry);
        }
    }

    /// Refresh now (new input for an on-demand feed, or something changed).
    pub fn wake(&mut self, now: i64) {
        self.next_at = self.next_at.min(now);
        self.woken = true;
    }

    /// The feed's setting changed: refresh at once (an on-demand feed waits
    /// for its next wake) and forget the old writes.
    pub fn reset(&mut self) {
        let fresh = if self.interval == NEVER { Self::on_demand() } else { Self::every(self.interval, self.retry) };
        *self = fresh;
    }

    /// The last good writes, in slot order.
    pub fn last(&self) -> impl Iterator<Item = &Vec<u8>> {
        self.last.iter().flatten()
    }
}

/// Fold to ASCII like the old daemon's `to_ascii`: accents dropped (NFKD),
/// anything else non-ASCII and control characters removed, whitespace
/// collapsed, then cut to `limit` characters (no ellipsis, so short fields
/// such as a currency stay exact).
pub fn fold_ascii(text: &str, limit: usize) -> String {
    to_ascii(text, usize::MAX).chars().filter(|c| !c.is_ascii_control()).take(limit).collect()
}

/// Python's `round()`: half to even.
pub(crate) fn py_round(x: f64) -> i64 {
    x.round_ties_even() as i64
}

/// Python's `round(x, 2)`.
pub(crate) fn round2(x: f64) -> f64 {
    (x * 100.0).round_ties_even() / 100.0
}

/// Percent-encode one URL query or path component.
pub(crate) fn encode_component(s: &str) -> String {
    s.bytes()
        .map(|b| match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => (b as char).to_string(),
            _ => format!("%{b:02X}"),
        })
        .collect()
}

const TIMEOUT: Duration = Duration::from_secs(15);

/// A blocking HTTP agent: 15 s timeout, statuses returned rather than errors.
pub(crate) fn agent() -> ureq::Agent {
    ureq::Agent::config_builder().http_status_as_error(false).timeout_global(Some(TIMEOUT)).build().into()
}

/// GET `url` as JSON with this User-Agent; a non-2xx status is an error.
pub(crate) fn get_json(agent: &ureq::Agent, url: &str, user_agent: &str) -> Result<Value, String> {
    let mut resp = agent.get(url).header("User-Agent", user_agent).call().map_err(|e| e.to_string())?;
    let status = resp.status().as_u16();
    if !(200..300).contains(&status) {
        return Err(format!("HTTP {status}"));
    }
    let body = resp.body_mut().read_to_string().map_err(|e| e.to_string())?;
    serde_json::from_str(&body).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn timed_schedule() {
        let mut s = Schedule::every(900, 120);
        assert!(s.due(0));
        s.started(100);
        assert!(!s.due(101));
        s.failed(110);
        assert!(s.due(110 + 120));
        assert_eq!(s.succeeded(200, vec![Some(b"{}".to_vec())]), vec![b"{}".to_vec()]);
        assert!(!s.due(200 + 900 - 1) && s.due(200 + 900));
        assert_eq!(s.last().count(), 1);
        s.reset();
        assert!(s.due(0) && s.last().next().is_none());
    }

    #[test]
    fn a_failed_slot_keeps_its_last_write() {
        let mut s = Schedule::every(300, 120);
        s.started(0);
        s.succeeded(1, vec![Some(b"a1".to_vec()), Some(b"b1".to_vec()), None]);
        s.started(300);
        let fresh = s.succeeded(301, vec![None, Some(b"b2".to_vec()), Some(b"c2".to_vec())]);
        assert_eq!(fresh, vec![b"b2".to_vec(), b"c2".to_vec()]);
        let last: Vec<&[u8]> = s.last().map(Vec::as_slice).collect();
        assert_eq!(last, vec![&b"a1"[..], b"b2", b"c2"]);
        // Fewer slots (a shorter list): the extra ones go.
        s.succeeded(600, vec![Some(b"a3".to_vec())]);
        assert_eq!(s.last().count(), 1);
    }

    #[test]
    fn on_demand_schedule() {
        let mut s = Schedule::on_demand();
        assert!(!s.due(0) && !s.due(1_000_000_000_000));
        s.wake(50);
        assert!(s.due(50));
        s.started(50);
        assert!(!s.due(51));
        // A wake while it runs survives the result.
        s.wake(52);
        s.succeeded(53, vec![Some(b"x".to_vec())]);
        assert!(s.due(53));
        s.started(53);
        s.failed(54);
        assert!(!s.due(1_000_000));
        s.reset();
        assert!(!s.due(1_000_000) && s.last().next().is_none());
    }

    #[test]
    fn fold() {
        assert_eq!(fold_ascii("Kraków  ąę\u{1}x", 20), "Krakow aex");
        assert_eq!(fold_ascii("GBp", 3), "GBp");
        assert_eq!(fold_ascii("ABCD", 3), "ABC");
        assert_eq!(fold_ascii("", 3), "");
    }

    #[test]
    fn python_rounding() {
        assert_eq!((py_round(2.5), py_round(3.5), py_round(-0.5), py_round(2.6)), (2, 4, 0, 3));
        assert_eq!(round2(123.456), 123.46);
    }

    #[test]
    fn component_encoding() {
        assert_eq!(encode_component("Nowy Sącz"), "Nowy%20S%C4%85cz");
        assert_eq!(encode_component("^GSPC"), "%5EGSPC");
    }
}
