#!/usr/bin/env python3
"""Weather / stock payload builders. Run: python -m pytest daemon/tests/test_extras.py -q"""
import json
import time
from datetime import datetime

from daemon import extras as x

FIRMWARE_BUF = 512  # BLE_BUF_SIZE in firmware/src/ble.cpp


def forecast_fixture():
    return {
        "current": {"time": "2026-10-12T19:00", "temperature_2m": 11.34,
                    "apparent_temperature": 9.8, "weather_code": 2, "is_day": 1},
        "daily": {"time": ["2026-10-12", "2026-10-13", "2026-10-14", "2026-10-15"],
                  "weather_code": [3, 80, 61, 0],
                  "temperature_2m_max": [13.4, 15.3, 17.8, 17.6],
                  "temperature_2m_min": [6.7, 9.1, 12.1, -0.6],
                  "sunrise": ["2026-10-12T07:40", "2026-10-13T07:41", "2026-10-14T07:43", "2026-10-15T07:44"],
                  "sunset": ["2026-10-12T18:45", "2026-10-13T18:43", "2026-10-14T18:41", "2026-10-15T18:39"]},
    }


def chart_fixture(closes, price=110.0, prev=100.0, start=1000, end=2000):
    return {"chart": {"result": [{
        "meta": {"symbol": "R3NK.DE", "currency": "EUR", "regularMarketPrice": price,
                 "chartPreviousClose": prev, "shortName": "RENK Group AG                 I",
                 "currentTradingPeriod": {"regular": {"start": start, "end": end}}},
        "indicators": {"quote": [{"close": closes}]},
    }]}}


def test_weather_payload():
    p = x.weather_payload(forecast_fixture(), "Ennetmöos")
    assert p["k"] == "wx" and p["loc"] == "Ennetmoos"          # folded to ASCII
    assert (p["t"], p["fl"], p["c"], p["d"], p["hi"], p["lo"]) == (11.3, 10, 2, 1, 13, 7)
    assert p["f"] == [["Tue", 80, 15, 9], ["Wed", 61, 18, 12], ["Thu", 0, 18, -1]]
    assert p["sun"] == "Sunrise 07:41"                          # after today's sunset
    assert len(x.compact(p)) < FIRMWARE_BUF


def test_next_sun_event():
    rises, sets = ["2026-10-12T07:40", "2026-10-13T07:41"], ["2026-10-12T18:45", "2026-10-13T18:43"]
    assert x.next_sun_event("2026-10-12T06:00", rises, sets) == "Sunrise 07:40"
    assert x.next_sun_event("2026-10-12T12:00", rises, sets) == "Sunset 18:45"
    assert x.next_sun_event("2026-10-14T12:00", rises, sets) == ""


def test_stock_spec():
    assert x.parse_stock_spec(" amd, NVDA=NVIDIA,,R3NK.DE = Renk ") == [
        ("AMD", None), ("NVDA", "NVIDIA"), ("R3NK.DE", "Renk")]
    assert len(x.parse_stock_spec(",".join("ABCDEFGH"))) == x.MAX_STOCKS


def test_clean_name():
    assert x.clean_name("Advanced Micro Devices, Inc.") == "Advanced Micro"
    assert x.clean_name("RENK Group AG                 I") == "RENK Group"
    assert x.clean_name("Supercalifragilisticexpialidocious") == "Supercalifragilistic"


def test_quote_payload():
    closes = [100.0, None, 105.0, 120.0, 110.0]
    p = x.quote_payload(chart_fixture(closes), 3, 4, now=1500)
    assert (p["k"], p["i"], p["n"], p["sy"], p["nm"], p["cu"]) == ("q", 3, 4, "R3NK", "RENK Group", "EUR")
    assert (p["p"], p["pc"], p["o"]) == (110.0, 100.0, 1)
    assert p["h"] == [0, 25, 99, 50]       # None dropped, scaled over [prev, max]
    assert p["b"] == 0                      # previous close is the low here
    closed = x.quote_payload(chart_fixture(closes), 0, 1, label="Renk", now=2500)
    assert (closed["o"], closed["fr"]) == (0, 100)
    assert x.quote_payload(chart_fixture(closes), 0, 1, label="Renk", now=0)["nm"] == "Renk"


def test_session_fraction_while_open():
    # 4 of 26 fifteen-minute bars into a 6.5h session.
    chart = chart_fixture([100.0, 101.0, 102.0, 103.0], start=0, end=26 * 900)
    assert x.quote_payload(chart, 0, 1, now=3600)["fr"] == 15


def test_quote_payload_fits_buffer():
    closes = [100.0 + (i % 7) for i in range(200)]
    p = x.quote_payload(chart_fixture(closes), 4, 5, label="A" * 40, now=0)
    assert len(p["h"]) == x.HIST_POINTS
    assert len(json.dumps(p, separators=(",", ":"))) < FIRMWARE_BUF


def test_flat_series_does_not_divide_by_zero():
    p = x.quote_payload(chart_fixture([100.0, 100.0], price=100.0, prev=100.0), 0, 1, now=0)
    assert p["h"] == [0, 0] and p["b"] == 0


# ---- Weather extras -----------------------------------------------------------

def test_weather_extras():
    f = forecast_fixture()
    f["current"].update(wind_speed_10m=12.6, uv_index=3.4)
    f["minutely_15"] = {"time": ["2026-10-12T18:45", "2026-10-12T19:00", "2026-10-12T19:15",
                                 "2026-10-12T19:30", "2026-10-12T19:45"],
                        "precipitation": [0.0, 0.0, 0.0, 0.4, 0.2]}
    p = x.weather_payload(f, "Ennetmoos", aqi=23.6)
    assert (p["ws"], p["uv"], p["aq"], p["rn"]) == (13, 3, 24, "Rain in 30 min")
    assert len(x.compact(p)) < FIRMWARE_BUF
    plain = x.weather_payload(forecast_fixture(), "Ennetmoos")   # older fields only
    assert not {"ws", "uv", "aq", "rn"} & plain.keys()


def test_rain_nowcast():
    times = ["2026-10-12T19:00", "2026-10-12T19:15", "2026-10-12T19:30", "2026-10-12T19:45"]
    m = lambda *mm: {"time": times, "precipitation": list(mm)}
    assert x.rain_nowcast("2026-10-12T19:00", m(0, 0, 0, 0)) == ""
    assert x.rain_nowcast("2026-10-12T19:00", m(0, 0.3, 0, 0)) == "Rain in 15 min"
    assert x.rain_nowcast("2026-10-12T19:00", m(0.5, 0.5, 0, 0)) == "Rain stops in 30 min"
    assert x.rain_nowcast("2026-10-12T19:00", m(1, 1, 1, 1)) == "Rain for the next hour"
    assert x.rain_nowcast("2026-10-12T19:00", None) == ""


# ---- Pace / history -----------------------------------------------------------

def usage(s, sr, w=20.0, wr=5000, acct="pro"):
    return {"s": s, "sr": sr, "w": w, "wr": wr, "acct": acct, "ok": True}


def test_session_series_ramps_and_interpolates():
    now = 1_000_000
    # 100 minutes into the window (sr = 200), one sample at minute 50.
    samples = [(now - 50 * 60, 10.0, 0.0)]
    vals, fr = x.session_series(samples, now, 30.0, 200)
    assert fr == 33 and len(vals) == 10                   # 10 of 30 slots
    assert vals[0] == 2 and vals[4] == 10                 # ramp 0 -> 10 over 50 min
    assert vals[-1] == 30                                 # ends on the live value


def test_pace_over_the_limit():
    now = 2_000_000
    samples = [(now - 30 * 60, 40.0, 10.0), (now - 15 * 60, 50.0, 11.0)]
    p = x.history_payload(samples, usage(60.0, 120), now)
    # 20 points in 30 min = 0.667 %/min; 60 + 0.667 * 120 = 140 at reset.
    assert (p["pr"], p["pz"]) == (140, 2)
    assert p["eta"] == x.clock_text(now + 60 * 60, 24)    # 40 points to go = 60 min
    assert len(x.compact(p)) < FIRMWARE_BUF


def test_pace_comfortable_and_window_average():
    now = 2_000_000
    # No recent samples: falls back to the window average (20% over 100 min).
    p = x.history_payload([], usage(20.0, 200), now)
    assert (p["pr"], p["pz"]) == (60, 0) and "eta" not in p
    assert x.history_payload([], usage(100.0, 30), now)["pz"] == 2
    assert "pr" not in x.history_payload([], usage(1.0, 295), now)   # 5 min in: too early


def test_week_days_counts_rises_only():
    now = int(datetime(2026, 10, 15, 12, 0).timestamp())
    day = 86400
    samples = [(now - 2 * day, 0, 10.0), (now - 2 * day + 600, 0, 14.0),   # +4 on Tue
               (now - day, 0, 90.0), (now - day + 600, 0, 2.0),            # reset on Wed
               (now - 600, 0, 5.0)]                                        # +3 today
    days = x.week_days(samples, now, 6.0)
    assert [d[0] for d in days][-1] == "Thu"
    assert days[-3:] == [["Tue", 4], ["Wed", 76], ["Thu", 4]]   # Wed: 14 -> 90 counts, the reset doesn't
    assert days[0][1] == -1                                     # no samples that day


def test_week_projection_and_enterprise():
    p = x.history_payload([], usage(5.0, 250, w=30.0, wr=x.WEEK_MIN // 2), 3_000_000)
    assert p["wp"] == 60
    assert x.history_payload([], usage(5.0, 250, acct="ent"), 3_000_000) is None


def test_history_file_roundtrip(tmp_path):
    path = str(tmp_path / "h.jsonl")
    now = 5_000_000
    x.history(json.dumps(usage(10.0, 250)), path=path, now=now - 3600 * 24 * 9)   # will be pruned
    x.history(json.dumps(usage(20.0, 200)), path=path, now=now - 600)
    p = x.history(json.dumps(usage(25.0, 190)), path=path, now=now)
    assert [s[1] for s in x.load_history(path, now)] == [20.0, 25.0]
    assert p["k"] == "h" and p["sh"][-1] == 25


# ---- Activity -----------------------------------------------------------------

def write_jsonl(path, records):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(json.dumps(r) + "\n" for r in records))


def assistant(ts, mid, model="claude-opus-5-5", tool=None, stop=None, **usage_):
    content = [{"type": "tool_use", "name": tool}] if tool else [{"type": "text", "text": "hi"}]
    u = {"input_tokens": 1000, "output_tokens": 500, "cache_read_input_tokens": 0,
         "cache_creation_input_tokens": 0, **usage_}
    return {"type": "assistant", "timestamp": ts, "sessionId": "s1", "requestId": "r" + mid,
            "cwd": "/home/me/Clawdmeter",
            "message": {"id": mid, "model": model, "usage": u, "content": content, "stop_reason": stop}}


def test_usage_cost():
    u = {"input_tokens": 1_000_000, "output_tokens": 1_000_000,
         "cache_creation_input_tokens": 1_000_000, "cache_read_input_tokens": 1_000_000}
    assert round(x.usage_cost("claude-opus-5-5", u), 2) == 4 + 20 + 5 + 0.2
    assert round(x.usage_cost("claude-sonnet-4-5-20250929", u), 2) == 3 + 15 + 3.75 + 0.3
    assert x.usage_cost("gpt-9", u) == 0.0


def test_activity_payload(tmp_path):
    now = time.time()
    ts = datetime.fromtimestamp(now).astimezone().isoformat()
    proj = tmp_path / "cfg" / "projects" / "-home-me-Clawdmeter"
    # Same message id twice (one line per content block) counts once.
    write_jsonl(proj / "s1.jsonl", [assistant(ts, "m1"), assistant(ts, "m1"),
                                    assistant(ts, "m2", tool="mcp__github__create_pull_request")])
    write_jsonl(proj / "s2.jsonl", [assistant(ts, "m3", stop="end_turn")])
    write_jsonl(proj / "s1" / "subagents" / "a.jsonl", [assistant(ts, "m4")])
    cache = str(tmp_path / "cache.json")
    p = x.activity_payload([str(tmp_path / "cfg")], now=now, alert_sid="s2", cache_path=cache)
    assert p["tk"] == 4 * 1500 and p["co"] == round(4 * (1000 * 4 + 500 * 20) / 1e6 * 100)
    assert p["ns"] == 1 and p["nl"] == 2
    assert p["ss"][0] == ["Clawdmeter", x.STATE_WAITING, "needs you", 0]      # alert sorts first
    assert p["ss"][1] == ["Clawdmeter", x.STATE_WORKING, "create_pull_", 0]
    # Second run: nothing new, totals unchanged (incremental offsets).
    again = x.activity_payload([str(tmp_path / "cfg")], now=now, cache_path=cache)
    assert again["tk"] == p["tk"]
    assert len(x.compact(p)) < FIRMWARE_BUF


def test_session_state():
    done = [{"type": "assistant", "message": {"content": [{"type": "text"}], "stop_reason": "end_turn"}}]
    prompt = [{"type": "user", "message": {"content": "do it"}}]
    assert x.session_state(done, 5) == (x.STATE_IDLE, "")
    assert x.session_state(prompt, 5) == (x.STATE_WORKING, "Thinking")
    assert x.session_state(prompt, 600) == (x.STATE_IDLE, "")     # stalled


# ---- Status / system ------------------------------------------------------------

def test_status_payload():
    ok = {"status": {"indicator": "none"}, "components": [{"name": "API", "status": "operational"}]}
    assert x.status_payload(ok) == {"k": "ss", "i": "none", "m": ""}
    inc = {"status": {"indicator": "minor"},
           "incidents": [{"name": "Elevated errors on Claude Opus 5.5", "status": "investigating"}]}
    assert x.status_payload(inc) == {"k": "ss", "i": "minor", "m": "Elevated errors on Claude Opus 5.5"}
    comp = {"status": {"indicator": "none"},
            "components": [{"name": "Claude Code", "status": "degraded_performance"}]}
    assert x.status_payload(comp) == {"k": "ss", "i": "minor", "m": "Claude Code degraded"}


def test_system_payload_shape():
    p = x.system_payload()
    assert p["k"] == "sys" and 0 <= p["cpu"] <= 100 and 0 <= p["mem"] <= 100
    assert p["mu"].endswith(" GB") and len(x.compact(p)) < FIRMWARE_BUF
    assert x.duration_text(59) == "0m" and x.duration_text(3 * 86400 + 4 * 3600) == "3d 4h"
