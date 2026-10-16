//! The device's extra pages in the daemon: Weather, Pace, Activity, service
//! status, System and Stocks. Each feed refreshes off the loop
//! (`spawn_blocking`) on its own [`Schedule`] and queues its writes; the
//! daemon sends one queued write per tick in which the engine wrote nothing,
//! round-robin across feeds. Each feed keeps its last good writes and
//! re-queues them after a reconnect (a rebooted device starts without
//! them). A feed whose setting changes starts over.
//!
//! | Feed | Refresh | Write |
//! | --- | --- | --- |
//! | Weather (`weather_location`) | 15 min, retry 2 | `{"k":"wx"}` |
//! | Pace (`pace_page`) | after each fresh usage write | `{"k":"h"}` |
//! | Activity (`activity_page`) | 30 s, and when the alert changes | `{"k":"a"}` |
//! | Service status (`service_status`) | 5 min, retry 2 | `{"k":"ss"}` |
//! | System (`system_stats`, Linux) | 30 s | `{"k":"sys"}` |
//! | Stocks (`stocks`) | 5 min, retry 2 when every symbol failed | `{"k":"q"}` per symbol |
//!
//! Refreshes start only while a device is connected.

use std::collections::VecDeque;
use std::path::PathBuf;

use clawdmeter_core::config::ClockMode;
use clawdmeter_core::feeds::{Schedule, WRITE_LIMIT};
use clawdmeter_core::stocks::Stock;
use clawdmeter_core::weather::WeatherPlace;
use clawdmeter_core::{activity, pace, service_status, stocks, system, weather, Paths, Payload, Settings};
use tokio::task::JoinHandle;

/// Each slot's new write (`None`: that slot failed and keeps its last one).
type Slots = Vec<Option<Vec<u8>>>;
type Job = JoinHandle<Result<Slots, String>>;

/// Activity re-reads transcripts this often (s).
const ACTIVITY_SECS: i64 = 30;

/// One feed: its schedule, the refresh in flight, and the writes not sent yet.
#[derive(Debug)]
struct Feed {
    /// Log label (`"Weather: {...}"`).
    name: &'static str,
    schedule: Schedule,
    job: Option<Job>,
    queue: VecDeque<Vec<u8>>,
}

impl Feed {
    fn new(name: &'static str, schedule: Schedule) -> Self {
        Feed { name, schedule, job: None, queue: VecDeque::new() }
    }

    /// Start over: a refresh in flight is dropped (it finishes unheard).
    fn reset(&mut self) {
        self.schedule.reset();
        self.job = None;
        self.queue.clear();
    }

    fn due(&self, now: i64) -> bool {
        self.job.is_none() && self.schedule.due(now)
    }

    fn start(&mut self, now: i64, f: impl FnOnce() -> Result<Slots, String> + Send + 'static) {
        self.schedule.started(now);
        self.job = Some(tokio::task::spawn_blocking(f));
    }

    /// Take a finished refresh's result: new writes replace whatever was
    /// still queued.
    async fn collect(&mut self, now: i64) {
        if !self.job.as_ref().is_some_and(JoinHandle::is_finished) {
            return;
        }
        let label = self.name.to_lowercase();
        match self.job.take().expect("checked").await {
            Ok(Ok(mut slots)) => {
                for w in slots.iter_mut() {
                    if w.as_ref().is_some_and(|b| b.len() > WRITE_LIMIT) {
                        log::warn!(
                            "{label}: dropped a {}-byte write (limit {WRITE_LIMIT})",
                            w.as_ref().map_or(0, Vec::len)
                        );
                        *w = None;
                    }
                }
                let fresh = self.schedule.succeeded(now, slots);
                if !fresh.is_empty() {
                    self.queue = fresh.into();
                }
            }
            Ok(Err(e)) => {
                log::warn!("{label}: {e}");
                self.schedule.failed(now);
            }
            Err(e) => {
                log::error!("{label} task failed: {e}");
                self.schedule.failed(now);
            }
        }
    }

    fn requeue(&mut self) {
        self.queue = self.schedule.last().cloned().collect();
    }
}

/// What the feeds take from the settings; a change to a feed's part
/// restarts that feed.
#[derive(Debug, Clone, PartialEq, Default)]
struct FeedSettings {
    weather: Option<WeatherPlace>,
    pace: bool,
    /// The accounts whose transcripts are read; `None` = off.
    activity: Option<Vec<PathBuf>>,
    status: bool,
    system: bool,
    stocks: Vec<Stock>,
    clock: ClockMode,
}

impl FeedSettings {
    fn from(s: &Settings) -> Self {
        FeedSettings {
            weather: s.weather.clone(),
            pace: s.pace_page,
            activity: s.activity_page.then(|| s.config_dirs.clone()),
            status: s.service_status,
            system: s.system_stats && system::SUPPORTED,
            stocks: s.stocks.clone(),
            clock: s.clock,
        }
    }
}

/// 12 or 24 for the Pace page's ETA: the `clock` setting, else the locale.
pub fn hour_format(clock: ClockMode, prefers_12h: bool) -> u8 {
    match clock {
        ClockMode::H12 => 12,
        ClockMode::H24 => 24,
        ClockMode::Auto | ClockMode::Off => {
            if prefers_12h {
                12
            } else {
                24
            }
        }
    }
}

fn one(write: Vec<u8>) -> Slots {
    vec![Some(write)]
}

fn unix_now_f64() -> f64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs_f64()).unwrap_or(0.0)
}

/// Every feed, in round-robin order.
#[derive(Debug)]
pub struct Feeds {
    paths: Paths,
    cfg: FeedSettings,
    prefers_12h: bool,
    weather: Feed,
    pace: Feed,
    activity: Feed,
    status: Feed,
    system: Feed,
    stocks: Feed,
    /// The usage payload the next Pace refresh records.
    pace_input: Option<Payload>,
    /// The session the alert on screen belongs to.
    alert_sid: Option<String>,
    /// Where the round-robin looks first.
    next: usize,
}

impl Feeds {
    /// The feeds for these settings. `prefers_12h` is the locale's hour
    /// format, for `clock = auto` / `off`.
    pub fn new(paths: Paths, settings: &Settings, prefers_12h: bool) -> Self {
        Feeds {
            paths,
            cfg: FeedSettings::from(settings),
            prefers_12h,
            weather: Feed::new("Weather", weather::schedule()),
            pace: Feed::new("Pace", Schedule::on_demand()),
            activity: Feed::new("Activity", Schedule::every(ACTIVITY_SECS, ACTIVITY_SECS)),
            status: Feed::new("Status", service_status::schedule()),
            system: Feed::new("System", system::schedule()),
            stocks: Feed::new("Stocks", stocks::schedule()),
            pace_input: None,
            alert_sid: None,
            next: 0,
        }
    }

    fn all_mut(&mut self) -> [&mut Feed; 6] {
        [&mut self.weather, &mut self.pace, &mut self.activity, &mut self.status, &mut self.system, &mut self.stocks]
    }

    fn all(&self) -> [&Feed; 6] {
        [&self.weather, &self.pace, &self.activity, &self.status, &self.system, &self.stocks]
    }

    /// Re-read settings: a feed whose setting changed starts over (a feed
    /// turned off stops writing).
    pub fn update_settings(&mut self, settings: &Settings) {
        let new = FeedSettings::from(settings);
        if new == self.cfg {
            return;
        }
        let old = std::mem::replace(&mut self.cfg, new);
        let cfg = self.cfg.clone();
        if old.weather != cfg.weather {
            self.weather.reset();
        }
        if old.pace != cfg.pace || old.clock != cfg.clock {
            self.pace.reset();
            self.pace_input = None;
        }
        if old.activity != cfg.activity {
            self.activity.reset();
        }
        if old.status != cfg.status {
            self.status.reset();
        }
        if old.system != cfg.system {
            self.system.reset();
        }
        if old.stocks != cfg.stocks {
            self.stocks.reset();
        }
    }

    /// The alert on screen now (its session id): a change refreshes the
    /// Activity page at once, so the session shows "needs you".
    pub fn set_alert_session(&mut self, sid: Option<&str>, now: i64) {
        if self.alert_sid.as_deref() == sid {
            return;
        }
        self.alert_sid = sid.map(str::to_owned);
        if self.cfg.activity.is_some() {
            self.activity.schedule.wake(now);
        }
    }

    /// A usage payload reached the device: fresh numbers (not a replay) of a
    /// subscription account refresh the Pace page.
    pub fn usage_written(&mut self, payload: &Payload, now: i64) {
        if self.cfg.pace && pace::Usage::from_payload(payload).is_some() {
            self.pace_input = Some(payload.clone());
            self.pace.schedule.wake(now);
        }
    }

    /// Take finished refreshes, and start the ones due (only while
    /// `connected`).
    pub async fn tick(&mut self, now: i64, connected: bool) {
        for f in self.all_mut() {
            f.collect(now).await;
        }
        if !connected {
            return;
        }
        if let Some(place) = self.cfg.weather.clone() {
            if self.weather.due(now) {
                self.weather.start(now, move || weather::fetch(&place).map(|p| one(p.encode())));
            }
        }
        if self.cfg.pace && self.pace.due(now) {
            if let Some(input) = self.pace_input.take() {
                let path = self.paths.state_dir.join("usage-history.jsonl");
                let tf = hour_format(self.cfg.clock, self.prefers_12h);
                self.pace.start(now, move || {
                    let off = clawdmeter_core::platform::utc_offset_secs();
                    match pace::history(&path, &input, now, off, tf) {
                        Ok(Some(p)) => Ok(one(p.encode())),
                        Ok(None) => Err("no 5-hour or weekly window in this reading".to_owned()),
                        Err(e) => Err(format!("{}: {e}", path.display())),
                    }
                });
            }
        }
        if let Some(dirs) = self.cfg.activity.clone() {
            if self.activity.due(now) {
                let cache = self.paths.state_dir.join("activity-cache.json");
                let sid = self.alert_sid.clone();
                self.activity.start(now, move || {
                    let off = clawdmeter_core::platform::utc_offset_secs();
                    activity::collect(&dirs, unix_now_f64(), off, sid.as_deref(), &cache).map(|p| one(p.encode()))
                });
            }
        }
        if self.cfg.status && self.status.due(now) {
            self.status.start(now, || service_status::fetch().map(|p| one(p.encode())));
        }
        if self.cfg.system && self.system.due(now) {
            self.system.start(now, || system::collect().map(|p| one(p.encode())));
        }
        if !self.cfg.stocks.is_empty() && self.stocks.due(now) {
            let list = self.cfg.stocks.clone();
            self.stocks.start(now, move || fetch_stocks(&list));
        }
    }

    /// A (re)connection: queue every feed's last writes again.
    pub fn requeue(&mut self) {
        for f in self.all_mut() {
            f.requeue();
        }
    }

    /// The next write to send, round-robin across feeds: (feed index, log
    /// label, bytes). Call [`Feeds::sent`] once it went out.
    pub fn next_write(&self) -> Option<(usize, &'static str, Vec<u8>)> {
        let feeds = self.all();
        (0..feeds.len()).map(|k| (self.next + k) % feeds.len()).find_map(|i| {
            let f = feeds[i];
            f.queue.front().map(|w| (i, f.name, w.clone()))
        })
    }

    /// The write [`Feeds::next_write`] returned went out.
    pub fn sent(&mut self, index: usize) {
        let n = self.all().len();
        if let Some(f) = self.all_mut().into_iter().nth(index) {
            f.queue.pop_front();
        }
        self.next = (index + 1) % n;
    }
}

/// One write per symbol; a failing symbol keeps its last write, and only
/// every symbol failing counts as a failure.
fn fetch_stocks(list: &[Stock]) -> Result<Slots, String> {
    let n = list.len();
    let slots: Slots = list
        .iter()
        .enumerate()
        .map(|(i, s)| match stocks::fetch(s, i, n) {
            Ok(p) => Some(p.encode()),
            Err(e) => {
                log::warn!("stock {}: {e}", s.symbol);
                None
            }
        })
        .collect();
    if slots.iter().all(Option::is_none) {
        return Err("every symbol failed".to_owned());
    }
    Ok(slots)
}

#[cfg(test)]
mod tests {
    use super::*;
    use clawdmeter_core::ConfigFile;

    fn settings(text: &str, paths: &Paths) -> Settings {
        Settings::from_config(&ConfigFile::parse(text), paths, None)
    }

    fn scratch(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("cm-feeds-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        d
    }

    #[test]
    fn write_limit_matches_the_link() {
        assert_eq!(WRITE_LIMIT, clawdmeter_ble::MAX_WRITE);
    }

    #[test]
    fn hour_formats() {
        assert_eq!(hour_format(ClockMode::H12, false), 12);
        assert_eq!(hour_format(ClockMode::H24, true), 24);
        assert_eq!(hour_format(ClockMode::Auto, true), 12);
        assert_eq!(hour_format(ClockMode::Off, false), 24);
    }

    #[test]
    fn round_robin_one_write_at_a_time() {
        let root = scratch("rr");
        let paths = Paths::under(&root);
        let mut f = Feeds::new(paths.clone(), &settings("", &paths), false);
        f.weather.queue.push_back(b"w1".to_vec());
        f.stocks.queue.extend([b"q1".to_vec(), b"q2".to_vec()]);
        f.status.queue.push_back(b"s1".to_vec());
        let mut order = Vec::new();
        while let Some((i, name, bytes)) = f.next_write() {
            order.push((name, String::from_utf8(bytes).unwrap()));
            f.sent(i);
        }
        let order: Vec<(&str, &str)> = order.iter().map(|(n, b)| (*n, b.as_str())).collect();
        assert_eq!(order, [("Weather", "w1"), ("Status", "s1"), ("Stocks", "q1"), ("Stocks", "q2")]);
        // Not sent (a failed write): the same one comes back.
        f.activity.queue.push_back(b"a".to_vec());
        let (i, ..) = f.next_write().unwrap();
        assert_eq!(f.next_write().unwrap().0, i);
    }

    #[test]
    fn a_settings_change_restarts_only_that_feed() {
        let root = scratch("cfg");
        let paths = Paths::under(&root);
        let mut f = Feeds::new(paths.clone(), &settings("stocks = AMD\n", &paths), false);
        for feed in f.all_mut() {
            feed.schedule.started(0);
            feed.schedule.succeeded(1, vec![Some(b"x".to_vec())]);
        }
        f.update_settings(&settings("stocks = AMD, NVDA\n", &paths));
        assert!(f.stocks.schedule.last().next().is_none() && f.stocks.due(2));
        assert!(f.status.schedule.last().next().is_some() && !f.status.due(2));
        f.update_settings(&settings("stocks = AMD, NVDA\nservice_status = off\n", &paths));
        assert!(f.status.schedule.last().next().is_none());
        // After a reconnect, every last write is queued again.
        f.requeue();
        assert_eq!(f.weather.queue.len(), 1);
        assert!(f.status.queue.is_empty());
    }

    #[tokio::test]
    async fn pace_after_a_fresh_usage_write() {
        let root = scratch("pace");
        let paths = Paths::under(&root);
        let mut f = Feeds::new(paths.clone(), &settings("activity_page = off\nservice_status = off\n", &paths), false);
        let now = clawdmeter_core::unix_now();
        // A replay or a spend-capped account: nothing.
        let fresh = Payload { s: Some(20), sr: Some(120), w: Some(5), wr: Some(4000), ok: true, ..Default::default() };
        f.usage_written(&Payload { rp: Some(1), ..fresh.clone() }, now);
        f.usage_written(&Payload { acct: Some("ent".into()), ..fresh.clone() }, now);
        assert!(f.pace_input.is_none());
        f.usage_written(&fresh, now);
        // Not connected: nothing starts.
        f.tick(now, false).await;
        assert!(f.pace.job.is_none());
        f.tick(now, true).await;
        while f.pace.job.as_ref().is_some_and(|j| !j.is_finished()) {
            tokio::task::yield_now().await;
        }
        f.tick(now + 1, true).await;
        let (_, name, bytes) = f.next_write().unwrap();
        assert_eq!(name, "Pace");
        let v: serde_json::Value = serde_json::from_slice(&bytes).unwrap();
        assert_eq!((v["k"].as_str(), v["fr"].as_i64()), (Some("h"), Some(60)));
        assert!(root.join("state/usage-history.jsonl").exists());
        // Not due again until the next usage write.
        assert!(!f.pace.due(now + 3600));
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn the_alert_wakes_activity() {
        let root = scratch("alert");
        let paths = Paths::under(&root);
        let mut f = Feeds::new(paths.clone(), &settings("", &paths), false);
        f.activity.schedule.started(100);
        assert!(!f.activity.due(101));
        f.set_alert_session(Some("abc"), 101);
        assert!(f.activity.due(101));
        f.activity.schedule.started(101);
        f.set_alert_session(Some("abc"), 102);
        assert!(!f.activity.due(102), "the same alert doesn't wake it again");
        f.set_alert_session(None, 103);
        assert!(f.activity.due(103));
    }
}
