//! The device's Activity page: one `{"k":"a", ...}` write, read straight
//! from Claude Code's transcripts (`<config dir>/projects/*/*.jsonl` for
//! every account): which sessions are live and what they're doing, plus
//! today's token and API-equivalent cost totals (subagent transcripts
//! included). A token cache (`<state>/activity-cache.json`) keeps the daily
//! totals incremental.
//!
//! "Today" is the host's local day: the UTC offset is a parameter.

use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::io::{self, Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::time::UNIX_EPOCH;

use chrono::{DateTime, FixedOffset, Utc};
use serde::{Deserialize, Serialize};
use serde_json::Value;

use crate::feeds::{fold_ascii, py_round, WRITE_LIMIT};

/// Every key [`ActivityPayload`] can emit, for the firmware cross-check.
pub const ACTIVITY_KEYS: &[&str] = &["k", "ss", "nl", "tk", "co", "ns"];

/// Sessions touched within the hour are listed (s).
const ACTIVE_WINDOW: f64 = 60.0 * 60.0;
/// An unfinished turn idle this long has stalled (s).
const WORKING_FRESH: f64 = 90.0;
/// A tool call older than this no longer counts as working (s).
const TOOL_FRESH: f64 = 15.0 * 60.0;
/// Rows on the page.
pub const MAX_SESSIONS: usize = 4;
/// How much of a transcript's end is read for its state.
const TAIL_BYTES: u64 = 256 * 1024;

/// Session states (`ss[i][1]`).
pub const STATE_IDLE: u8 = 0;
/// Claude has the turn.
pub const STATE_WORKING: u8 = 1;
/// The session's alert is up.
pub const STATE_WAITING: u8 = 2;

/// $ per million tokens: (model id prefix, input, output, cache read).
/// Cache writes bill at 1.25× input. First match wins, so specific ids come
/// first; an unknown model costs 0. Update this table when models or prices
/// change.
pub const PRICES: &[(&str, f64, f64, f64)] = &[
    ("claude-fable-5-1", 10.0, 50.0, 0.25),
    ("claude-mythos-5-1", 10.0, 50.0, 0.25),
    ("claude-fable-5", 10.0, 50.0, 1.0),
    ("claude-mythos-5", 10.0, 50.0, 1.0),
    ("claude-opus-5-5", 4.0, 20.0, 0.20),
    ("claude-opus-5", 5.0, 25.0, 0.50),
    ("claude-opus-4-8", 5.0, 25.0, 0.50),
    ("claude-opus-4-7", 5.0, 25.0, 0.50),
    ("claude-opus-4-6", 5.0, 25.0, 0.50),
    ("claude-opus-4-5", 5.0, 25.0, 0.50),
    ("claude-opus-4", 15.0, 75.0, 1.50),
    ("claude-sonnet-5", 2.0, 10.0, 0.20),
    ("claude-sonnet-4", 3.0, 15.0, 0.30),
    ("claude-haiku-5-5", 0.10, 0.50, 0.01),
    ("claude-haiku-4-5", 1.0, 5.0, 0.10),
];

/// One activity write.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct ActivityPayload {
    /// Always `"a"`.
    pub k: &'static str,
    /// Live sessions, waiting first, then working, then idle, most recent
    /// first within each: `[project, state, label, minutes since its last
    /// write]` (≤ [`MAX_SESSIONS`]).
    pub ss: Vec<(String, u8, String, i64)>,
    /// Sessions touched within the hour (all of them, not just the rows).
    pub nl: usize,
    /// Tokens today (input, output, cache writes and reads).
    pub tk: u64,
    /// API-equivalent cost today, in cents.
    pub co: i64,
    /// Sessions with a reply today.
    pub ns: usize,
}

impl ActivityPayload {
    /// Compact JSON for the RX characteristic; rows are dropped from the end
    /// if it would not fit one write.
    pub fn encode(&self) -> Vec<u8> {
        let mut p = self.clone();
        loop {
            let data = serde_json::to_vec(&p).unwrap_or_default();
            if data.len() <= WRITE_LIMIT || p.ss.is_empty() {
                return data;
            }
            p.ss.pop();
        }
    }
}

fn int(u: &Value, k: &str) -> u64 {
    u.get(k).and_then(Value::as_u64).unwrap_or(0)
}

/// API-equivalent $ for one assistant message's usage block (0 for an
/// unknown model).
pub fn usage_cost(model: &str, u: &Value) -> f64 {
    let Some(&(_, inp, outp, cread)) = PRICES.iter().find(|(prefix, ..)| model.starts_with(prefix)) else {
        return 0.0;
    };
    (int(u, "input_tokens") as f64 * inp
        + int(u, "cache_creation_input_tokens") as f64 * inp * 1.25
        + int(u, "cache_read_input_tokens") as f64 * cread
        + int(u, "output_tokens") as f64 * outp)
        / 1e6
}

/// Every token in a usage block.
pub fn usage_tokens(u: &Value) -> u64 {
    ["input_tokens", "output_tokens", "cache_creation_input_tokens", "cache_read_input_tokens"]
        .iter()
        .map(|k| int(u, k))
        .sum()
}

fn offset(utc_offset_secs: i64) -> FixedOffset {
    FixedOffset::east_opt(utc_offset_secs as i32).unwrap_or_else(|| FixedOffset::east_opt(0).expect("zero offset"))
}

/// The local date (`2026-10-16`) of a unix time.
pub fn local_date(t: i64, utc_offset_secs: i64) -> String {
    DateTime::<Utc>::from_timestamp(t, 0)
        .unwrap_or_default()
        .with_timezone(&offset(utc_offset_secs))
        .format("%Y-%m-%d")
        .to_string()
}

/// The local date of a transcript timestamp (`2026-10-16T09:12:03.123Z`).
fn record_day(ts: Option<&str>, utc_offset_secs: i64) -> Option<String> {
    let t = DateTime::parse_from_rfc3339(ts?).ok()?;
    Some(t.with_timezone(&offset(utc_offset_secs)).format("%Y-%m-%d").to_string())
}

/// Today's totals, kept between runs so each run reads only new lines.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct TokenCache {
    /// The local date the totals are for; another day starts over.
    pub day: String,
    /// Bytes of each transcript already read.
    pub files: BTreeMap<String, u64>,
    /// `message id:request id` pairs already counted (Claude Code writes
    /// one line per content block, each carrying the same usage).
    pub seen: BTreeSet<String>,
    /// Tokens today.
    pub tk: u64,
    /// Dollars today.
    pub co: f64,
    /// Sessions seen today.
    pub ss: BTreeSet<String>,
}

impl TokenCache {
    /// Start over when the day changed.
    pub fn roll(&mut self, today: &str) {
        if self.day != today {
            *self = TokenCache { day: today.to_owned(), ..Default::default() };
        }
    }

    /// Fold complete transcript lines into the totals.
    pub fn add_lines(&mut self, chunk: &[u8], utc_offset_secs: i64) {
        for raw in chunk.split(|b| *b == b'\n') {
            let Ok(rec) = serde_json::from_slice::<Value>(raw) else { continue };
            if rec.get("type").and_then(Value::as_str) != Some("assistant")
                || record_day(rec.get("timestamp").and_then(Value::as_str), utc_offset_secs).as_deref()
                    != Some(self.day.as_str())
            {
                continue;
            }
            let Some(msg) = rec.get("message") else { continue };
            let model = msg.get("model").and_then(Value::as_str).unwrap_or("");
            let Some(u) = msg.get("usage").filter(|u| u.as_object().is_some_and(|o| !o.is_empty())) else {
                continue;
            };
            if model == "<synthetic>" {
                continue;
            }
            let id = |v: Option<&Value>| v.and_then(Value::as_str).unwrap_or("None").to_owned();
            let key = format!("{}:{}", id(msg.get("id")), id(rec.get("requestId")));
            if !self.seen.insert(key) {
                continue;
            }
            self.tk += usage_tokens(u);
            self.co += usage_cost(model, u);
            if let Some(sid) = rec.get("sessionId").and_then(Value::as_str).filter(|s| !s.is_empty()) {
                self.ss.insert(sid.to_owned());
            }
        }
    }

    /// Read what's new in each file (from its saved offset to its last
    /// complete line). A file that shrank is read from the start.
    pub fn tally(&mut self, files: &[PathBuf], utc_offset_secs: i64) {
        for path in files {
            let key = path.to_string_lossy().into_owned();
            let Ok(size) = fs::metadata(path).map(|m| m.len()) else { continue };
            let mut off = self.files.get(&key).copied().unwrap_or(0);
            if off > size {
                off = 0; // rewritten / truncated
            }
            if off == size {
                continue;
            }
            let Ok(chunk) = read_from(path, off) else { continue };
            let Some(end) = chunk.iter().rposition(|b| *b == b'\n') else { continue };
            self.files.insert(key, off + end as u64 + 1);
            self.add_lines(&chunk[..end], utc_offset_secs);
        }
    }

    /// Load the cache (missing or unreadable = empty).
    pub fn load(path: &Path) -> Self {
        fs::read(path).ok().and_then(|b| serde_json::from_slice(&b).ok()).unwrap_or_default()
    }

    /// Save atomically.
    pub fn save(&self, path: &Path) -> io::Result<()> {
        if let Some(dir) = path.parent() {
            fs::create_dir_all(dir)?;
        }
        let tmp = path.with_extension("json.tmp");
        fs::write(&tmp, serde_json::to_vec(self).map_err(io::Error::other)?)?;
        fs::rename(&tmp, path)
    }
}

fn read_from(path: &Path, off: u64) -> io::Result<Vec<u8>> {
    let mut f = fs::File::open(path)?;
    f.seek(SeekFrom::Start(off))?;
    let mut buf = Vec::new();
    f.read_to_end(&mut buf)?;
    Ok(buf)
}

/// The records in the last [`TAIL_BYTES`] of a transcript (the first line
/// may be cut in half and is then skipped).
fn tail_records(path: &Path) -> io::Result<Vec<Value>> {
    let size = fs::metadata(path)?.len();
    let data = read_from(path, size.saturating_sub(TAIL_BYTES))?;
    Ok(data.split(|b| *b == b'\n').filter_map(|l| serde_json::from_slice(l).ok()).collect())
}

/// `mcp__github__create_pr` → `create_pr`, cut to fit the row.
pub fn tool_label(name: &str) -> String {
    fold_ascii(name.rsplit("__").next().unwrap_or(""), 12)
}

/// (state, label) from a transcript's last user / assistant record; `age`
/// is seconds since the file was written.
pub fn session_state(records: &[Value], age: f64) -> (u8, String) {
    let last = records.iter().rev().find(|r| {
        matches!(r.get("type").and_then(Value::as_str), Some("user" | "assistant"))
            && !r.get("isMeta").and_then(Value::as_bool).unwrap_or(false)
    });
    let Some(last) = last else { return (STATE_IDLE, String::new()) };
    let msg = last.get("message");
    if last.get("type").and_then(Value::as_str) == Some("assistant") {
        let content = msg.and_then(|m| m.get("content")).and_then(Value::as_array);
        let tool = content.into_iter().flatten().rfind(|c| c.get("type").and_then(Value::as_str) == Some("tool_use"));
        if let Some(tool) = tool {
            return if age < TOOL_FRESH {
                (STATE_WORKING, tool_label(tool.get("name").and_then(Value::as_str).unwrap_or("")))
            } else {
                (STATE_IDLE, String::new())
            };
        }
        let stop = msg.and_then(|m| m.get("stop_reason")).and_then(Value::as_str);
        if matches!(stop, Some("end_turn" | "stop_sequence" | "max_tokens" | "refusal")) {
            return (STATE_IDLE, String::new());
        }
    }
    // A prompt or tool result, or a reply still streaming: Claude has the turn.
    if age < WORKING_FRESH {
        (STATE_WORKING, "Thinking".to_owned())
    } else {
        (STATE_IDLE, String::new())
    }
}

fn base_name(p: &str) -> &str {
    p.trim_end_matches(['/', '\\']).rsplit(['/', '\\']).next().unwrap_or("")
}

/// The project a transcript belongs to: the last `cwd` it names, else its
/// folder's name.
pub fn project_name(records: &[Value], path: &Path) -> String {
    if let Some(cwd) = records.iter().rev().filter_map(|r| r.get("cwd").and_then(Value::as_str)).find(|c| !c.is_empty())
    {
        let name = fold_ascii(base_name(cwd), 18);
        return if name.is_empty() { "~".to_owned() } else { name };
    }
    let dir = path.parent().and_then(Path::file_name).map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    fold_ascii(&dir, 18)
}

/// One live session, before ranking.
#[derive(Debug, Clone, PartialEq)]
pub struct SessionRow {
    /// Seconds since its transcript was written.
    pub age: f64,
    /// `[project, state, label, minutes]`.
    pub row: (String, u8, String, i64),
}

/// A session's row from its transcript's tail. The session the current
/// alert belongs to (`alert_sid`) shows as waiting.
pub fn session_row(records: &[Value], path: &Path, age: f64, alert_sid: Option<&str>) -> SessionRow {
    let sid = path.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
    let (mut state, mut label) = session_state(records, age);
    if alert_sid.is_some_and(|a| !a.is_empty() && a == sid) {
        (state, label) = (STATE_WAITING, "needs you".to_owned());
    }
    SessionRow { age, row: (project_name(records, path), state, label, (age / 60.0).floor() as i64) }
}

/// Waiting first, then working, then idle; most recent first within each.
/// Returns the top rows and how many there were.
pub fn rank(mut rows: Vec<SessionRow>) -> (Vec<(String, u8, String, i64)>, usize) {
    rows.sort_by(|a, b| b.row.1.cmp(&a.row.1).then(a.age.total_cmp(&b.age)));
    let live = rows.len();
    (rows.into_iter().take(MAX_SESSIONS).map(|r| r.row).collect(), live)
}

fn mtime_secs(path: &Path) -> Option<f64> {
    let m = fs::metadata(path).ok()?.modified().ok()?;
    Some(m.duration_since(UNIX_EPOCH).ok()?.as_secs_f64())
}

fn is_jsonl(p: &Path) -> bool {
    p.extension().is_some_and(|e| e == "jsonl")
}

fn hidden(p: &Path) -> bool {
    p.file_name().is_some_and(|n| n.to_string_lossy().starts_with('.'))
}

/// `projects/*/*.jsonl`: one transcript per top-level session.
fn top_level(projects: &Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    for dir in fs::read_dir(projects).into_iter().flatten().flatten().map(|e| e.path()) {
        if !dir.is_dir() || hidden(&dir) {
            continue;
        }
        for f in fs::read_dir(&dir).into_iter().flatten().flatten().map(|e| e.path()) {
            if is_jsonl(&f) && !hidden(&f) && f.is_file() {
                out.push(f);
            }
        }
    }
    out
}

/// `projects/**/*.jsonl`: subagent transcripts too.
fn every_transcript(dir: &Path, out: &mut Vec<PathBuf>, depth: usize) {
    if depth > 8 {
        return;
    }
    for p in fs::read_dir(dir).into_iter().flatten().flatten().map(|e| e.path()) {
        if hidden(&p) {
            continue;
        }
        if p.is_dir() {
            every_transcript(&p, out, depth + 1);
        } else if is_jsonl(&p) {
            out.push(p);
        }
    }
}

/// Unix time of the local midnight starting `now`'s day.
fn local_midnight(now: i64, utc_offset_secs: i64) -> i64 {
    (now + utc_offset_secs).div_euclid(86_400) * 86_400 - utc_offset_secs
}

/// Read every account's transcripts and build the write (blocking: run it
/// off the daemon's loop). `cache_path` keeps today's token totals.
pub fn collect(
    config_dirs: &[PathBuf],
    now: f64,
    utc_offset_secs: i64,
    alert_sid: Option<&str>,
    cache_path: &Path,
) -> Result<ActivityPayload, String> {
    let mut top = Vec::new();
    let mut every = Vec::new();
    for d in config_dirs {
        let root = d.join("projects");
        top.extend(top_level(&root));
        every_transcript(&root, &mut every, 0);
    }
    let now_i = now.floor() as i64;
    let today = local_date(now_i, utc_offset_secs);
    let midnight = local_midnight(now_i, utc_offset_secs) as f64;
    let todays: Vec<PathBuf> = every.into_iter().filter(|p| mtime_secs(p).is_some_and(|m| m >= midnight)).collect();
    let mut cache = TokenCache::load(cache_path);
    cache.roll(&today);
    cache.tally(&todays, utc_offset_secs);
    cache.save(cache_path).map_err(|e| format!("activity cache: {e}"))?;
    let rows: Vec<SessionRow> = top
        .iter()
        .filter_map(|p| {
            let age = (now - mtime_secs(p)?).max(0.0);
            if age > ACTIVE_WINDOW {
                return None;
            }
            let records = tail_records(p).unwrap_or_default();
            Some(session_row(&records, p, age, alert_sid))
        })
        .collect();
    let (ss, nl) = rank(rows);
    Ok(ActivityPayload { k: "a", ss, nl, tk: cache.tk, co: py_round(cache.co * 100.0), ns: cache.ss.len() })
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn assistant(content: Value, stop: Option<&str>) -> Value {
        json!({"type": "assistant", "message": {"content": content, "stop_reason": stop}})
    }

    #[test]
    fn prices() {
        let u = json!({"input_tokens": 1_000_000, "output_tokens": 1_000_000,
                       "cache_creation_input_tokens": 1_000_000, "cache_read_input_tokens": 1_000_000});
        // 4 + 20 + 4×1.25 + 0.20
        assert!((usage_cost("claude-opus-5-5-20261001", &u) - 29.2).abs() < 1e-9);
        // The specific id wins over its family prefix.
        assert!((usage_cost("claude-opus-4-1", &u) - (15.0 + 75.0 + 18.75 + 1.5)).abs() < 1e-9);
        assert!((usage_cost("claude-opus-4-6", &u) - (5.0 + 25.0 + 6.25 + 0.5)).abs() < 1e-9);
        assert_eq!(usage_cost("gpt-9", &u), 0.0);
        assert_eq!(usage_tokens(&u), 4_000_000);
        assert_eq!(usage_tokens(&json!({"input_tokens": 5, "output_tokens": null})), 5);
    }

    #[test]
    fn states() {
        let tool = assistant(
            json!([{"type": "text"}, {"type": "tool_use", "name": "mcp__github__create_pull_request"}]),
            None,
        );
        assert_eq!(session_state(std::slice::from_ref(&tool), 10.0), (STATE_WORKING, "create_pull_".to_owned()));
        assert_eq!(session_state(&[tool], 16.0 * 60.0), (STATE_IDLE, String::new()));
        let done = assistant(json!([{"type": "text"}]), Some("end_turn"));
        assert_eq!(session_state(std::slice::from_ref(&done), 5.0), (STATE_IDLE, String::new()));
        let prompt = json!({"type": "user", "message": {"content": "hi"}});
        assert_eq!(session_state(&[done.clone(), prompt.clone()], 30.0), (STATE_WORKING, "Thinking".to_owned()));
        assert_eq!(session_state(std::slice::from_ref(&prompt), 120.0), (STATE_IDLE, String::new()));
        // Meta lines and other record types don't count.
        let meta = json!({"type": "user", "isMeta": true, "message": {"content": "x"}});
        let summary = json!({"type": "summary"});
        assert_eq!(session_state(&[done, meta, summary], 5.0), (STATE_IDLE, String::new()));
        // A reply still streaming (no stop reason yet).
        assert_eq!(session_state(&[assistant(json!([]), None)], 5.0).0, STATE_WORKING);
        assert_eq!(session_state(&[], 5.0), (STATE_IDLE, String::new()));
    }

    #[test]
    fn projects() {
        let p = Path::new("/h/.claude/projects/-home-u-proj/abc.jsonl");
        assert_eq!(project_name(&[json!({"cwd": "/home/u/Clawdmeter/"}), json!({})], p), "Clawdmeter");
        assert_eq!(project_name(&[json!({"cwd": "C:\\Users\\u\\Projekt Łódź"})], p), "Projekt odz");
        assert_eq!(project_name(&[json!({"cwd": "/"})], p), "~");
        assert_eq!(project_name(&[], p), "-home-u-proj");
        assert_eq!(project_name(&[json!({"cwd": "/x/a-very-long-project-name-indeed"})], p).len(), 18);
    }

    #[test]
    fn ranking_and_alert() {
        let p = |sid: &str| PathBuf::from(format!("/p/proj/{sid}.jsonl"));
        let idle = [assistant(json!([]), Some("end_turn"))];
        let busy = [json!({"type": "user", "cwd": "/w/busy"})];
        let rows = vec![
            session_row(&idle, &p("a"), 100.0, None),
            session_row(&idle, &p("b"), 50.0, Some("b")),
            session_row(&busy, &p("c"), 70.0, Some("b")),
            session_row(&idle, &p("d"), 10.0, None),
            session_row(&idle, &p("e"), 3000.0, None),
        ];
        let (ss, live) = rank(rows);
        assert_eq!(live, 5);
        assert_eq!(
            ss,
            vec![
                ("proj".to_owned(), STATE_WAITING, "needs you".to_owned(), 0),
                ("busy".to_owned(), STATE_WORKING, "Thinking".to_owned(), 1),
                ("proj".to_owned(), STATE_IDLE, String::new(), 0),
                ("proj".to_owned(), STATE_IDLE, String::new(), 1),
            ]
        );
    }

    fn line(id: &str, req: &str, ts: &str, model: &str, sid: &str) -> String {
        json!({"type": "assistant", "timestamp": ts, "requestId": req, "sessionId": sid,
               "message": {"id": id, "model": model,
                           "usage": {"input_tokens": 1000, "output_tokens": 500}}})
        .to_string()
            + "\n"
    }

    #[test]
    fn token_cache_counts_each_message_once() {
        let mut c = TokenCache::default();
        c.roll("2026-10-16");
        let text = [
            line("m1", "r1", "2026-10-16T08:00:00.000Z", "claude-sonnet-4-5", "s1"),
            line("m1", "r1", "2026-10-16T08:00:01.000Z", "claude-sonnet-4-5", "s1"), // same message, next block
            line("m2", "r2", "2026-10-15T23:30:00Z", "claude-sonnet-4-5", "s2"),     // yesterday in UTC...
            line("m3", "r3", "2026-10-16T09:00:00Z", "<synthetic>", "s3"),
            "{\"type\":\"user\"}\nnot json\n".to_owned(),
        ]
        .concat();
        c.add_lines(text.as_bytes(), 0);
        assert_eq!((c.tk, c.ss.len()), (1500, 1));
        assert!((c.co - (1000.0 * 3.0 + 500.0 * 15.0) / 1e6).abs() < 1e-12);
        // ...but today at UTC+1.
        let mut c = TokenCache::default();
        c.roll("2026-10-16");
        c.add_lines(text.as_bytes(), 3600);
        assert_eq!((c.tk, c.ss.len()), (3000, 2));
        // A new day starts over.
        c.roll("2026-10-17");
        assert_eq!((c.tk, c.seen.len(), c.ss.len()), (0, 0, 0));
    }

    #[test]
    fn collect_from_transcripts() {
        let root = std::env::temp_dir().join(format!("cm-activity-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        let acct = root.join(".claude");
        let proj = acct.join("projects").join("-home-u-app");
        fs::create_dir_all(proj.join("s1").join("subagents")).unwrap();
        let now_epoch = std::time::SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs_f64();
        let ts = DateTime::<Utc>::from_timestamp(now_epoch as i64, 0).unwrap().to_rfc3339();
        let main = proj.join("s1.jsonl");
        let mut text = line("m1", "r1", &ts, "claude-opus-4-6", "s1");
        text.push_str(&json!({"type": "user", "cwd": "/home/u/app", "message": {"content": "go"}}).to_string());
        text.push('\n');
        fs::write(&main, &text).unwrap();
        fs::write(
            proj.join("s1").join("subagents").join("agent-1.jsonl"),
            line("m9", "r9", &ts, "claude-haiku-4-5", "s1"),
        )
        .unwrap();
        // A half-written last line waits for its newline.
        fs::write(proj.join("s2.jsonl"), line("m5", "r5", &ts, "claude-opus-4-6", "s2").trim_end()).unwrap();
        let cache = root.join("state").join("activity-cache.json");
        let off = crate::platform::utc_offset_secs();
        let p = collect(std::slice::from_ref(&acct), now_epoch + 5.0, off, Some("s2"), &cache).unwrap();
        assert_eq!(p.nl, 2);
        assert_eq!(p.ss[0], ("-home-u-app".to_owned(), STATE_WAITING, "needs you".to_owned(), 0));
        assert_eq!(p.ss[1], ("app".to_owned(), STATE_WORKING, "Thinking".to_owned(), 0));
        assert_eq!((p.tk, p.ns), (3000, 1));
        // 1000×5 + 500×25 (opus) + 1000×1 + 500×5 (haiku), per million: 2.1 cents.
        assert_eq!(p.co, 2);
        // Run again: nothing new is counted twice.
        let again = collect(std::slice::from_ref(&acct), now_epoch + 6.0, off, None, &cache).unwrap();
        assert_eq!((again.tk, again.ns), (3000, 1));
        assert!(again.ss.iter().all(|r| r.1 == STATE_WORKING), "{:?}", again.ss);
        let json = String::from_utf8(again.encode()).unwrap();
        assert!(
            json.starts_with(r#"{"k":"a","ss":[["#) && json.ends_with(r#""nl":2,"tk":3000,"co":2,"ns":1}"#),
            "{json}"
        );
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn the_biggest_write_fits() {
        let worst = ("\"".repeat(18), STATE_WAITING, "\\".repeat(12), 59);
        let p = ActivityPayload { k: "a", ss: vec![worst; MAX_SESSIONS], nl: 999, tk: u64::MAX, co: i64::MAX, ns: 999 };
        let n = p.encode().len();
        assert!(n <= WRITE_LIMIT, "{n} bytes");
        assert_eq!(serde_json::from_slice::<Value>(&p.encode()).unwrap()["ss"].as_array().unwrap().len(), MAX_SESSIONS);
        // Something oversized loses rows rather than overflowing.
        let huge = ActivityPayload { ss: vec![("x".repeat(200), 1, String::new(), 0); 4], ..p };
        let out = huge.encode();
        assert!(out.len() <= WRITE_LIMIT);
    }

    #[test]
    fn local_days() {
        assert_eq!(local_date(1_791_892_800, 0), "2026-10-13");
        assert_eq!(local_date(1_791_892_800, 13 * 3600), "2026-10-14");
        assert_eq!(local_midnight(1_791_892_800, 0), 1_791_849_600);
        assert_eq!(local_midnight(1_791_892_800, -13 * 3600), 1_791_849_600 - 86_400 + 13 * 3600);
    }
}
