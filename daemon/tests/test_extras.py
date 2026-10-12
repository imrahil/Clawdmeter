#!/usr/bin/env python3
"""Weather / stock payload builders. Run: python -m pytest daemon/tests/test_extras.py -q"""
import json

from daemon import extras as x

FIRMWARE_BUF = 512  # BLE_BUF_SIZE in firmware/src/ble.cpp


def forecast_fixture():
    return {
        "current": {"temperature_2m": 11.34, "apparent_temperature": 9.8,
                    "weather_code": 2, "is_day": 1},
        "daily": {"time": ["2026-10-12", "2026-10-13", "2026-10-14", "2026-10-15"],
                  "weather_code": [3, 80, 61, 0],
                  "temperature_2m_max": [13.4, 15.3, 17.8, 17.6],
                  "temperature_2m_min": [6.7, 9.1, 12.1, -0.6]},
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
    assert len(x.compact(p)) < FIRMWARE_BUF


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
