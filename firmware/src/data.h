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
