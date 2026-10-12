#!/usr/bin/env python3
"""Weather and stock-quote payloads for the device's extra screens.

The Linux daemon shells out to this module (stdlib only, no venv needed):

    python3 extras.py weather "<location>" [lat,lon]   -> one JSON line
    python3 extras.py stocks "<spec>"                  -> one JSON line per symbol

Both payloads ride the same RX characteristic as the usage numbers, tagged with
"k" so the firmware can tell them apart (an untagged write is a usage payload).
The device fonts are ASCII plus a few symbols, and the daemon writes payloads
byte-per-char, so every string here is folded to ASCII.

Weather: Open-Meteo (no account, no key). Quotes: Yahoo Finance's chart
endpoint (unofficial; no key).
"""
import json
import sys
import time
import unicodedata
import urllib.parse
import urllib.request
from datetime import date

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


def weather_payload(forecast, label):
    """Build the {"k":"wx"} payload from an Open-Meteo forecast response."""
    cur = forecast["current"]
    daily = forecast["daily"]
    days = []
    for i in range(1, min(1 + FORECAST_DAYS, len(daily["time"]))):
        d = date.fromisoformat(daily["time"][i])
        days.append([DAY_NAMES[d.weekday()], int(daily["weather_code"][i]),
                     round(daily["temperature_2m_max"][i]), round(daily["temperature_2m_min"][i])])
    return {
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


def fetch_weather(location, coords=None):
    if coords:
        lat, lon = coords
        label = location
    else:
        lat, lon, label = geocode(location)
    url = ("https://api.open-meteo.com/v1/forecast?timezone=auto"
           f"&forecast_days={FORECAST_DAYS + 1}&latitude={lat}&longitude={lon}"
           "&current=temperature_2m,apparent_temperature,weather_code,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset")
    return weather_payload(get_json(url), label)


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
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Exception as e:
        print(f"extras: {e}", file=sys.stderr)
        sys.exit(1)
