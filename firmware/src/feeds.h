#pragma once
#include <stdint.h>

// The pages' data besides weather (weather.h): the app's tagged writes
// `{"k":"h"}` Pace, `{"k":"a"}` Activity, `{"k":"sys"}` System, `{"k":"q"}`
// one stock row and `{"k":"ss"}` Claude's service status (keys in
// docs/protocol.md). Pure: no Arduino, no LVGL, so it runs in the native
// tests. Each parser returns false on malformed JSON or another kind and
// leaves `out` untouched then.

enum FeedKind { FEED_NONE, FEED_PACE, FEED_ACTIVITY, FEED_SYSTEM, FEED_STOCK, FEED_STATUS };

// Which tagged write this is (cheap check, before any JSON parse). Usage
// payloads never carry "k"; weather is weather_is_payload()'s.
FeedKind feed_kind(const char* json);

// ─── Pace ({"k":"h"}): the current 5-hour window and the last 7 days ────────
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
bool feed_parse_pace(const char* json, PaceData* out);

// ─── Activity ({"k":"a"}): live Claude Code sessions + today's totals ────────
#define ACT_ROWS_MAX 4

enum SessionState { SESSION_IDLE = 0, SESSION_WORKING = 1, SESSION_WAITING = 2 };

struct ActivityRow {
    char name[20];           // project (cwd basename)
    int  state;              // SessionState
    char label[14];          // tool name / "Thinking" / "needs you"
    int  age_min;            // minutes since the transcript last moved
};

struct ActivityData {
    ActivityRow rows[ACT_ROWS_MAX];
    int    n_rows;
    int    live;             // sessions touched in the last hour (may exceed n_rows)
    double tokens;           // today, all four kinds (can pass 2^31)
    long   cost_cents;       // today, API-equivalent
    int    sessions;         // sessions with usage today
};
bool feed_parse_activity(const char* json, ActivityData* out);

// ─── System ({"k":"sys"}): the computer the app runs on ──────────────────────
struct SystemData {
    char host[22];
    int  cpu;                // %
    char load[8];            // 1-minute load average, "1.23"
    int  cores;
    int  mem;                // % used
    char mem_text[20];       // "9.8/16 GB"
    int  temp;               // °C; -1 = no sensor
    int  disk;               // % used
    char disk_text[20];
    char uptime[12];         // "3d 4h"
};
bool feed_parse_system(const char* json, SystemData* out);

// ─── Stocks ({"k":"q"}): one row per write ───────────────────────────────────
#define STOCK_MAX      5
#define STOCK_HIST_MAX 32

struct StockQuote {
    int     index;           // row 0..count-1
    int     count;           // rows the app is sending; rows >= count hide
    char    symbol[10];      // "R3NK" (exchange suffix stripped app-side)
    char    name[24];
    char    currency[4];     // "USD", "EUR", "GBp"…
    float   price;
    float   prev_close;
    bool    open;            // regular session in progress
    uint8_t hist[STOCK_HIST_MAX];  // intraday closes scaled 0..99
    int     n_hist;
    uint8_t baseline;        // previous close on the same 0..99 scale
    uint8_t session_pct;     // share of the trading day hist covers, 1..100
};
// False also for a row index outside 0..STOCK_MAX-1.
bool feed_parse_stock(const char* json, StockQuote* out);

// ─── Service status ({"k":"ss"}): status.claude.com, for the status line ─────
struct ServiceStatus {
    int  level;              // 0 all good, 1 minor / maintenance, 2 major / critical
    char msg[50];            // incident name; "" when all is well
};
bool feed_parse_status(const char* json, ServiceStatus* out);
