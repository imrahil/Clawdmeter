// Weather screen (optional, daemon-fed {"k":"wx"}): condition + temperature,
// feels-like / high / low, wind / UV / air quality, and three forecast tiles.
#include "ui.h"
#include "ui_internal.h"
#include "weather_icons.h"
#include <math.h>
#include <string.h>

enum {
    WX_SUN, WX_MOON, WX_CLOUD_SUN, WX_CLOUD_MOON, WX_CLOUD, WX_FOG,
    WX_DRIZZLE, WX_RAIN, WX_SNOW, WX_THUNDER, WX_ICON_COUNT,
};
static lv_image_dsc_t wx_big_dscs[WX_ICON_COUNT];
static lv_image_dsc_t wx_small_dscs[WX_ICON_COUNT];
static bool           icons_ready = false;

static void init_weather_icons(void) {
    if (icons_ready) return;
    icons_ready = true;
#define WX_ICON(idx, NAME, name)                                                              \
    init_icon_dsc_rgb565a8(&wx_big_dscs[idx], ICON_WX_##NAME##_120_W, ICON_WX_##NAME##_120_H, \
                           icon_wx_##name##_120_data);                                        \
    init_icon_dsc_rgb565a8(&wx_small_dscs[idx], ICON_WX_##NAME##_48_W, ICON_WX_##NAME##_48_H, \
                           icon_wx_##name##_48_data)
    WX_ICON(WX_SUN,        SUN,             sun);
    WX_ICON(WX_MOON,       MOON,            moon);
    WX_ICON(WX_CLOUD_SUN,  CLOUD_SUN,       cloud_sun);
    WX_ICON(WX_CLOUD_MOON, CLOUD_MOON,      cloud_moon);
    WX_ICON(WX_CLOUD,      CLOUD,           cloud);
    WX_ICON(WX_FOG,        CLOUD_FOG,       cloud_fog);
    WX_ICON(WX_DRIZZLE,    CLOUD_DRIZZLE,   cloud_drizzle);
    WX_ICON(WX_RAIN,       CLOUD_RAIN,      cloud_rain);
    WX_ICON(WX_SNOW,       CLOUD_SNOW,      cloud_snow);
    WX_ICON(WX_THUNDER,    CLOUD_LIGHTNING, cloud_lightning);
#undef WX_ICON
}

// WMO weather interpretation codes (Open-Meteo's `weather_code`).
static int wx_icon_for(int code, bool day) {
    if (code == 0)              return day ? WX_SUN : WX_MOON;
    if (code <= 2)              return day ? WX_CLOUD_SUN : WX_CLOUD_MOON;
    if (code == 3)              return WX_CLOUD;
    if (code == 45 || code == 48) return WX_FOG;
    if (code >= 51 && code <= 57) return WX_DRIZZLE;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WX_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86)   return WX_SNOW;
    if (code >= 95)             return WX_THUNDER;
    return WX_CLOUD;
}

static const char* wx_text_for(int code) {
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

// The art is white; every weather glyph is drawn in the brand terra-cotta,
// the same ink as the Clawd mascot and the status line. Text stays ivory.
static void set_wx_icon(lv_obj_t* img, const lv_image_dsc_t* dscs, int icon) {
    lv_image_set_src(img, &dscs[icon]);
    lv_obj_set_style_image_recolor(img, COL_ACCENT, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
}

// Show an icon at `px`, scaling the art when the layout asks for another size.
static lv_obj_t* make_wx_icon(lv_obj_t* parent, const lv_image_dsc_t* dscs, int art_px, int px) {
    lv_obj_t* img = lv_image_create(parent);
    set_wx_icon(img, dscs, WX_CLOUD);
    if (px != art_px) {
        lv_obj_set_size(img, px, px);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
    }
    return img;
}

static lv_obj_t* weather_container = nullptr;
static lv_obj_t* wx_title;
static lv_obj_t* wx_icon;
static lv_obj_t* wx_temp;
static lv_obj_t* wx_cond;
static lv_obj_t* wx_detail;
static lv_obj_t* wx_extra;
static lv_obj_t* wx_tiles[WX_FORECAST_DAYS];
static lv_obj_t* wx_tile_day[WX_FORECAST_DAYS];
static lv_obj_t* wx_tile_icon[WX_FORECAST_DAYS];
static lv_obj_t* wx_tile_temp[WX_FORECAST_DAYS];
static WeatherData wx = {};
static bool      weather_received = false;
static uint32_t  weather_ms = 0;

static void weather_apply(void);

static void weather_build(lv_obj_t* root) {
    init_weather_icons();
    weather_container = root;
    wx_title = make_screen_title(weather_container, "Weather");

    // Icon + big temperature, centered together as one row.
    lv_obj_t* row = lv_obj_create(weather_container);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, L.panel_pad_x, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, L.wx_main_y);

    wx_icon = make_wx_icon(row, wx_big_dscs, ICON_WX_SUN_120_W, L.wx_icon_px);
    wx_temp = lv_label_create(row);
    lv_label_set_recolor(wx_temp, true);   // the degree sign takes the accent
    lv_label_set_text(wx_temp, "-");
    lv_obj_set_style_text_font(wx_temp, L.wx_temp_font, 0);
    lv_obj_set_style_text_color(wx_temp, COL_TEXT, 0);

    wx_cond = lv_label_create(weather_container);
    lv_label_set_text(wx_cond, "");
    lv_obj_set_style_text_font(wx_cond, L.wx_cond_font, 0);
    lv_obj_set_style_text_color(wx_cond, COL_TEXT, 0);
    lv_obj_align(wx_cond, LV_ALIGN_TOP_MID, 0, L.wx_cond_y);

    wx_detail = lv_label_create(weather_container);
    lv_label_set_text(wx_detail, "");
    lv_obj_set_style_text_font(wx_detail, L.wx_detail_font, 0);
    lv_obj_set_style_text_color(wx_detail, COL_DIM, 0);
    lv_obj_align(wx_detail, LV_ALIGN_TOP_MID, 0, L.wx_detail_y);

    wx_extra = make_label(weather_container, L.wx_extra_font, COL_DIM, "");
    lv_obj_align(wx_extra, LV_ALIGN_TOP_MID, 0, L.wx_extra_y);

    // Forecast: equal tiles across the content width.
    const int gap = L.usage_panel_gap;
    const int tile_w = (L.content_w - (WX_FORECAST_DAYS - 1) * gap) / WX_FORECAST_DAYS;
    for (int i = 0; i < WX_FORECAST_DAYS; i++) {
        lv_obj_t* tile = make_panel(weather_container, L.margin + i * (tile_w + gap),
                                    L.wx_fc_y, tile_w, L.wx_fc_h);
        lv_obj_set_style_pad_top(tile, L.wx_fc_pad_y, 0);
        lv_obj_set_style_pad_bottom(tile, L.wx_fc_pad_y, 0);
        wx_tiles[i] = tile;

        wx_tile_day[i] = lv_label_create(tile);
        lv_label_set_text(wx_tile_day[i], "");
        lv_obj_set_style_text_font(wx_tile_day[i], L.wx_fc_font, 0);
        lv_obj_set_style_text_color(wx_tile_day[i], COL_DIM, 0);
        lv_obj_align(wx_tile_day[i], LV_ALIGN_TOP_MID, 0, 0);

        wx_tile_icon[i] = make_wx_icon(tile, wx_small_dscs, ICON_WX_SUN_48_W, L.wx_fc_icon_px);
        lv_obj_align(wx_tile_icon[i], LV_ALIGN_CENTER, 0, 0);

        wx_tile_temp[i] = lv_label_create(tile);
        lv_label_set_text(wx_tile_temp[i], "");
        lv_label_set_recolor(wx_tile_temp[i], true);
        lv_obj_set_style_text_font(wx_tile_temp[i], L.wx_fc_font, 0);
        lv_obj_align(wx_tile_temp[i], LV_ALIGN_BOTTOM_MID, 0, 0);

        lv_obj_add_flag(tile, LV_OBJ_FLAG_HIDDEN);
    }

    weather_apply();
}

static void weather_apply(void) {
    if (!weather_container || !weather_received) return;
    const WeatherData* d = &wx;
    char buf[64];
    set_screen_title(wx_title, d->location[0] ? d->location : "Weather");
    set_wx_icon(wx_icon, wx_big_dscs, wx_icon_for(d->code, d->is_day));
    snprintf(buf, sizeof(buf), "%ld#d97757 \xC2\xB0#", lroundf(d->temp));
    lv_label_set_text(wx_temp, buf);
    lv_label_set_text(wx_cond, wx_text_for(d->code));
    snprintf(buf, sizeof(buf), "Feels %d\xC2\xB0  \xC2\xB7  H %d\xC2\xB0  L %d\xC2\xB0",
             d->feels, d->hi, d->lo);
    lv_label_set_text(wx_detail, buf);

    // Wind / UV / air quality, whichever the daemon sent.
    buf[0] = '\0';
    auto add = [&](const char* part) {
        if (buf[0]) strlcat(buf, "  \xC2\xB7  ", sizeof(buf));
        strlcat(buf, part, sizeof(buf));
    };
    char part[24];
    if (d->wind >= 0) { snprintf(part, sizeof(part), "Wind %d km/h", d->wind); add(part); }
    if (d->uv >= 0)   { snprintf(part, sizeof(part), "UV %d", d->uv); add(part); }
    if (d->aqi >= 0)  { snprintf(part, sizeof(part), "AQI %d", d->aqi); add(part); }
    lv_label_set_text(wx_extra, buf);

    for (int i = 0; i < WX_FORECAST_DAYS; i++) {
        if (i >= d->n_days) {
            lv_obj_add_flag(wx_tiles[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const WeatherDay* day = &d->days[i];
        lv_label_set_text(wx_tile_day[i], day->day);
        set_wx_icon(wx_tile_icon[i], wx_small_dscs, wx_icon_for(day->code, true));
        snprintf(buf, sizeof(buf), "#faf9f5 %d\xC2\xB0#  #b0aea5 %d\xC2\xB0#", day->hi, day->lo);
        lv_label_set_text(wx_tile_temp[i], buf);
        lv_obj_clear_flag(wx_tiles[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_update_weather(const WeatherData* d) {
    wx = *d;
    weather_received = true;
    weather_ms = lv_tick_get();
    weather_apply();
}

static void weather_destroy(void) {
    weather_container = nullptr;
}

static bool weather_available(uint32_t now) {
    return weather_received && now - weather_ms < EXTRA_STALE_MS;
}

// Rain in the next hour beats the next sun event.
static const char* weather_status(uint32_t now) {
    (void)now;
    return wx.rain[0] ? wx.rain : wx.sun;
}

const ScreenDef weather_screen = {
    weather_build, weather_destroy, weather_available, nullptr,
    weather_status, nullptr, nullptr, nullptr,
};

