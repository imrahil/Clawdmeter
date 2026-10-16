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

## Page writes

The other device pages work the same way: each is its own write tagged with
`k`, built in `clawdmeter-core` (one module per page) and sent by
`clawdmeter-app/src/feeds.rs`. Each refreshes off the daemon's loop while a
device is connected, waits for a tick with no usage write, goes out one write
per tick round-robin across pages, and is re-sent after a reconnect. Every
string is folded to ASCII and every write stays within 500 bytes.

| Page | `k` | Config | Refresh |
| --- | --- | --- | --- |
| Pace | `h` | `pace_page` (on) | After each fresh usage write of a subscription account |
| Activity | `a` | `activity_page` (on) | Every 30 s, and when an alert goes up or ends |
| Service status | `ss` | `service_status` (on) | Every 5 min (2 after a failure) |
| System | `sys` | `system_stats` (off; Linux only) | Every 30 s |
| Stocks | `q` | `stocks` (unset = off) | Every 5 min (2 when every symbol failed) |

### Pace write

The session's usage across its 5-hour window and where it is headed, plus
the week. Sent after a usage write with `ok`, no `rp` and `acct` `pro` (or
absent), from that write's `s`/`sr`/`w`/`wr` and the app's own history
(`<state>/usage-history.jsonl`: one `[epoch, s, w]` line per poll, 30 s
apart at least, pruned to 8 days). `pace.rs`.

```json
{"k":"h","sh":[2,4,6,9,12,15,18,21,24,27,30,34],"fr":40,"pr":131,"pz":2,"eta":"14:35",
 "wd":[["Wed",-1],["Thu",6],["Fri",9],["Sat",0],["Sun",2],["Mon",11],["Tue",8]],"wp":72}
```

| Field | Meaning |
| --- | --- |
| `sh` | Session % at the end of each 10-minute slot of the window so far (at most 30); ramps from 0 at the window start, interpolated between polls. Absent when the window hasn't started (`sr` -1). |
| `fr` | % of the 5-hour window elapsed. |
| `pr`, `pz` | Projected session % at the reset (at most 999) at the pace of the last 30 minutes (once 10 of them are in the window; else the window's average), and its zone: `0` under 80, `1` under 100, `2` at or over. `100`/`2` once at 100 %. Absent while the pace can't be told (under 10 minutes in). |
| `eta` | Local time the session reaches 100 % at this pace, `"14:35"` or `"2:35 PM"` (the `clock` setting, else the locale). Only when it will before the reset. |
| `wd` | The last 7 local days, today last: `[day, % of the week added that day]`, summing rises between polls (a weekly reset isn't negative); `-1` for a day with no polls. |
| `wp` | Projected weekly % at the reset (at most 999), once 5 % of the week has gone. |

### Activity write

Claude Code's live sessions and today's totals, read from every account's
transcripts (`<config dir>/projects/*/*.jsonl` for sessions,
`projects/**/*.jsonl` for the totals, subagents included). A token cache in
`<state>/activity-cache.json` keeps the totals incremental. `activity.rs`.

```json
{"k":"a","ss":[["Clawdmeter",2,"needs you",0],["api",1,"Bash",1],["docs",0,"",12]],
 "nl":3,"tk":1843211,"co":412,"ns":5}
```

| Field | Meaning |
| --- | --- |
| `ss` | Up to 4 sessions written to within the hour: `[project, state, label, minutes since its last write]`. State `2` = its alert is up (label `needs you`), `1` = Claude has the turn (label: the tool running, ≤ 12, or `Thinking`), `0` = idle. Waiting first, then working, then idle; most recent first within each. Project: the transcript's last `cwd`, base name, ≤ 18. |
| `nl` | Sessions written to within the hour (all of them). |
| `tk` | Tokens today (input, output, cache writes and reads), local day. |
| `co` | API-equivalent cost today, in cents (the `PRICES` table in `activity.rs`). |
| `ns` | Sessions with a reply today. |

### Service status write

Claude's status from the Statuspage summary behind status.claude.com
(`/api/v2/summary.json`). `service_status.rs`.

```json
{"k":"ss","i":"minor","m":"Elevated errors on Claude Opus"}
```

| Field | Meaning |
| --- | --- |
| `i` | `none`, `minor`, `major`, `critical` or `maintenance`. A component that isn't operational lifts `none` to `minor` (or `maintenance`). |
| `m` | The first unresolved incident's name, else `"<component> degraded"` / `partial outage` / `outage` / `maintenance`; empty when `i` is `none`. At most 48 characters. |

### System write

This computer's stats, from `/proc` and `/sys` (Linux; the page is off
elsewhere). `system.rs`.

```json
{"k":"sys","hn":"workstation","cpu":12,"ld":"0.42","nc":8,"mem":38,"mu":"6.1/16 GB",
 "tmp":54,"dsk":25,"du":"112/456 GB","up":"3d 4h"}
```

| Field | Meaning |
| --- | --- |
| `hn` | Host name up to the first dot, ≤ 20. |
| `cpu` | CPU busy % over half a second. |
| `ld`, `nc` | 1-minute load average (text) and CPU count. |
| `mem`, `mu` | Memory used % (total − available) and `"used/total GB"`. |
| `tmp` | CPU package temperature, °C (hwmon, else thermal zones); `-1` = unknown. |
| `dsk`, `du` | Root filesystem used % and `"used/total GB"`. |
| `up` | Uptime: `"42m"`, `"5h 12m"`, `"3d 4h"`. |

### Stock write

One write per symbol of `stocks` (up to 5), from Yahoo Finance's chart
endpoint (`range=1d&interval=15m`; unofficial, no key). A symbol that fails
keeps its last write. `stocks.rs`.

```json
{"k":"q","i":0,"n":2,"sy":"R3NK","nm":"Renk","cu":"EUR","p":61.23,"pc":60.0,"o":1,
 "h":[33,40,52,61,58,70],"b":33,"fr":18}
```

| Field | Meaning |
| --- | --- |
| `i`, `n` | This symbol's place (from 0) and how many there are. |
| `sy` | Symbol without its exchange suffix or index caret, ≤ 8. |
| `nm` | The configured name, else Yahoo's minus legal suffixes, ≤ 20. |
| `cu` | Currency as Yahoo writes it (`GBp` is pence), ≤ 3. |
| `p`, `pc` | Price now and the previous close, 2 decimals. |
| `o` | `1` while the regular session is open. |
| `h` | Today's 15-minute closes, at most 32 points, scaled 0–99 over a range that includes the previous close. |
| `b` | The previous close on the same scale (the baseline). |
| `fr` | % of the trading day `h` covers while open (1–100); `100` when closed. |
