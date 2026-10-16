//! Claude's service status for the device's status line: the Statuspage
//! summary behind status.claude.com → one `{"k":"ss", ...}` write. Checked
//! every 5 minutes; a failed fetch retries after 2. Everything but [`fetch`]
//! is pure.

use serde::Serialize;
use serde_json::Value;

use crate::feeds::{agent, fold_ascii, get_json, Schedule};

/// Every key [`StatusPayload`] can emit, for the firmware cross-check.
pub const STATUS_KEYS: &[&str] = &["k", "i", "m"];

/// The Statuspage summary.
pub const STATUS_URL: &str = "https://status.claude.com/api/v2/summary.json";
/// Seconds between fetches.
pub const INTERVAL_SECS: i64 = 5 * 60;
/// Seconds before a failed fetch is tried again.
pub const RETRY_SECS: i64 = 2 * 60;
/// Longest message.
const MESSAGE_MAX: usize = 48;

/// One status write.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct StatusPayload {
    /// Always `"ss"`.
    pub k: &'static str,
    /// `none`, `minor`, `major`, `critical` or `maintenance`.
    pub i: String,
    /// The open incident's name, or `"<component> <state>"`; empty when all
    /// is well (≤ 48 ASCII).
    pub m: String,
}

impl StatusPayload {
    /// Compact JSON for the RX characteristic.
    pub fn encode(&self) -> Vec<u8> {
        serde_json::to_vec(self).unwrap_or_default()
    }
}

/// How a component's state reads on the device.
fn component_word(status: &str) -> Option<&'static str> {
    match status {
        "degraded_performance" => Some("degraded"),
        "partial_outage" => Some("partial outage"),
        "major_outage" => Some("outage"),
        "under_maintenance" => Some("maintenance"),
        _ => None,
    }
}

/// The write from a Statuspage `summary.json`: the overall indicator, and
/// the first unresolved incident's name or else the first component that
/// isn't operational (which lifts a `none` indicator to `minor`, or
/// `maintenance`).
pub fn payload_from_summary(summary: &Value) -> StatusPayload {
    let s = |v: Option<&Value>| v.and_then(Value::as_str).unwrap_or("").to_owned();
    let mut ind = s(summary.get("status").and_then(|st| st.get("indicator")));
    if ind.is_empty() {
        ind = "none".to_owned();
    }
    let incidents = summary.get("incidents").and_then(Value::as_array).cloned().unwrap_or_default();
    let mut msg = incidents
        .iter()
        .find(|inc| !matches!(inc.get("status").and_then(Value::as_str), Some("resolved" | "postmortem")))
        .map(|inc| s(inc.get("name")))
        .unwrap_or_default();
    if msg.is_empty() {
        let components = summary.get("components").and_then(Value::as_array).cloned().unwrap_or_default();
        let hit = components.iter().find_map(|c| {
            let word = component_word(c.get("status").and_then(Value::as_str).unwrap_or(""))?;
            let group = c.get("group").and_then(Value::as_bool).unwrap_or(false);
            (!group).then(|| (s(c.get("name")), word))
        });
        if let Some((name, word)) = hit {
            msg = format!("{name} {word}");
            if ind == "none" {
                ind = if word == "maintenance" { "maintenance" } else { "minor" }.to_owned();
            }
        }
    }
    if ind == "none" {
        msg.clear();
    }
    StatusPayload { k: "ss", i: fold_ascii(&ind, 16), m: fold_ascii(&msg, MESSAGE_MAX) }
}

/// When to fetch.
pub fn schedule() -> Schedule {
    Schedule::every(INTERVAL_SECS, RETRY_SECS)
}

/// Fetch the summary (blocking: run it off the daemon's loop).
pub fn fetch() -> Result<StatusPayload, String> {
    let summary = get_json(&agent(), STATUS_URL, &crate::oauth::user_agent())?;
    Ok(payload_from_summary(&summary))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn all_good() {
        let s = json!({"status": {"indicator": "none", "description": "All Systems Operational"},
                       "components": [{"name": "claude.ai", "status": "operational", "group": false}],
                       "incidents": []});
        let p = payload_from_summary(&s);
        assert_eq!(String::from_utf8(p.encode()).unwrap(), r#"{"k":"ss","i":"none","m":""}"#);
    }

    #[test]
    fn an_open_incident_names_it() {
        let s = json!({"status": {"indicator": "major"},
                       "incidents": [{"name": "Old one", "status": "resolved"},
                                     {"name": "Elevated errors on Claude Opus 5.5 for some users in Europe — investigating",
                                      "status": "investigating"}],
                       "components": [{"name": "API", "status": "major_outage"}]});
        let p = payload_from_summary(&s);
        assert_eq!(p.i, "major");
        assert_eq!(p.m, "Elevated errors on Claude Opus 5.5 for some user");
        assert_eq!(p.m.len(), 48);
    }

    #[test]
    fn a_component_lifts_none() {
        let s = json!({"status": {"indicator": "none"},
                       "components": [{"name": "Group", "status": "partial_outage", "group": true},
                                      {"name": "Claude Code", "status": "degraded_performance", "group": false}]});
        let p = payload_from_summary(&s);
        assert_eq!((p.i.as_str(), p.m.as_str()), ("minor", "Claude Code degraded"));
        let s = json!({"status": {"indicator": "none"},
                       "components": [{"name": "Console", "status": "under_maintenance"}]});
        let p = payload_from_summary(&s);
        assert_eq!((p.i.as_str(), p.m.as_str()), ("maintenance", "Console maintenance"));
        // An indicator already set is kept.
        let s = json!({"status": {"indicator": "critical"},
                       "components": [{"name": "API", "status": "major_outage"}]});
        assert_eq!(payload_from_summary(&s).m, "API outage");
    }

    #[test]
    fn missing_fields() {
        let p = payload_from_summary(&json!({}));
        assert_eq!((p.i.as_str(), p.m.as_str()), ("none", ""));
    }
}
