# BLE protocol

Usage readings and hook events reach the app independently. The app selects
an account, builds a JSON payload, and writes it to the device over Bluetooth
LE. The firmware updates the meters and passes activity to Clawd's director.
Keyboard button reports use the separate HID service.

## Service and characteristics

The device advertises a custom GATT service next to the standard HID keyboard service:

|                                | UUID                                   |
| ------------------------------ | -------------------------------------- |
| **Data service**               | `4c41555a-4465-7669-6365-000000000001` |
| RX characteristic (write)      | `4c41555a-4465-7669-6365-000000000002` |
| TX characteristic (notify)     | `4c41555a-4465-7669-6365-000000000003` |
| REQ characteristic (notify)    | `4c41555a-4465-7669-6365-000000000004` |
| **HID service**                | `00001812-0000-1000-8000-00805f9b34fb` |

The host writes one complete JSON object to RX per write. The app limits
payloads to 500 bytes; the firmware has a 512-byte receive buffer, including
the terminating null byte. The device notifies `0x01` on REQ when it has no
data yet, asking for a poll.
It sends `{"dismiss":true}` on TX when a tap or the device's 2-minute timeout
ends an alert, so the app clears its corresponding alert too. The app expires
an alert nothing cleared after the same 2 minutes (a test alert after 30
seconds) and sends `x` if the device still shows it.

```json
{"s":45,"sr":120,"w":28,"wr":7200,"st":"allowed","acct":"pro","ok":true,"a":2,"e":"p","es":14,"n":1}
```

| Field | Meaning |
| --- | --- |
| `s`, `w` | Session and weekly % used. |
| `sr`, `wr` | Minutes until each resets; `-1` = window not started / unknown. |
| `st` | `allowed`, `limited` or `overage` (stored, not shown). |
| `acct` | `pro`, or `ent` for a spend-capped account: `s` is then the spend %, and `tp` (% of the billing period elapsed), `pd` (period length in days) and `rd` (reset date, e.g. `"Nov 1"`) fill the second card. |
| `t`, `tf` | Local wall-clock epoch and 12/24-hour format, when the clock is on. |
| `c` | `1` = the reset chime may sound. |
| `ok` | `false` alone = no usable account data; do not treat this as a zero reading. |
| `b`, `m`, `x` | Attention: alert chime, message (ASCII), end the alert. The app sends a fixed line per alert kind in `m`: `Question for you`, `Permission needed!` (also for other notification types), `Waiting for you!` (idle) or `MCP request!`; the test alert sends `Clawdmeter test alert`. The firmware shows whatever `m` it gets. |
| `a`, `e`, `es`, `n` | Fast clock: activity 0–3, latest hook event (`p` prompt, `s` stop, `b` back after a gap) and its sequence number, active sessions. |
| `rp` | `1` = a heartbeat replay, not a fresh poll; the rate math skips it. |
| `lv` | How lively Clawd is, `1` (No-nonsense) to `5` (All-nonsense); `3` is the default. Sent with every data payload and right away when the setting changes. The device stores the last value in NVS, so it applies from boot before the app connects; absent = keep the stored value. |
| `aa` | How long Clawd points and waves for an alert, in seconds: `120` (the default, the whole alert), `30`, `10`, or `0` = no animation. Only the animation: the alert's message, tap, `x`, timeout and dismiss notify are unchanged. Sent like `lv` and stored the same way (clamped to 0–120); absent = keep the stored value. |
| `pm` | Pace marker: `1` draws a line on the session and weekly bars at the elapsed share of each window (5 hours, 7 days) and colours the fill by pace (green at or behind it, amber ahead, red at 100%); `0` keeps the plain 50/80% colours. A bar with no reset time (`sr`/`wr` = -1) and spend-capped accounts keep the plain colours. Sent like `lv` and stored the same way; absent = keep the stored value. |

## Weather write

The app's other write: the device's Weather page, sent when the config names
a place (`weather_location`, optionally pinned with `weather_coords`). It is
tagged `"k":"wx"` (usage payloads never carry `k`), so the firmware routes it
to the page: it never counts as fresh usage, a rate sample or a heartbeat.
The app fetches it from Open-Meteo every 15 minutes, writes it in a tick
with no usage write, and re-sends the last one after a reconnect. Built by
`clawdmeter-core/src/weather.rs`, parsed by `firmware/src/weather.cpp`.

```json
{"k":"wx","loc":"Warsaw","t":12.8,"fl":11,"c":2,"d":1,"hi":15,"lo":7,
 "f":[["Tue",80,16,8],["Wed",61,19,13],["Thu",3,18,12]],
 "sun":"Sunset 18:45","ws":14,"uv":3,"aq":22,"rn":"Rain in 30 min"}
```

| Field | Meaning |
| --- | --- |
| `loc` | Place name (ASCII). |
| `t`, `fl` | Temperature (°C, one decimal) and feels-like (°C). |
| `c`, `d` | [WMO weather code](https://open-meteo.com/en/docs) now, and `1` in daytime (sun or moon icons). |
| `hi`, `lo` | Today's high and low, °C. |
| `f` | The next three days, `[day, code, high, low]`. |
| `sun` | The next sunrise or sunset, for the status line. |
| `ws`, `uv`, `aq` | Wind (km/h), UV index, European air-quality index; each absent when unknown. |
| `rn` | Rain in the next hour (`"Rain in 30 min"`, `"Rain stops in 15 min"`, `"Rain for the next hour"`); absent when it stays dry. The status line shows it instead of `sun`. |

The page drops out after 3 hours without a write. The Focus page needs
nothing from the app.
