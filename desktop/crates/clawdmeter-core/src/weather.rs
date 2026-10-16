//! The device's Weather page: Open-Meteo (no account, no key) → one
//! `{"k":"wx", ...}` write, separate from the usage payload (the firmware
//! routes on `k`; `firmware/src/weather.cpp`).
//!
//! Off until the config names a place (`weather_location`, optionally pinned
//! with `weather_coords = <lat>, <lon>`). Refreshed every 15 minutes; a failed
//! fetch retries after 2. Everything but [`fetch`] is pure.

use chrono::{Datelike, NaiveDate, NaiveDateTime};
use serde::Serialize;
use serde_json::Value;

use crate::attention::to_ascii;
use crate::feeds::{agent, encode_component, get_json, Schedule};

/// Forecast tiles under today's reading (`WX_FORECAST_DAYS` in firmware).
pub const FORECAST_DAYS: usize = 3;
/// Seconds between fetches.
pub const INTERVAL_SECS: i64 = 15 * 60;
/// Seconds before a failed fetch is tried again.
pub const RETRY_SECS: i64 = 2 * 60;
/// A 15-minute slot with at least this much precipitation counts as rain.
const RAIN_MM: f64 = 0.1;
/// Slots the rain nowcast looks ahead (one hour).
const NOWCAST_SLOTS: usize = 4;
const DAY_NAMES: [&str; 7] = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];

/// Every key [`WeatherPayload`] can emit, for the firmware cross-check.
pub const WEATHER_KEYS: &[&str] = &["k", "loc", "t", "fl", "c", "d", "hi", "lo", "f", "sun", "ws", "uv", "aq", "rn"];

/// One weather write. `None` fields are omitted.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct WeatherPayload {
    /// Always `"wx"`: routes the write to the Weather page.
    pub k: &'static str,
    /// Place name (ASCII).
    pub loc: String,
    /// Temperature now, °C, one decimal.
    pub t: f64,
    /// Feels like, °C.
    pub fl: i64,
    /// WMO weather code now.
    pub c: i64,
    /// 1 = daytime (sun or moon icons).
    pub d: u8,
    /// Today's high, °C.
    pub hi: i64,
    /// Today's low, °C.
    pub lo: i64,
    /// The next days: `[day, code, high, low]`.
    pub f: Vec<(String, i64, i64, i64)>,
    /// The next sun event, `"Sunset 18:45"`; empty when unknown.
    pub sun: String,
    /// Wind, km/h.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ws: Option<i64>,
    /// UV index.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub uv: Option<i64>,
    /// European air quality index.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub aq: Option<i64>,
    /// Rain in the next hour, `"Rain in 30 min"`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub rn: Option<String>,
}

impl WeatherPayload {
    /// Compact JSON for the RX characteristic.
    pub fn encode(&self) -> Vec<u8> {
        serde_json::to_vec(self).unwrap_or_default()
    }
}

/// Where to look the weather up (`weather_location`, `weather_coords`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct WeatherPlace {
    /// The name the user wrote; shown as-is when coordinates pin it.
    pub name: String,
    /// `weather_coords`, skipping the geocoder.
    pub coords: Option<Coords>,
}

/// A latitude / longitude in whole microdegrees (so settings compare exactly).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Coords {
    /// Latitude × 10⁶.
    pub lat_e6: i32,
    /// Longitude × 10⁶.
    pub lon_e6: i32,
}

impl Coords {
    /// Degrees.
    pub fn degrees(self) -> (f64, f64) {
        (self.lat_e6 as f64 / 1e6, self.lon_e6 as f64 / 1e6)
    }
    /// As the config file writes it, `"52.23, 21.01"`.
    pub fn as_config(self) -> String {
        let (lat, lon) = self.degrees();
        format!("{lat}, {lon}")
    }
}

/// `"46.956, 8.339"` → coordinates; anything else → `None`.
pub fn parse_coords(v: &str) -> Option<Coords> {
    let (a, b) = v.split_once(',')?;
    let lat: f64 = a.trim().parse().ok()?;
    let lon: f64 = b.trim().parse().ok()?;
    ((-90.0..=90.0).contains(&lat) && (-180.0..=180.0).contains(&lon))
        .then(|| Coords { lat_e6: (lat * 1e6).round() as i32, lon_e6: (lon * 1e6).round() as i32 })
}

fn local_time(s: &str) -> Option<NaiveDateTime> {
    NaiveDateTime::parse_from_str(s, "%Y-%m-%dT%H:%M").ok()
}

fn num(v: &Value) -> Option<f64> {
    v.as_f64()
}

/// `"Sunset 18:45"` / `"Sunrise 07:41"`, whichever comes after `now` (local
/// ISO times, as Open-Meteo returns them with `timezone=auto`).
pub fn next_sun_event(now: &str, sunrises: &[Value], sunsets: &[Value]) -> String {
    let Some(now) = local_time(now) else { return String::new() };
    let mut events: Vec<(NaiveDateTime, &str)> = Vec::new();
    for (list, name) in [(sunrises, "Sunrise"), (sunsets, "Sunset")] {
        for t in list.iter().filter_map(Value::as_str).filter_map(local_time) {
            events.push((t, name));
        }
    }
    events.sort();
    events
        .into_iter()
        .find(|(t, _)| *t > now)
        .map(|(t, name)| format!("{name} {}", t.format("%H:%M")))
        .unwrap_or_default()
}

/// Rain in the next hour from Open-Meteo's `minutely_15` block: `"Rain in 30
/// min"`, `"Rain stops in 15 min"`, `"Rain for the next hour"`, or `None`
/// when it stays dry.
pub fn rain_nowcast(now: &str, minutely: Option<&Value>) -> Option<String> {
    let now = local_time(now)?;
    let m = minutely?;
    let times = m.get("time")?.as_array()?;
    let rain = m.get("precipitation")?.as_array()?;
    let mut slots: Vec<(i64, bool)> = Vec::new();
    for (t, mm) in times.iter().zip(rain) {
        let Some(start) = t.as_str().and_then(local_time) else { continue };
        if start + chrono::Duration::minutes(15) <= now {
            continue; // already over
        }
        let mins = ((start - now).num_seconds().max(0) + 30) / 60;
        slots.push((mins, num(mm).unwrap_or(0.0) >= RAIN_MM));
        if slots.len() >= NOWCAST_SLOTS {
            break;
        }
    }
    let first = slots.first()?;
    if first.1 {
        return Some(match slots[1..].iter().find(|(_, wet)| !wet) {
            Some((mins, _)) => format!("Rain stops in {mins} min"),
            None => "Rain for the next hour".to_owned(),
        });
    }
    slots.iter().find(|(_, wet)| *wet).map(|(mins, _)| format!("Rain in {mins} min"))
}

/// The weather write from an Open-Meteo forecast response, for `label`. `aqi`
/// is the European AQI from the air-quality endpoint, when it answered.
/// `None` when the response lacks the current reading.
pub fn payload_from_forecast(forecast: &Value, label: &str, aqi: Option<f64>) -> Option<WeatherPayload> {
    let cur = forecast.get("current")?;
    let daily = forecast.get("daily")?;
    let arr = |k: &str| daily.get(k).and_then(Value::as_array).cloned().unwrap_or_default();
    let (times, codes, highs, lows) =
        (arr("time"), arr("weather_code"), arr("temperature_2m_max"), arr("temperature_2m_min"));
    let round = |v: Option<&Value>| v.and_then(num).map(|x| x.round() as i64);
    let mut days = Vec::new();
    for (i, day) in times.iter().enumerate().skip(1).take(FORECAST_DAYS) {
        let Some(date) = day.as_str().and_then(|s| NaiveDate::parse_from_str(s, "%Y-%m-%d").ok()) else {
            continue;
        };
        days.push((
            DAY_NAMES[date.weekday().num_days_from_monday() as usize].to_owned(),
            round(codes.get(i)).unwrap_or(0),
            round(highs.get(i)).unwrap_or(0),
            round(lows.get(i)).unwrap_or(0),
        ));
    }
    let now = cur.get("time").and_then(Value::as_str).unwrap_or("");
    Some(WeatherPayload {
        k: "wx",
        loc: to_ascii(label, 20),
        t: (num(cur.get("temperature_2m")?)? * 10.0).round() / 10.0,
        fl: round(cur.get("apparent_temperature")).unwrap_or(0),
        c: round(cur.get("weather_code")).unwrap_or(0),
        d: u8::from(cur.get("is_day").and_then(num).unwrap_or(1.0) != 0.0),
        hi: round(highs.first()).unwrap_or(0),
        lo: round(lows.first()).unwrap_or(0),
        f: days,
        sun: next_sun_event(now, &arr("sunrise"), &arr("sunset")),
        ws: round(cur.get("wind_speed_10m")),
        uv: round(cur.get("uv_index")),
        aq: aqi.map(|a| a.round() as i64),
        rn: rain_nowcast(now, forecast.get("minutely_15")),
    })
}

/// When to fetch: every 15 minutes, 2 after a failure.
pub fn schedule() -> Schedule {
    Schedule::every(INTERVAL_SECS, RETRY_SECS)
}

fn get(agent: &ureq::Agent, url: &str) -> Result<Value, String> {
    get_json(agent, url, &crate::oauth::user_agent())
}

/// Look the place up and fetch the reading (blocking: run it off the
/// daemon's loop). The air quality is a second call; the weather still goes
/// out without it.
pub fn fetch(place: &WeatherPlace) -> Result<WeatherPayload, String> {
    let agent = agent();
    let (lat, lon, label) = match place.coords.map(Coords::degrees) {
        Some((lat, lon)) => (lat, lon, place.name.clone()),
        None => {
            let query = place.name.split(',').next().unwrap_or("").trim();
            let url = format!(
                "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name={}",
                encode_component(query)
            );
            let hit = get(&agent, &url)?
                .get("results")
                .and_then(|r| r.get(0))
                .cloned()
                .ok_or_else(|| format!("no place called {query:?}"))?;
            let lat = hit.get("latitude").and_then(Value::as_f64).ok_or("geocoder: no latitude")?;
            let lon = hit.get("longitude").and_then(Value::as_f64).ok_or("geocoder: no longitude")?;
            let name = hit.get("name").and_then(Value::as_str).unwrap_or(query).to_owned();
            (lat, lon, name)
        }
    };
    let url = format!(
        "https://api.open-meteo.com/v1/forecast?timezone=auto&forecast_days={}&latitude={lat}&longitude={lon}\
         &current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m,uv_index\
         &minutely_15=precipitation&forecast_minutely_15=8\
         &daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset",
        FORECAST_DAYS + 1
    );
    let forecast = get(&agent, &url)?;
    let aqi = get(
        &agent,
        &format!(
            "https://air-quality-api.open-meteo.com/v1/air-quality?latitude={lat}&longitude={lon}&current=european_aqi"
        ),
    )
    .ok()
    .and_then(|v| v.get("current").and_then(|c| c.get("european_aqi")).and_then(Value::as_f64));
    payload_from_forecast(&forecast, &label, aqi).ok_or_else(|| "forecast without a current reading".to_owned())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    /// The firmware's RX buffer (`firmware/src/ble.cpp`).
    const FIRMWARE_BUF: usize = 512;

    fn forecast() -> Value {
        json!({
            "current": {"time": "2026-10-12T19:00", "temperature_2m": 11.34, "apparent_temperature": 9.8,
                        "weather_code": 2, "is_day": 1, "wind_speed_10m": 12.6, "uv_index": 3.4},
            "minutely_15": {"time": ["2026-10-12T18:45", "2026-10-12T19:00", "2026-10-12T19:15",
                                     "2026-10-12T19:30", "2026-10-12T19:45"],
                            "precipitation": [0.0, 0.0, 0.0, 0.4, 0.2]},
            "daily": {"time": ["2026-10-12", "2026-10-13", "2026-10-14", "2026-10-15"],
                      "weather_code": [3, 80, 61, 0],
                      "temperature_2m_max": [13.4, 15.3, 17.8, 17.6],
                      "temperature_2m_min": [6.7, 9.1, 12.1, -0.6],
                      "sunrise": ["2026-10-12T07:40", "2026-10-13T07:41"],
                      "sunset": ["2026-10-12T18:45", "2026-10-13T18:43"]}
        })
    }

    #[test]
    fn payload_from_a_forecast() {
        let p = payload_from_forecast(&forecast(), "Kraków", Some(23.6)).unwrap();
        assert_eq!((p.k, p.loc.as_str()), ("wx", "Krakow"));
        assert_eq!((p.t, p.fl, p.c, p.d, p.hi, p.lo), (11.3, 10, 2, 1, 13, 7));
        assert_eq!(
            p.f,
            vec![("Tue".to_owned(), 80, 15, 9), ("Wed".to_owned(), 61, 18, 12), ("Thu".to_owned(), 0, 18, -1)]
        );
        assert_eq!(p.sun, "Sunrise 07:41"); // after today's sunset
        assert_eq!((p.ws, p.uv, p.aq), (Some(13), Some(3), Some(24)));
        assert_eq!(p.rn.as_deref(), Some("Rain in 30 min"));
        assert!(p.encode().len() < FIRMWARE_BUF);
    }

    #[test]
    fn optional_fields_stay_out() {
        let mut f = forecast();
        f["current"].as_object_mut().unwrap().remove("wind_speed_10m");
        f["current"].as_object_mut().unwrap().remove("uv_index");
        f.as_object_mut().unwrap().remove("minutely_15");
        let p = payload_from_forecast(&f, "X", None).unwrap();
        let json = String::from_utf8(p.encode()).unwrap();
        for k in ["\"ws\"", "\"uv\"", "\"aq\"", "\"rn\""] {
            assert!(!json.contains(k), "{k} in {json}");
        }
        assert!(json.starts_with("{\"k\":\"wx\""), "{json}");
    }

    #[test]
    fn no_current_reading_is_no_payload() {
        assert!(payload_from_forecast(&json!({"daily": {}}), "X", None).is_none());
    }

    #[test]
    fn rain_nowcast_wording() {
        let m = |mm: [f64; 4]| {
            json!({"time": ["2026-10-12T19:00", "2026-10-12T19:15", "2026-10-12T19:30", "2026-10-12T19:45"],
                   "precipitation": mm})
        };
        let now = "2026-10-12T19:00";
        assert_eq!(rain_nowcast(now, Some(&m([0.0, 0.0, 0.0, 0.0]))), None);
        assert_eq!(rain_nowcast(now, Some(&m([0.0, 0.3, 0.0, 0.0]))).as_deref(), Some("Rain in 15 min"));
        assert_eq!(rain_nowcast(now, Some(&m([0.5, 0.5, 0.0, 0.0]))).as_deref(), Some("Rain stops in 30 min"));
        assert_eq!(rain_nowcast(now, Some(&m([1.0, 1.0, 1.0, 1.0]))).as_deref(), Some("Rain for the next hour"));
        assert_eq!(rain_nowcast(now, None), None);
    }

    #[test]
    fn sun_events() {
        let rises = [json!("2026-10-12T07:40"), json!("2026-10-13T07:41")];
        let sets = [json!("2026-10-12T18:45"), json!("2026-10-13T18:43")];
        assert_eq!(next_sun_event("2026-10-12T06:00", &rises, &sets), "Sunrise 07:40");
        assert_eq!(next_sun_event("2026-10-12T12:00", &rises, &sets), "Sunset 18:45");
        assert_eq!(next_sun_event("2026-10-14T12:00", &rises, &sets), "");
    }

    #[test]
    fn coords() {
        let c = parse_coords(" 52.23, 21.01 ").unwrap();
        assert_eq!(c.degrees(), (52.23, 21.01));
        assert_eq!(c.as_config(), "52.23, 21.01");
        assert_eq!(parse_coords("52.23"), None);
        assert_eq!(parse_coords("95, 10"), None);
    }
}
