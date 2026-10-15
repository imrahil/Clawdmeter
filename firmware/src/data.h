#pragma once
#include <Arduino.h>

struct UsageData {
    float session_pct;       // utilization 0-100 (5h window Pro/Max; spending % Enterprise)
    int session_reset_mins;  // minutes until reset
    float weekly_pct;        // 7-day utilization (Pro/Max only; 0 for Enterprise)
    int weekly_reset_mins;   // minutes until weekly reset (Pro/Max only)
    char status[16];         // "allowed", "limited", etc.
    bool chime;              // play the session-reset chime; false unless daemon opts in
    bool beep;               // Claude needs attention: play the chime once for this packet
    char alert_msg[128];     // attention message for the status line; empty = no alert
    bool alert_clear;        // the session moved on: end the alert
    bool enterprise;         // true = Enterprise spending-limit account
    int time_pct;            // 0-100: fraction of billing period elapsed (Enterprise)
    int period_days;         // total billing period length in days (Enterprise)
    char reset_date[12];     // formatted reset date e.g. "Jul 1" (Enterprise)
    long clock_epoch;        // local wall-clock epoch (s) from daemon; 0 = not provided
    int  clock_fmt;          // 12 or 24 (hour format from daemon); defaults to 24
    bool ok;                 // data parse succeeded
    bool valid;              // false until first successful parse
};

// ---- Optional extra screens (daemon sends these only when configured) ----
// Both ride the usage RX characteristic, tagged with "k" ("wx" / "q"); an
// untagged write is always a usage payload. Field names mirror daemon/extras.py.

#define WX_FORECAST_DAYS 3

struct WeatherDay {
    char day[4];             // "Tue"
    int  code;               // WMO weather code
    int  hi, lo;             // °C
};

struct WeatherData {
    char  location[24];
    float temp;              // °C, current
    int   feels;             // °C, apparent
    int   code;              // WMO weather code, current
    bool  is_day;
    int   hi, lo;            // today's range, °C
    WeatherDay days[WX_FORECAST_DAYS];
    int   n_days;
    char  sun[16];           // next sun event, "Sunset 18:45"; empty = unknown
    int   wind;              // km/h; -1 = not sent
    int   uv;                // UV index; -1 = not sent
    int   aqi;               // European AQI; -1 = not sent
    char  rain[24];          // "Rain in 30 min"; empty = dry next hour
};

#define STOCK_MAX       5
#define STOCK_HIST_MAX  32

struct StockQuote {
    int     index;           // row 0..count-1
    int     count;           // rows the daemon is sending; rows >= count hide
    char    symbol[10];      // "R3NK" (exchange suffix stripped daemon-side)
    char    name[24];
    char    currency[4];     // "USD", "EUR"…
    float   price;
    float   prev_close;
    bool    open;            // regular session in progress
    uint8_t hist[STOCK_HIST_MAX];  // intraday closes scaled 0..99
    int     n_hist;
    uint8_t baseline;        // previous close on the same 0..99 scale
    uint8_t session_pct;     // share of the trading day hist covers, 0..100
};

// {"k":"h"} — Pace screen: the current 5-hour window and the last 7 days.
#define PACE_SESS_MAX 30
#define PACE_DAYS     7

struct PaceDay {
    char day[4];             // "Mon"
    int  pct;                // weekly % added that day; -1 = no samples
};

struct PaceData {
    uint8_t sess[PACE_SESS_MAX];   // session % at the end of each 10-min slot
    int     n_sess;                // slots filled so far (0 = no window data)
    int     frac;                  // % of the 5-hour window elapsed
    int     proj;                  // projected session % at reset; -1 = too early
    int     zone;                  // 0 comfortable, 1 tight, 2 limit before reset
    char    eta[10];               // "15:40" when the limit lands first; else ""
    PaceDay days[PACE_DAYS];
    int     n_days;
    int     week_proj;             // projected weekly % at its reset; -1 = unknown
};

// {"k":"a"} — Activity screen: live Claude Code sessions + today's totals.
#define ACT_ROWS_MAX 4

enum SessionState { SESSION_IDLE = 0, SESSION_WORKING = 1, SESSION_WAITING = 2 };

struct ActivityRow {
    char name[20];           // project (cwd basename)
    int  state;              // SessionState
    char label[14];          // tool name / "needs you"
    int  age_min;            // minutes since the transcript last moved
};

struct ActivityData {
    ActivityRow rows[ACT_ROWS_MAX];
    int   n_rows;
    int   live;              // sessions touched in the last hour (may exceed n_rows)
    double tokens;           // today, all four kinds (can pass 2^31)
    long  cost_cents;        // today, API-equivalent
    int   sessions;          // sessions with usage today
};

// {"k":"sys"} — System screen: the host the daemon runs on.
struct SystemData {
    char host[22];
    int  cpu;                // %
    char load[8];            // 1-minute load average, "1.23"
    int  mem;                // % used
    char mem_text[20];       // "9.8/16 GB"
    int  temp;               // °C; -1 = no sensor
    int  disk;               // % used
    char disk_text[20];
    char uptime[12];         // "3d 4h"
};

// {"k":"ss"} — Claude service status for the status line.
struct ServiceStatus {
    int  level;              // 0 none, 1 minor / maintenance, 2 major / critical
    char msg[50];
};
