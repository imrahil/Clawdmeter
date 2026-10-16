//! The device's Stocks page: Yahoo Finance's chart endpoint (unofficial, no
//! key) → one `{"k":"q", ...}` write per symbol of the `stocks` setting
//! (`AMD, NVDA=NVIDIA, R3NK.DE=Renk`; up to 5). Refreshed every 5 minutes; a
//! symbol that fails keeps its last write, and only all of them failing
//! retries after 2. Everything but [`fetch`] is pure.

use serde::Serialize;
use serde_json::Value;

use crate::feeds::{agent, encode_component, fold_ascii, get_json, py_round, round2, Schedule};

/// Every key [`QuotePayload`] can emit, for the firmware cross-check.
pub const STOCK_KEYS: &[&str] = &["k", "i", "n", "sy", "nm", "cu", "p", "pc", "o", "h", "b", "fr"];

/// Symbols shown (`STOCK_MAX` in firmware).
pub const MAX_STOCKS: usize = 5;
/// Sparkline points (`STOCK_HIST_MAX` in firmware).
pub const HIST_POINTS: usize = 32;
/// The chart's bar length; keep in step with the `interval` in [`fetch`].
const BAR_SECONDS: i64 = 900;
/// Seconds between fetches.
pub const INTERVAL_SECS: i64 = 5 * 60;
/// Seconds before a fetch where every symbol failed is tried again.
pub const RETRY_SECS: i64 = 2 * 60;
/// Yahoo turns away unknown clients; this is the one the old daemon used.
const USER_AGENT: &str = "Mozilla/5.0 (X11; Linux x86_64) Clawdmeter";

/// Suffix words dropped from Yahoo's company names so they fit the row.
const NAME_NOISE: &[&str] = &[
    "inc",
    "inc.",
    "corp",
    "corp.",
    "corporation",
    "ag",
    "se",
    "plc",
    "n.v.",
    "nv",
    "s.a.",
    "sa",
    "ltd",
    "ltd.",
    "co",
    "co.",
    "holdings",
    "i",
    "a",
    "class",
];

/// One symbol of the `stocks` setting.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Stock {
    /// Yahoo symbol, upper case (`R3NK.DE`, `^GSPC`).
    pub symbol: String,
    /// The name to show instead of Yahoo's.
    pub label: Option<String>,
}

/// `"AMD, NVDA=NVIDIA, R3NK.DE=Renk"` → up to [`MAX_STOCKS`] symbols.
pub fn parse_spec(spec: &str) -> Vec<Stock> {
    spec.split(',')
        .map(str::trim)
        .filter(|i| !i.is_empty())
        .filter_map(|item| {
            let (sym, label) = item.split_once('=').unwrap_or((item, ""));
            let symbol = sym.trim().to_uppercase();
            let label = Some(label.trim()).filter(|l| !l.is_empty()).map(str::to_owned);
            (!symbol.is_empty()).then_some(Stock { symbol, label })
        })
        .take(MAX_STOCKS)
        .collect()
}

/// The setting's spelling ([`parse_spec`] reads it back).
pub fn as_config(stocks: &[Stock]) -> String {
    stocks
        .iter()
        .map(|s| match &s.label {
            Some(l) => format!("{}={l}", s.symbol),
            None => s.symbol.clone(),
        })
        .collect::<Vec<_>>()
        .join(", ")
}

/// One quote write.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct QuotePayload {
    /// Always `"q"`.
    pub k: &'static str,
    /// This symbol's place, from 0.
    pub i: usize,
    /// How many symbols there are.
    pub n: usize,
    /// Symbol without its exchange suffix or index caret (≤ 8).
    pub sy: String,
    /// Name: the label, or Yahoo's minus legal suffixes (≤ 20).
    pub nm: String,
    /// Currency as Yahoo writes it (`GBp` is pence).
    pub cu: String,
    /// Price now.
    pub p: f64,
    /// Previous close.
    pub pc: f64,
    /// 1 = the market is open.
    pub o: u8,
    /// Today's closes, ≤ 32 points scaled 0..99 over a range that includes
    /// the previous close.
    pub h: Vec<i64>,
    /// The previous close on the same scale (the dim baseline).
    pub b: i64,
    /// % of the trading day the sparkline covers (100 when closed).
    pub fr: i64,
}

impl QuotePayload {
    /// Compact JSON for the RX characteristic.
    pub fn encode(&self) -> Vec<u8> {
        serde_json::to_vec(self).unwrap_or_default()
    }
}

/// Yahoo's long company name, minus legal suffixes, cut at a word boundary.
pub fn clean_name(name: &str, limit: usize) -> String {
    let folded = fold_ascii(name, 60);
    let mut words: Vec<&str> = folded.split(',').next().unwrap_or("").split_whitespace().collect();
    while words.last().is_some_and(|w| NAME_NOISE.contains(&w.to_lowercase().as_str())) {
        words.pop();
    }
    let mut out = String::new();
    for w in &words {
        if out.len() + w.len() + usize::from(!out.is_empty()) > limit {
            break;
        }
        if !out.is_empty() {
            out.push(' ');
        }
        out.push_str(w);
    }
    if out.is_empty() {
        words.join(" ").chars().take(limit).collect()
    } else {
        out
    }
}

/// Exchange suffixes (`R3NK.DE`) are noise on the device; indices lose
/// their caret.
pub fn display_symbol(sym: &str) -> String {
    sym.split('.').next().unwrap_or("").trim_start_matches('^').chars().take(8).collect()
}

/// At most `n` evenly spaced values.
pub fn resample(values: &[f64], n: usize) -> Vec<f64> {
    if values.len() <= n {
        return values.to_vec();
    }
    let step = (values.len() - 1) as f64 / (n - 1) as f64;
    (0..n).map(|i| values[py_round(i as f64 * step) as usize]).collect()
}

/// The write for one symbol from a Yahoo chart response, as of `now` (unix
/// seconds). `None` when the response has no price.
pub fn payload_from_chart(
    chart: &Value,
    index: usize,
    count: usize,
    label: Option<&str>,
    now: f64,
) -> Option<QuotePayload> {
    let res = chart.get("chart")?.get("result")?.get(0)?;
    let meta = res.get("meta")?;
    let num = |k: &str| meta.get(k).and_then(Value::as_f64);
    let price = num("regularMarketPrice")?;
    let prev =
        [num("chartPreviousClose"), num("previousClose")].into_iter().flatten().find(|v| *v != 0.0).unwrap_or(price);
    let closes: Vec<f64> = res
        .get("indicators")
        .and_then(|i| i.get("quote"))
        .and_then(|q| q.get(0))
        .and_then(|q| q.get("close"))
        .and_then(Value::as_array)
        .map(|a| a.iter().filter_map(Value::as_f64).collect())
        .unwrap_or_default();
    let regular = meta
        .get("currentTradingPeriod")
        .and_then(|p| p.get("regular"))
        .filter(|r| r.as_object().is_some_and(|o| !o.is_empty()));
    let edge = |k: &str| regular.and_then(|r| r.get(k)).and_then(Value::as_i64).unwrap_or(0);
    let (start, end) = (edge("start"), edge("end"));
    let is_open = regular.is_some() && start as f64 <= now && now < end as f64;
    // Mid-session the sparkline covers only the elapsed share of the day.
    let session = if is_open {
        let bars = ((end - start).div_euclid(BAR_SECONDS)).max(1);
        py_round(closes.len() as f64 / bars as f64 * 100.0).clamp(1, 100)
    } else {
        100
    };
    let closes = resample(&closes, HIST_POINTS);
    // Scale to 0..99 over a range that includes the previous close, so the
    // baseline lands on the same scale.
    let lo = closes.iter().copied().fold(prev, f64::min);
    let hi = closes.iter().copied().fold(prev, f64::max);
    let span = if hi - lo == 0.0 { 1.0 } else { hi - lo };
    let scale = |v: f64| py_round((v - lo) / span * 99.0);
    let s = |k: &str| meta.get(k).and_then(Value::as_str).unwrap_or("");
    let nm = match label {
        Some(l) => fold_ascii(l, 20),
        None => clean_name([s("shortName"), s("longName")].into_iter().find(|n| !n.is_empty()).unwrap_or(""), 20),
    };
    Some(QuotePayload {
        k: "q",
        i: index,
        n: count,
        sy: display_symbol(s("symbol")),
        nm,
        cu: fold_ascii(s("currency"), 3),
        p: round2(price),
        pc: round2(prev),
        o: u8::from(is_open),
        h: closes.iter().map(|c| scale(*c)).collect(),
        b: scale(prev),
        fr: session,
    })
}

/// When to fetch.
pub fn schedule() -> Schedule {
    Schedule::every(INTERVAL_SECS, RETRY_SECS)
}

/// Fetch one symbol (blocking: run it off the daemon's loop).
pub fn fetch(stock: &Stock, index: usize, count: usize) -> Result<QuotePayload, String> {
    // 15-minute bars: keep in step with BAR_SECONDS.
    let url = format!(
        "https://query1.finance.yahoo.com/v8/finance/chart/{}?range=1d&interval=15m",
        encode_component(&stock.symbol)
    );
    let chart = get_json(&agent(), &url, USER_AGENT)?;
    let now =
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs_f64()).unwrap_or(0.0);
    payload_from_chart(&chart, index, count, stock.label.as_deref(), now)
        .ok_or_else(|| format!("{}: no price in the chart", stock.symbol))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::feeds::WRITE_LIMIT;
    use serde_json::json;

    #[test]
    fn spec() {
        let s = parse_spec(" amd, NVDA=NVIDIA ,, R3NK.DE = Renk Group, =Nope, a, b, c, d");
        assert_eq!(
            s[..3],
            [
                Stock { symbol: "AMD".into(), label: None },
                Stock { symbol: "NVDA".into(), label: Some("NVIDIA".into()) },
                Stock { symbol: "R3NK.DE".into(), label: Some("Renk Group".into()) },
            ]
        );
        assert_eq!(s.len(), MAX_STOCKS);
        assert_eq!(as_config(&s), "AMD, NVDA=NVIDIA, R3NK.DE=Renk Group, A, B");
        assert_eq!(parse_spec(&as_config(&s)), s);
        assert!(parse_spec("  ").is_empty());
    }

    #[test]
    fn names_and_symbols() {
        assert_eq!(clean_name("Advanced Micro Devices, Inc.", 20), "Advanced Micro");
        assert_eq!(clean_name("NVIDIA Corporation", 20), "NVIDIA");
        assert_eq!(clean_name("Société Générale S.A.", 20), "Societe Generale");
        assert_eq!(clean_name("Supercalifragilisticexpialidocious Inc", 20), "Supercalifragilistic");
        assert_eq!(display_symbol("R3NK.DE"), "R3NK");
        assert_eq!(display_symbol("^GSPC"), "GSPC");
        assert_eq!(display_symbol("BRKBRKBRKB"), "BRKBRKBR");
    }

    #[test]
    fn resampling() {
        let v: Vec<f64> = (0..10).map(f64::from).collect();
        assert_eq!(resample(&v, 32), v);
        assert_eq!(resample(&v, 4), vec![0.0, 3.0, 6.0, 9.0]);
        let long: Vec<f64> = (0..100).map(f64::from).collect();
        let r = resample(&long, HIST_POINTS);
        assert_eq!((r.len(), r[0], r[31]), (32, 0.0, 99.0));
    }

    fn chart(closes: Value, start: i64, end: i64) -> Value {
        json!({"chart": {"result": [{
            "meta": {"symbol": "R3NK.DE", "currency": "EUR", "shortName": "RENK Group AG",
                     "regularMarketPrice": 61.234, "chartPreviousClose": 60.0,
                     "currentTradingPeriod": {"regular": {"start": start, "end": end}}},
            "indicators": {"quote": [{"close": closes}]}
        }]}})
    }

    #[test]
    fn an_open_market() {
        let start = 1_791_900_000;
        let c = chart(json!([60.5, null, 59.0, 62.0]), start, start + 8 * 3600 + 1800);
        let p = payload_from_chart(&c, 1, 3, None, (start + 3600) as f64).unwrap();
        assert_eq!(
            (p.k, p.i, p.n, p.sy.as_str(), p.nm.as_str(), p.cu.as_str()),
            ("q", 1, 3, "R3NK", "RENK Group", "EUR")
        );
        assert_eq!((p.p, p.pc, p.o), (61.23, 60.0, 1));
        // 3 closes of 34 bars.
        assert_eq!(p.fr, 9);
        // lo 59, hi 62: 60.5 → 49.5 → 50 (half to even), 59 → 0, 62 → 99; prev 60 → 33.
        assert_eq!((p.h.clone(), p.b), (vec![50, 0, 99], 33));
        let json = String::from_utf8(p.encode()).unwrap();
        assert!(
            json.starts_with(r#"{"k":"q","i":1,"n":3,"sy":"R3NK","nm":"RENK Group","cu":"EUR","p":61.23"#),
            "{json}"
        );
    }

    #[test]
    fn a_closed_market_and_a_label() {
        let c = chart(json!([60.0, 60.0]), 100, 200);
        let p = payload_from_chart(&c, 0, 1, Some("Renk"), 1_000.0).unwrap();
        assert_eq!((p.o, p.fr, p.nm.as_str()), (0, 100, "Renk"));
        // A flat day: everything at the bottom of the scale.
        assert_eq!((p.h.clone(), p.b), (vec![0, 0], 0));
        // No previous close: the price stands in.
        let mut c = chart(json!([]), 0, 0);
        c["chart"]["result"][0]["meta"].as_object_mut().unwrap().remove("chartPreviousClose");
        c["chart"]["result"][0]["meta"].as_object_mut().unwrap().remove("currentTradingPeriod");
        let p = payload_from_chart(&c, 0, 1, None, 0.0).unwrap();
        assert_eq!((p.pc, p.h.len(), p.o), (61.23, 0, 0));
    }

    #[test]
    fn no_price_is_no_write() {
        assert!(payload_from_chart(
            &json!({"chart": {"result": null, "error": {"code": "Not Found"}}}),
            0,
            1,
            None,
            0.0
        )
        .is_none());
        let mut c = chart(json!([1.0]), 0, 0);
        c["chart"]["result"][0]["meta"].as_object_mut().unwrap().remove("regularMarketPrice");
        assert!(payload_from_chart(&c, 0, 1, None, 0.0).is_none());
    }

    #[test]
    fn the_biggest_write_fits() {
        let closes: Vec<f64> = (0..200).map(|i| 1000.0 + f64::from(i) * 0.37).collect();
        let mut c = chart(json!(closes), 0, 1);
        c["chart"]["result"][0]["meta"]["regularMarketPrice"] = json!(123_456_789.123_456);
        c["chart"]["result"][0]["meta"]["chartPreviousClose"] = json!(-0.000_123_456_789);
        let p = payload_from_chart(&c, 4, 5, Some(&"\"".repeat(30)), 0.0).unwrap();
        assert_eq!(p.h.len(), HIST_POINTS);
        let n = p.encode().len();
        assert!(n <= WRITE_LIMIT, "{n} bytes");
    }
}
