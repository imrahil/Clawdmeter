#!/usr/bin/env python3
"""Payloads for the device's extra screens.

The Linux daemon shells out to this module (stdlib only, no venv needed):

    python3 extras.py weather "<location>" [lat,lon]   -> one JSON line
    python3 extras.py stocks "<spec>"                  -> one JSON line per symbol
    python3 extras.py history '<usage payload>' [12|24] -> one JSON line (Pace screen)
    python3 extras.py activity "<dirs>" [alert sid]     -> one JSON line (Activity screen)
    python3 extras.py status                           -> one JSON line (status line)
    python3 extras.py system                           -> one JSON line (System screen)

Both payloads ride the same RX characteristic as the usage numbers, tagged with
"k" so the firmware can tell them apart (an untagged write is a usage payload).
The device fonts are ASCII plus a few symbols, and the daemon writes payloads
byte-per-char, so every string here is folded to ASCII.

Weather: Open-Meteo (no account, no key). Quotes: Yahoo Finance's chart
endpoint (unofficial; no key). Service status: the Statuspage JSON behind
status.claude.com. History, activity and system stats are local: the daemon's
own usage samples, Claude Code's transcripts, and /proc.
"""
import glob
import json
import math
import os
import socket
import sys
import time
import unicodedata
import urllib.parse
import urllib.request
from datetime import date, datetime, timedelta

UA = "Mozilla/5.0 (X11; Linux x86_64) Clawdmeter"
TIMEOUT = 15
FORECAST_DAYS = 3      # tiles under today's reading
HIST_POINTS = 32       # sparkline resolution; matches STOCK_HIST_MAX in firmware
BAR_SECONDS = 900      # 15-minute bars from the chart endpoint
MAX_STOCKS = 5         # matches STOCK_MAX in firmware
DAY_NAMES = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]

# Suffix words dropped from Yahoo's company names so they fit the row.
NAME_NOISE = {"inc", "inc.", "corp", "corp.", "corporation", "ag", "se", "plc",
              "n.v.", "nv", "s.a.", "sa", "ltd", "ltd.", "co", "co.", "holdings",
              "i", "a", "class"}


def to_ascii(text, limit):
    """Fold accents and drop anything non-ASCII, collapse whitespace, cap length."""
    folded = unicodedata.normalize("NFKD", text or "").encode("ascii", "ignore").decode()
    folded = " ".join(folded.split())
    return folded[:limit]


def get_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
        return json.load(resp)


def compact(obj):
    return json.dumps(obj, separators=(",", ":"))


# ---- Weather ----------------------------------------------------------------

def geocode(name):
    """Resolve a place name to (lat, lon, display name) via Open-Meteo."""
    query = name.split(",")[0].strip()
    url = ("https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name="
           + urllib.parse.quote(query))
    hits = get_json(url).get("results") or []
    if not hits:
        raise LookupError(f"no geocoding result for {name!r}")
    return hits[0]["latitude"], hits[0]["longitude"], hits[0]["name"]


def next_sun_event(now_local, sunrises, sunsets):
    """'Sunset 18:45' / 'Sunrise 07:41' — whichever comes next. Inputs are the
    local ISO timestamps Open-Meteo returns with timezone=auto."""
    events = sorted([(t, "Sunrise") for t in sunrises] + [(t, "Sunset") for t in sunsets])
    for t, name in events:
        if t > now_local:
            return f"{name} {t[11:16]}"
    return ""


RAIN_MM = 0.1          # a 15-minute slot with at least this much counts as rain
NOWCAST_SLOTS = 4      # look one hour ahead (15-minute slots)


def rain_nowcast(now_local, minutely):
    """'Rain in 30 min' / 'Rain stops in 15 min' / 'Rain for the next hour', or
    '' when the next hour is dry. `minutely` is Open-Meteo's minutely_15 block;
    times are local ISO strings like the current time."""
    times = (minutely or {}).get("time") or []
    rain = (minutely or {}).get("precipitation") or []
    now = datetime.fromisoformat(now_local)
    slots = []
    for t, mm in zip(times, rain):
        start = datetime.fromisoformat(t)
        if start + timedelta(minutes=15) <= now:
            continue                      # slot already over
        slots.append((max(0, round((start - now).total_seconds() / 60)), (mm or 0) >= RAIN_MM))
        if len(slots) >= NOWCAST_SLOTS:
            break
    if not slots:
        return ""
    if slots[0][1]:
        for mins, wet in slots[1:]:
            if not wet:
                return f"Rain stops in {mins} min"
        return "Rain for the next hour"
    for mins, wet in slots:
        if wet:
            return f"Rain in {mins} min"
    return ""


def weather_payload(forecast, label, aqi=None):
    """Build the {"k":"wx"} payload from an Open-Meteo forecast response.
    `aqi` is the European AQI from the air-quality endpoint (None = unknown)."""
    cur = forecast["current"]
    daily = forecast["daily"]
    days = []
    for i in range(1, min(1 + FORECAST_DAYS, len(daily["time"]))):
        d = date.fromisoformat(daily["time"][i])
        days.append([DAY_NAMES[d.weekday()], int(daily["weather_code"][i]),
                     round(daily["temperature_2m_max"][i]), round(daily["temperature_2m_min"][i])])
    out = {
        "k": "wx",
        "loc": to_ascii(label, 20),
        "t": round(cur["temperature_2m"], 1),
        "fl": round(cur["apparent_temperature"]),
        "c": int(cur["weather_code"]),
        "d": int(cur["is_day"]),
        "hi": round(daily["temperature_2m_max"][0]),
        "lo": round(daily["temperature_2m_min"][0]),
        "f": days,
        "sun": next_sun_event(cur["time"], daily.get("sunrise", []), daily.get("sunset", [])),
    }
    if cur.get("wind_speed_10m") is not None:
        out["ws"] = round(cur["wind_speed_10m"])          # km/h (Open-Meteo default)
    if cur.get("uv_index") is not None:
        out["uv"] = round(cur["uv_index"])
    if aqi is not None:
        out["aq"] = round(aqi)
    rain = rain_nowcast(cur["time"], forecast.get("minutely_15"))
    if rain:
        out["rn"] = rain
    return out


def fetch_weather(location, coords=None):
    if coords:
        lat, lon = coords
        label = location
    else:
        lat, lon, label = geocode(location)
    url = ("https://api.open-meteo.com/v1/forecast?timezone=auto"
           f"&forecast_days={FORECAST_DAYS + 1}&latitude={lat}&longitude={lon}"
           "&current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m,uv_index"
           "&minutely_15=precipitation&forecast_minutely_15=8"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset")
    forecast = get_json(url)
    try:  # separate endpoint; weather still goes out without it
        air = get_json("https://air-quality-api.open-meteo.com/v1/air-quality"
                       f"?latitude={lat}&longitude={lon}&current=european_aqi")
        aqi = (air.get("current") or {}).get("european_aqi")
    except Exception as e:
        print(f"air quality failed: {e}", file=sys.stderr)
        aqi = None
    return weather_payload(forecast, label, aqi)


# ---- Stocks -----------------------------------------------------------------

def parse_stock_spec(spec):
    """'AMD, NVDA=NVIDIA, R3NK.DE=Renk' -> [('AMD', None), ('NVDA', 'NVIDIA'), ...]."""
    out = []
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        sym, _, label = item.partition("=")
        out.append((sym.strip().upper(), label.strip() or None))
    return out[:MAX_STOCKS]


def clean_name(name, limit=20):
    """Yahoo's long company name, minus legal suffixes, cut at a word boundary."""
    words = to_ascii(name, 60).split(",")[0].split()
    while words and words[-1].lower() in NAME_NOISE:
        words.pop()
    out = ""
    for w in words:
        if len(out) + len(w) + (1 if out else 0) > limit:
            break
        out = f"{out} {w}" if out else w
    return out or " ".join(words)[:limit]


def display_symbol(sym):
    """Exchange suffixes (R3NK.DE) are noise on the device; indices keep their caret off."""
    return sym.split(".")[0].lstrip("^")[:8]


def resample(values, n):
    if len(values) <= n:
        return values
    step = (len(values) - 1) / (n - 1)
    return [values[round(i * step)] for i in range(n)]


def quote_payload(chart, index, count, label=None, now=None):
    """Build one {"k":"q"} payload from a Yahoo chart response."""
    now = time.time() if now is None else now
    res = chart["chart"]["result"][0]
    meta = res["meta"]
    price = meta["regularMarketPrice"]
    prev = meta.get("chartPreviousClose") or meta.get("previousClose") or price
    closes = [c for c in res["indicators"]["quote"][0].get("close") or [] if c is not None]
    regular = (meta.get("currentTradingPeriod") or {}).get("regular") or {}
    is_open = bool(regular) and regular.get("start", 0) <= now < regular.get("end", 0)
    # Mid-session the sparkline covers only the elapsed share of the day.
    session = 100
    if is_open:
        bars = max(1, (regular["end"] - regular["start"]) // BAR_SECONDS)
        session = max(1, min(100, round(len(closes) / bars * 100)))
    closes = resample(closes, HIST_POINTS)
    # Normalise the intraday closes to 0..99 over a range that includes the
    # previous close, so the dim baseline ("b") lands on the same scale.
    lo = min(closes + [prev])
    hi = max(closes + [prev])
    span = (hi - lo) or 1.0
    scale = lambda v: round((v - lo) / span * 99)
    return {
        "k": "q",
        "i": index,
        "n": count,
        "sy": display_symbol(meta.get("symbol", "")),
        "nm": to_ascii(label, 20) if label else clean_name(meta.get("shortName") or meta.get("longName") or ""),
        "cu": to_ascii(meta.get("currency", ""), 3),   # keep case: "GBp" is pence
        "p": round(price, 2),
        "pc": round(prev, 2),
        "o": 1 if is_open else 0,
        "h": [scale(c) for c in closes],
        "b": scale(prev),
        "fr": session,
    }


def fetch_quote(symbol, index, count, label=None):
    url = ("https://query1.finance.yahoo.com/v8/finance/chart/"
           + urllib.parse.quote(symbol) + "?range=1d&interval=15m")   # keep in step with BAR_SECONDS
    return quote_payload(get_json(url), index, count, label)


# ---- Pace / history ---------------------------------------------------------
# The API only reports the current utilisation, so the daemon keeps its own
# record: one [epoch, session %, weekly %] line per poll, pruned to 8 days.

HISTORY_FILE = os.path.expanduser("~/.config/claude-usage-monitor/usage-history.jsonl")
HISTORY_KEEP = 8 * 86400
SESSION_MIN = 300               # 5-hour window
WEEK_MIN = 7 * 1440
SESS_POINTS = 30                # sparkline slots across the 5-hour window (10 min each)
RATE_SPAN = 30 * 60             # "at this pace" = the last 30 minutes...
RATE_MIN_SPAN = 10 * 60         # ...once there are at least 10 of them


def load_history(path, now):
    out = []
    try:
        with open(path) as f:
            for line in f:
                try:
                    t, s, w = json.loads(line)[:3]
                except (ValueError, TypeError):
                    continue
                if now - t <= HISTORY_KEEP:
                    out.append((t, float(s), float(w)))
    except FileNotFoundError:
        pass
    return out


def record_sample(path, samples, sample, now):
    """Append one sample (skipping one landing within 30 s of the last) and
    rewrite the file only when old lines need pruning. Returns the samples."""
    if samples and sample[0] - samples[-1][0] < 30:
        return samples
    samples = samples + [sample]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    line = json.dumps([sample[0], sample[1], sample[2]]) + "\n"
    try:
        with open(path) as f:
            first = f.readline()
        stale = bool(first) and now - json.loads(first)[0] > HISTORY_KEEP
    except (FileNotFoundError, ValueError, TypeError, IndexError):
        stale = False
    if stale:
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            f.writelines(json.dumps(list(s)) + "\n" for s in samples)
        os.replace(tmp, path)
    else:
        with open(path, "a") as f:
            f.write(line)
    return samples


def session_series(samples, now, s_now, sr):
    """Session % across the current 5-hour window: SESS_POINTS slots, filled up
    to now. Returns (values, % of the window elapsed). Before the first sample
    the line ramps up from 0 at the window start (a window starts empty); gaps
    between samples are interpolated."""
    elapsed = max(0, min(SESSION_MIN, SESSION_MIN - sr)) * 60
    start = now - elapsed
    slot = SESSION_MIN * 60 / SESS_POINTS
    filled = max(1, min(SESS_POINTS, math.ceil(elapsed / slot)))
    pts = [(start, 0.0)] + [(t, s) for t, s, _ in samples if start <= t < now] + [(now, s_now)]
    values = []
    for i in range(filled):
        at = min(now, start + (i + 1) * slot)          # end of slot i
        j = 0
        while j + 1 < len(pts) and pts[j + 1][0] <= at:
            j += 1
        if j + 1 < len(pts) and pts[j + 1][0] > pts[j][0]:
            (t0, v0), (t1, v1) = pts[j], pts[j + 1]
            v = v0 + (v1 - v0) * (at - t0) / (t1 - t0)
        else:
            v = pts[j][1]
        values.append(max(0, min(100, round(v))))
    return values, round(elapsed / (SESSION_MIN * 60) * 100)


def session_rate(samples, now, s_now, sr):
    """%/min. The last 30 minutes when there are at least 10 of them in this
    window, else the window's average so far. None when it can't be told yet."""
    start = now - max(0, SESSION_MIN - sr) * 60
    recent = [(t, s) for t, s, _ in samples if start <= t < now and now - t <= RATE_SPAN]
    if recent and now - recent[0][0] >= RATE_MIN_SPAN:
        return max(0.0, (s_now - recent[0][1]) / ((now - recent[0][0]) / 60))
    elapsed_min = (now - start) / 60
    if elapsed_min >= 10:
        return max(0.0, s_now / elapsed_min)
    return None


def clock_text(epoch, tf):
    lt = time.localtime(epoch)
    if tf == 12:
        return f"{(lt.tm_hour % 12) or 12}:{lt.tm_min:02d} {'AM' if lt.tm_hour < 12 else 'PM'}"
    return f"{lt.tm_hour:02d}:{lt.tm_min:02d}"


def week_days(samples, now, w_now):
    """Weekly % added on each of the last 7 local days (today last): the sum of
    the rises between consecutive samples, so a weekly reset doesn't count.
    -1 for a day with no samples at all (daemon wasn't running)."""
    today = date.fromtimestamp(now)
    days = [today - timedelta(days=6 - i) for i in range(7)]
    added = {d: 0.0 for d in days}
    seen = set()
    prev = None
    for t, _, w in list(samples) + [(now, 0.0, w_now)]:
        d = date.fromtimestamp(t)
        if d in added:
            seen.add(d)
            if prev is not None and w > prev:
                added[d] += w - prev
        prev = w
    return [[DAY_NAMES[d.weekday()], round(added[d]) if d in seen else -1] for d in days]


def history_payload(samples, usage, now, tf=24):
    """Build the {"k":"h"} payload. `usage` is the device usage payload just
    sent (s / sr / w / wr). None for enterprise accounts (no 5h/7d windows)."""
    if usage.get("acct", "pro") != "pro":
        return None
    s_now, sr = float(usage.get("s", 0)), int(usage.get("sr", -1))
    w_now, wr = float(usage.get("w", 0)), int(usage.get("wr", -1))
    out = {"k": "h"}
    if sr >= 0:
        out["sh"], out["fr"] = session_series(samples, now, s_now, sr)
        rate = session_rate(samples, now, s_now, sr)
        if s_now >= 100:
            out["pr"], out["pz"] = 100, 2
        elif rate is not None:
            proj = s_now + rate * sr
            out["pr"] = min(999, round(proj))
            out["pz"] = 0 if proj < 80 else 1 if proj < 100 else 2
            if proj >= 100 and rate > 0:
                out["eta"] = clock_text(now + (100 - s_now) / rate * 60, tf)
    out["wd"] = week_days(samples, now, w_now)
    if wr >= 0:
        frac = 1 - wr / WEEK_MIN
        if frac >= 0.05:
            out["wp"] = min(999, round(w_now / frac))
    return out


def history(usage_json, tf=24, path=HISTORY_FILE, now=None):
    now = int(time.time()) if now is None else now
    usage = json.loads(usage_json)
    if usage.get("acct", "pro") != "pro" or not usage.get("ok", True):
        return None
    samples = load_history(path, now)
    samples = record_sample(path, samples, (now, float(usage.get("s", 0)), float(usage.get("w", 0))), now)
    return history_payload(samples[:-1] if samples and samples[-1][0] == now else samples, usage, now, tf)


# ---- Claude Code activity ---------------------------------------------------
# Read straight from Claude Code's transcripts (<config dir>/projects/*/*.jsonl):
# which sessions are live and what they're doing, plus today's token totals.
# A token cache in ~/.cache keeps the daily totals incremental.

ACTIVITY_CACHE = os.path.expanduser("~/.cache/clawdmeter/activity.json")
ACTIVE_WINDOW = 60 * 60          # sessions touched within the hour are listed
WORKING_FRESH = 90               # an unfinished turn idle this long has stalled
MAX_SESSIONS = 4
TAIL_BYTES = 256 * 1024

# $ per million tokens: (model id prefix, input, output, cache read). Cache
# writes bill at 1.25x input. First match wins, so specific ids come first.
PRICES = [
    ("claude-fable-5-1", 10, 50, 0.25), ("claude-mythos-5-1", 10, 50, 0.25),
    ("claude-fable-5", 10, 50, 1.0), ("claude-mythos-5", 10, 50, 1.0),
    ("claude-opus-5-5", 4, 20, 0.20), ("claude-opus-5", 5, 25, 0.50),
    ("claude-opus-4-8", 5, 25, 0.50), ("claude-opus-4-7", 5, 25, 0.50),
    ("claude-opus-4-6", 5, 25, 0.50), ("claude-opus-4-5", 5, 25, 0.50),
    ("claude-opus-4", 15, 75, 1.50),
    ("claude-sonnet-5", 2, 10, 0.20), ("claude-sonnet-4", 3, 15, 0.30),
    ("claude-haiku-5-5", 0.10, 0.50, 0.01), ("claude-haiku-4-5", 1, 5, 0.10),
]

STATE_IDLE, STATE_WORKING, STATE_WAITING = 0, 1, 2


def usage_cost(model, u):
    """API-equivalent $ for one assistant message's usage block (0 if unknown model)."""
    for prefix, inp, outp, cread in PRICES:
        if (model or "").startswith(prefix):
            return ((u.get("input_tokens") or 0) * inp
                    + (u.get("cache_creation_input_tokens") or 0) * inp * 1.25
                    + (u.get("cache_read_input_tokens") or 0) * cread
                    + (u.get("output_tokens") or 0) * outp) / 1e6
    return 0.0


def usage_tokens(u):
    return sum(u.get(k) or 0 for k in ("input_tokens", "output_tokens",
                                       "cache_creation_input_tokens", "cache_read_input_tokens"))


def local_day(ts):
    try:
        return datetime.fromisoformat(ts.replace("Z", "+00:00")).astimezone().date().isoformat()
    except (ValueError, AttributeError):
        return None


def tally_tokens(files, today, cache):
    """Fold new transcript lines into today's totals. `cache` holds per-file
    byte offsets and the message ids already counted (Claude Code writes one
    line per content block, each carrying the same usage)."""
    if cache.get("day") != today:
        cache.clear()
        cache.update(day=today, files={}, seen=[], tk=0, co=0.0, ss=[])
    seen, sessions = set(cache["seen"]), set(cache["ss"])
    for path in files:
        try:
            size = os.path.getsize(path)
        except OSError:
            continue
        off = cache["files"].get(path, 0)
        if off > size:
            off = 0                      # rewritten / truncated
        if off == size:
            continue
        with open(path, "rb") as f:
            f.seek(off)
            chunk = f.read()
        end = chunk.rfind(b"\n")
        if end < 0:
            continue
        cache["files"][path] = off + end + 1
        for raw in chunk[:end].splitlines():
            try:
                rec = json.loads(raw)
            except ValueError:
                continue
            if rec.get("type") != "assistant" or local_day(rec.get("timestamp")) != today:
                continue
            msg = rec.get("message") or {}
            u = msg.get("usage")
            if not u or msg.get("model") == "<synthetic>":
                continue
            key = f"{msg.get('id')}:{rec.get('requestId')}"
            if key in seen:
                continue
            seen.add(key)
            cache["tk"] += usage_tokens(u)
            cache["co"] += usage_cost(msg.get("model"), u)
            if rec.get("sessionId"):
                sessions.add(rec["sessionId"])
    cache["seen"], cache["ss"] = sorted(seen), sorted(sessions)
    return cache


def tail_records(path, nbytes=TAIL_BYTES):
    with open(path, "rb") as f:
        f.seek(0, os.SEEK_END)
        size = f.tell()
        f.seek(max(0, size - nbytes))
        data = f.read()
    out = []
    for raw in data.splitlines():
        try:
            out.append(json.loads(raw))
        except ValueError:
            pass                         # first line may be cut in half
    return out


def tool_label(name):
    """'mcp__github__create_pr' -> 'create_pr'; cut to fit the row."""
    return to_ascii((name or "").split("__")[-1], 12)


def session_state(records, age):
    """(state, label) from a transcript's last user/assistant record."""
    last = None
    for rec in reversed(records):
        if rec.get("type") in ("user", "assistant") and not rec.get("isMeta"):
            last = rec
            break
    if last is None:
        return STATE_IDLE, ""
    msg = last.get("message") or {}
    if last["type"] == "assistant":
        content = msg.get("content") or []
        tools = [c for c in content if isinstance(c, dict) and c.get("type") == "tool_use"]
        if tools:
            return (STATE_WORKING, tool_label(tools[-1].get("name"))) if age < 15 * 60 else (STATE_IDLE, "")
        if msg.get("stop_reason") in ("end_turn", "stop_sequence", "max_tokens", "refusal"):
            return STATE_IDLE, ""
    # A prompt or tool result, or a reply still streaming: Claude has the turn.
    return (STATE_WORKING, "Thinking") if age < WORKING_FRESH else (STATE_IDLE, "")


def project_name(records, path):
    for rec in reversed(records):
        if rec.get("cwd"):
            return to_ascii(os.path.basename(rec["cwd"].rstrip("/")), 18) or "~"
    return to_ascii(os.path.basename(os.path.dirname(path)), 18)


def list_sessions(top_files, now, alert_sid=""):
    rows = []
    for path in top_files:
        try:
            age = max(0.0, now - os.path.getmtime(path))
        except OSError:
            continue
        if age > ACTIVE_WINDOW:
            continue
        records = tail_records(path)
        sid = os.path.splitext(os.path.basename(path))[0]
        state, label = session_state(records, age)
        if alert_sid and sid == alert_sid:
            state, label = STATE_WAITING, "needs you"
        rows.append((state, age, [project_name(records, path), state, label, int(age // 60)]))
    # Waiting first, then working, then idle; most recent first within each.
    rows.sort(key=lambda r: (-r[0], r[1]))
    return [r[2] for r in rows[:MAX_SESSIONS]], len(rows)


def activity_payload(config_dirs, now=None, alert_sid="", cache_path=ACTIVITY_CACHE):
    now = time.time() if now is None else now
    top, every = [], []
    for d in config_dirs:
        root = os.path.join(os.path.expanduser(d), "projects")
        top += glob.glob(os.path.join(root, "*", "*.jsonl"))
        every += glob.glob(os.path.join(root, "**", "*.jsonl"), recursive=True)
    today = date.fromtimestamp(now).isoformat()
    midnight = datetime.combine(date.fromtimestamp(now), datetime.min.time()).timestamp()
    todays = [p for p in every if os.path.getmtime(p) >= midnight]
    try:
        with open(cache_path) as f:
            cache = json.load(f)
    except (FileNotFoundError, ValueError):
        cache = {}
    cache = tally_tokens(todays, today, cache)
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    tmp = cache_path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(cache, f)
    os.replace(tmp, cache_path)
    rows, live = list_sessions(top, now, alert_sid)
    return {"k": "a", "ss": rows, "nl": live, "tk": cache["tk"],
            "co": round(cache["co"] * 100), "ns": len(cache["ss"])}


# ---- Claude service status --------------------------------------------------

STATUS_URL = "https://status.claude.com/api/v2/summary.json"
COMPONENT_WORDS = {"degraded_performance": "degraded", "partial_outage": "partial outage",
                   "major_outage": "outage", "under_maintenance": "maintenance"}


def status_payload(summary):
    """{"k":"ss","i":<indicator>,"m":<short text>} — i is none/minor/major/
    critical/maintenance; m is empty when all is well."""
    ind = ((summary.get("status") or {}).get("indicator")) or "none"
    msg = ""
    for inc in summary.get("incidents") or []:
        if inc.get("status") not in ("resolved", "postmortem"):
            msg = inc.get("name") or ""
            break
    if not msg:
        for c in summary.get("components") or []:
            word = COMPONENT_WORDS.get(c.get("status"))
            if word and not c.get("group"):
                msg = f"{c.get('name', '')} {word}"
                if ind == "none":
                    ind = "maintenance" if word == "maintenance" else "minor"
                break
    if ind == "none":
        msg = ""
    return {"k": "ss", "i": ind, "m": to_ascii(msg, 48)}


# ---- Host system stats ------------------------------------------------------

def cpu_percent(interval=0.5):
    def snap():
        with open("/proc/stat") as f:
            vals = [int(v) for v in f.readline().split()[1:]]
        idle = vals[3] + (vals[4] if len(vals) > 4 else 0)
        return sum(vals), idle
    t0, i0 = snap()
    time.sleep(interval)
    t1, i1 = snap()
    return round(100 * (1 - (i1 - i0) / ((t1 - t0) or 1)))


def mem_info():
    info = {}
    with open("/proc/meminfo") as f:
        for line in f:
            k, v = line.split(":", 1)
            info[k] = int(v.split()[0]) * 1024
    total, avail = info["MemTotal"], info.get("MemAvailable", info.get("MemFree", 0))
    return total - avail, total


# hwmon chip names whose first input is the CPU package / die temperature.
CPU_HWMON = ("coretemp", "k10temp", "zenpower", "cpu_thermal", "soc_thermal")


def cpu_temp():
    """°C of the CPU package, or None. hwmon first, then thermal zones."""
    for hw in sorted(glob.glob("/sys/class/hwmon/hwmon*")):
        try:
            with open(os.path.join(hw, "name")) as f:
                name = f.read().strip()
        except OSError:
            continue
        if name not in CPU_HWMON:
            continue
        labels = sorted(glob.glob(os.path.join(hw, "temp*_label")))
        pick = None
        for lab in labels:            # prefer "Package id 0" / "Tctl" / "Tdie"
            with open(lab) as f:
                if f.read().strip() in ("Package id 0", "Tctl", "Tdie"):
                    pick = lab.replace("_label", "_input")
                    break
        pick = pick or os.path.join(hw, "temp1_input")
        try:
            with open(pick) as f:
                return round(int(f.read()) / 1000)
        except (OSError, ValueError):
            continue
    best = None
    for z in glob.glob("/sys/class/thermal/thermal_zone*"):
        try:
            with open(os.path.join(z, "type")) as f:
                kind = f.read().strip()
            with open(os.path.join(z, "temp")) as f:
                t = int(f.read()) / 1000
        except (OSError, ValueError):
            continue
        if kind == "x86_pkg_temp":
            return round(t)
        if 0 < t < 125:
            best = t if best is None else max(best, t)
    return round(best) if best is not None else None


def human_gb(n):
    g = n / 1024 ** 3
    return f"{g:.1f}" if g < 10 else f"{g:.0f}"


def duration_text(secs):
    m = int(secs // 60)
    if m < 60:
        return f"{m}m"
    if m < 1440:
        return f"{m // 60}h {m % 60}m"
    return f"{m // 1440}d {(m % 1440) // 60}h"


def system_payload(path="/"):
    used, total = mem_info()
    st = os.statvfs(path)
    d_total = st.f_blocks * st.f_frsize
    d_used = d_total - st.f_bavail * st.f_frsize
    with open("/proc/uptime") as f:
        up = float(f.read().split()[0])
    t = cpu_temp()
    return {
        "k": "sys",
        "hn": to_ascii(socket.gethostname().split(".")[0], 20),
        "cpu": cpu_percent(),
        "ld": f"{os.getloadavg()[0]:.2f}",
        "nc": os.cpu_count() or 1,
        "mem": round(used / total * 100),
        "mu": f"{human_gb(used)}/{human_gb(total)} GB",
        "tmp": t if t is not None else -1,
        "dsk": round(d_used / d_total * 100) if d_total else 0,
        "du": f"{human_gb(d_used)}/{human_gb(d_total)} GB",
        "up": duration_text(up),
    }


def main(argv):
    if len(argv) >= 2 and argv[0] == "weather":
        coords = None
        if len(argv) >= 3 and argv[2].strip():
            lat, lon = (float(v) for v in argv[2].split(","))
            coords = (lat, lon)
        print(compact(fetch_weather(argv[1], coords)))
        return 0
    if len(argv) >= 2 and argv[0] == "stocks":
        stocks = parse_stock_spec(argv[1])
        failed = 0
        for i, (sym, label) in enumerate(stocks):
            try:
                print(compact(fetch_quote(sym, i, len(stocks), label)), flush=True)
            except Exception as e:  # one bad symbol shouldn't blank the others
                print(f"quote {sym} failed: {e}", file=sys.stderr)
                failed += 1
        return 1 if failed == len(stocks) else 0
    if len(argv) >= 2 and argv[0] == "history":
        tf = 12 if len(argv) >= 3 and argv[2] == "12" else 24
        p = history(argv[1], tf)
        if p:
            print(compact(p))
        return 0
    if len(argv) >= 2 and argv[0] == "activity":
        dirs = [d.strip() for d in argv[1].split(",") if d.strip()]
        print(compact(activity_payload(dirs, alert_sid=argv[2] if len(argv) >= 3 else "")))
        return 0
    if argv[:1] == ["status"]:
        print(compact(status_payload(get_json(STATUS_URL))))
        return 0
    if argv[:1] == ["system"]:
        print(compact(system_payload()))
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Exception as e:
        print(f"extras: {e}", file=sys.stderr)
        sys.exit(1)
