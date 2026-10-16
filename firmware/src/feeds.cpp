#include "feeds.h"
#include <ArduinoJson.h>
#include <string.h>

static void copy(char* dst, size_t n, const char* src) {
    if (!src) src = "";
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

FeedKind feed_kind(const char* json) {
    if (!json) return FEED_NONE;
    const char* k = strstr(json, "\"k\":\"");
    if (!k) return FEED_NONE;
    k += 5;
    if (strncmp(k, "h\"", 2) == 0)   return FEED_PACE;
    if (strncmp(k, "a\"", 2) == 0)   return FEED_ACTIVITY;
    if (strncmp(k, "sys\"", 4) == 0) return FEED_SYSTEM;
    if (strncmp(k, "q\"", 2) == 0)   return FEED_STOCK;
    if (strncmp(k, "ss\"", 3) == 0)  return FEED_STATUS;
    return FEED_NONE;
}

// Parse and check the tag; false = leave `out` alone.
static bool load(JsonDocument& doc, const char* json, const char* kind) {
    if (!json || deserializeJson(doc, json)) return false;
    return strcmp(doc["k"] | "", kind) == 0;
}

bool feed_parse_pace(const char* json, PaceData* out) {
    JsonDocument doc;
    if (!load(doc, json, "h")) return false;
    PaceData p = {};
    for (JsonVariant v : doc["sh"].as<JsonArray>()) {
        if (p.n_sess >= PACE_SESS_MAX) break;
        p.sess[p.n_sess++] = (uint8_t)clamp(v | 0, 0, 100);
    }
    p.frac = clamp(doc["fr"] | 100, 0, 100);
    p.proj = doc["pr"] | -1;
    p.zone = clamp(doc["pz"] | 0, 0, 2);
    copy(p.eta, sizeof(p.eta), doc["eta"] | "");
    for (JsonArray day : doc["wd"].as<JsonArray>()) {
        if (p.n_days >= PACE_DAYS) break;
        PaceDay* d = &p.days[p.n_days++];
        copy(d->day, sizeof(d->day), day[0] | "");
        d->pct = day[1] | -1;
    }
    p.week_proj = doc["wp"] | -1;
    *out = p;
    return true;
}

bool feed_parse_activity(const char* json, ActivityData* out) {
    JsonDocument doc;
    if (!load(doc, json, "a")) return false;
    ActivityData a = {};
    for (JsonArray row : doc["ss"].as<JsonArray>()) {
        if (a.n_rows >= ACT_ROWS_MAX) break;
        ActivityRow* r = &a.rows[a.n_rows++];
        copy(r->name, sizeof(r->name), row[0] | "");
        r->state = clamp(row[1] | 0, SESSION_IDLE, SESSION_WAITING);
        copy(r->label, sizeof(r->label), row[2] | "");
        r->age_min = row[3] | 0;
    }
    a.live = doc["nl"] | a.n_rows;
    a.tokens = doc["tk"] | 0.0;
    a.cost_cents = doc["co"] | 0L;
    a.sessions = doc["ns"] | 0;
    *out = a;
    return true;
}

bool feed_parse_system(const char* json, SystemData* out) {
    JsonDocument doc;
    if (!load(doc, json, "sys")) return false;
    SystemData s = {};
    copy(s.host, sizeof(s.host), doc["hn"] | "");
    s.cpu = clamp(doc["cpu"] | 0, 0, 100);
    copy(s.load, sizeof(s.load), doc["ld"] | "");
    s.cores = doc["nc"] | 1;
    s.mem = clamp(doc["mem"] | 0, 0, 100);
    copy(s.mem_text, sizeof(s.mem_text), doc["mu"] | "");
    s.temp = doc["tmp"] | -1;
    s.disk = clamp(doc["dsk"] | 0, 0, 100);
    copy(s.disk_text, sizeof(s.disk_text), doc["du"] | "");
    copy(s.uptime, sizeof(s.uptime), doc["up"] | "");
    *out = s;
    return true;
}

bool feed_parse_stock(const char* json, StockQuote* out) {
    JsonDocument doc;
    if (!load(doc, json, "q")) return false;
    StockQuote q = {};
    q.index = doc["i"] | -1;
    if (q.index < 0 || q.index >= STOCK_MAX) return false;
    q.count = clamp(doc["n"] | 0, 0, STOCK_MAX);
    copy(q.symbol, sizeof(q.symbol), doc["sy"] | "");
    copy(q.name, sizeof(q.name), doc["nm"] | "");
    copy(q.currency, sizeof(q.currency), doc["cu"] | "");
    q.price      = doc["p"] | 0.0f;
    q.prev_close = doc["pc"] | 0.0f;
    q.open       = doc["o"].as<bool>();
    q.baseline   = (uint8_t)clamp(doc["b"] | 0, 0, 99);
    q.session_pct = (uint8_t)clamp(doc["fr"] | 100, 1, 100);   // absent (closed) = full width
    for (JsonVariant v : doc["h"].as<JsonArray>()) {
        if (q.n_hist >= STOCK_HIST_MAX) break;
        q.hist[q.n_hist++] = (uint8_t)clamp(v | 0, 0, 99);
    }
    *out = q;
    return true;
}

bool feed_parse_status(const char* json, ServiceStatus* out) {
    JsonDocument doc;
    if (!load(doc, json, "ss")) return false;
    ServiceStatus s = {};
    const char* ind = doc["i"] | "none";
    s.level = strcmp(ind, "major") == 0 || strcmp(ind, "critical") == 0 ? 2
            : strcmp(ind, "none") == 0 ? 0 : 1;
    copy(s.msg, sizeof(s.msg), doc["m"] | "");
    if (s.level == 0) s.msg[0] = '\0';
    *out = s;
    return true;
}
