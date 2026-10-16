#pragma once
#include <stdint.h>

// The Weather page's data: the app's `{"k":"wx", ...}` write (keys in
// docs/protocol.md). Pure: no Arduino, no LVGL, so it runs in the native tests.

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
    char  rain[24];          // "Rain in 30 min"; empty = dry the next hour
};

// A write tagged "k":"wx" (cheap check, before any JSON parse). Usage
// payloads never carry "k".
bool weather_is_payload(const char* json);

// Parse a weather write. False on malformed JSON or a write that isn't
// weather; `out` is untouched then.
bool weather_parse(const char* json, WeatherData* out);

// WMO weather interpretation codes (Open-Meteo's `weather_code`) → icon and
// words.
enum WeatherIcon {
    WX_SUN, WX_MOON, WX_CLOUD_SUN, WX_CLOUD_MOON, WX_CLOUD, WX_FOG,
    WX_DRIZZLE, WX_RAIN, WX_SNOW, WX_THUNDER, WX_ICON_COUNT,
};
WeatherIcon weather_icon_for(int code, bool day);
const char* weather_text_for(int code);   // "" for an unknown code
