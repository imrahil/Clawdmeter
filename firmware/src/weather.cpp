#include "weather.h"
#include <ArduinoJson.h>
#include <string.h>

static void copy(char* dst, size_t n, const char* src) {
    if (!src) src = "";
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

bool weather_is_payload(const char* json) {
    return json && strstr(json, "\"k\":\"wx\"") != nullptr;
}

bool weather_parse(const char* json, WeatherData* out) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    const char* kind = doc["k"] | "";
    if (strcmp(kind, "wx") != 0) return false;

    WeatherData w = {};
    copy(w.location, sizeof(w.location), doc["loc"] | "");
    w.temp   = doc["t"] | 0.0f;
    w.feels  = doc["fl"] | 0;
    w.code   = doc["c"] | 0;
    w.is_day = doc["d"].as<bool>();
    w.hi     = doc["hi"] | 0;
    w.lo     = doc["lo"] | 0;
    copy(w.sun, sizeof(w.sun), doc["sun"] | "");
    w.wind = doc["ws"] | -1;
    w.uv   = doc["uv"] | -1;
    w.aqi  = doc["aq"] | -1;
    copy(w.rain, sizeof(w.rain), doc["rn"] | "");
    for (JsonArray day : doc["f"].as<JsonArray>()) {
        if (w.n_days >= WX_FORECAST_DAYS) break;
        WeatherDay* d = &w.days[w.n_days++];
        copy(d->day, sizeof(d->day), day[0] | "");
        d->code = day[1] | 0;
        d->hi   = day[2] | 0;
        d->lo   = day[3] | 0;
    }
    *out = w;
    return true;
}

WeatherIcon weather_icon_for(int code, bool day) {
    if (code == 0)                                                 return day ? WX_SUN : WX_MOON;
    if (code <= 2)                                                 return day ? WX_CLOUD_SUN : WX_CLOUD_MOON;
    if (code == 3)                                                 return WX_CLOUD;
    if (code == 45 || code == 48)                                  return WX_FOG;
    if (code >= 51 && code <= 57)                                  return WX_DRIZZLE;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82))  return WX_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86)    return WX_SNOW;
    if (code >= 95)                                                return WX_THUNDER;
    return WX_CLOUD;
}

const char* weather_text_for(int code) {
    switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mostly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: case 48: return "Fog";
    case 51: return "Light drizzle";
    case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm, hail";
    default: return "";
    }
}
