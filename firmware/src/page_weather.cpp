// Weather page (the app's {"k":"wx"} write): condition + temperature,
// feels-like / high / low, wind / UV / air quality, and three forecast
// tiles, laid out in the meters' card area. The status line carries rain in
// the next hour, or the next sunrise / sunset.
#include "pages.h"
#include "pages_internal.h"
#include "ui.h"
#include "weather.h"
#include "weather_icons.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_styrene_temp_96);
LV_FONT_DECLARE(font_styrene_temp_72);

static const uint32_t WX_STALE_MS = 3UL * 60 * 60 * 1000;   // the app refreshes every 15 min

static WeatherData wx = {};
static bool        received = false;
static uint32_t    received_ms = 0;

static lv_image_dsc_t big_dscs[WX_ICON_COUNT];
static lv_image_dsc_t small_dscs[WX_ICON_COUNT];
static bool           icons_ready = false;

static lv_obj_t* root = nullptr;
static lv_obj_t* title;
static lv_obj_t* icon;
static lv_obj_t* temp;
static lv_obj_t* cond;
static lv_obj_t* detail;
static lv_obj_t* extra;
static lv_obj_t* tiles[WX_FORECAST_DAYS];
static lv_obj_t* tile_day[WX_FORECAST_DAYS];
static lv_obj_t* tile_icon[WX_FORECAST_DAYS];
static lv_obj_t* tile_temp[WX_FORECAST_DAYS];
static lv_obj_t* footer;

static void init_icons(void) {
    if (icons_ready) return;
    icons_ready = true;
#define WX_ICON(idx, NAME, name)                                                           \
    page_init_icon(&big_dscs[idx], ICON_WX_##NAME##_120_W, ICON_WX_##NAME##_120_H,         \
                   icon_wx_##name##_120_data);                                             \
    page_init_icon(&small_dscs[idx], ICON_WX_##NAME##_48_W, ICON_WX_##NAME##_48_H,         \
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

// The art is white; every weather glyph is drawn in Clawd orange.
static void set_icon(lv_obj_t* img, const lv_image_dsc_t* dscs, int which) {
    lv_image_set_src(img, &dscs[which]);
    lv_obj_set_style_image_recolor(img, THEME_ACCENT, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
}

static lv_obj_t* make_icon(lv_obj_t* parent, const lv_image_dsc_t* dscs, int art_px, int px) {
    lv_obj_t* img = lv_image_create(parent);
    set_icon(img, dscs, WX_CLOUD);
    if (px != art_px) {
        lv_obj_set_size(img, px, px);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
    }
    return img;
}

static void apply(void) {
    if (!root || !received) return;
    char buf[64];
    page_set_title(title, wx.location[0] ? wx.location : "Weather");
    set_icon(icon, big_dscs, weather_icon_for(wx.code, wx.is_day));
    snprintf(buf, sizeof(buf), "%ld#d97757 \xC2\xB0#", lroundf(wx.temp));   // the ° takes the accent
    lv_label_set_text(temp, buf);
    lv_label_set_text(cond, weather_text_for(wx.code));
    snprintf(buf, sizeof(buf), "Feels %d\xC2\xB0  \xC2\xB7  H %d\xC2\xB0  L %d\xC2\xB0", wx.feels, wx.hi, wx.lo);
    lv_label_set_text(detail, buf);

    if (extra) {
        buf[0] = '\0';
        char part[24];
        auto add = [&](const char* p) {
            if (buf[0]) strncat(buf, "  \xC2\xB7  ", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, p, sizeof(buf) - strlen(buf) - 1);
        };
        if (wx.wind >= 0) { snprintf(part, sizeof(part), "Wind %d km/h", wx.wind); add(part); }
        if (wx.uv >= 0)   { snprintf(part, sizeof(part), "UV %d", wx.uv); add(part); }
        if (wx.aqi >= 0)  { snprintf(part, sizeof(part), "AQI %d", wx.aqi); add(part); }
        lv_label_set_text(extra, buf);
    }

    for (int i = 0; i < WX_FORECAST_DAYS; i++) {
        if (!tiles[i]) continue;
        if (i >= wx.n_days) {
            lv_obj_add_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const WeatherDay* d = &wx.days[i];
        lv_label_set_text(tile_day[i], d->day);
        set_icon(tile_icon[i], small_dscs, weather_icon_for(d->code, true));
        snprintf(buf, sizeof(buf), "#faf9f5 %d\xC2\xB0#  #b0aea5 %d\xC2\xB0#", d->hi, d->lo);
        lv_label_set_text(tile_temp[i], buf);
        lv_obj_clear_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
    }

    page_set_footer(footer, wx.rain[0] ? wx.rain : wx.sun);
}

// Sizes from the card area: the tallest temperature font that leaves room
// for the lines under it and the tiles; on short areas the extra line and
// then the tiles go first.
static void build(lv_obj_t* r) {
    root = r;
    init_icons();
    const Layout& L = ui_layout();
    const PageArea A = page_area(L);
    title = page_make_title(root, "Weather");

    const lv_font_t* temp_font = A.h >= 300 ? &font_styrene_temp_96
                               : A.h >= 220 ? &font_styrene_temp_72
                               : page_with_symbols(L.pct_font);
    const int temp_h = lv_font_get_line_height(temp_font);
    const int icon_px = A.h >= 220 ? LV_MIN(120, temp_h * 5 / 3) : LV_MIN(48, temp_h);
    const lv_font_t* cond_font = L.reset_font;
    const lv_font_t* detail_font = page_with_symbols(page_smaller(L.reset_font, 1));
    const lv_font_t* extra_font = page_with_symbols(page_smaller(L.reset_font, 2));
    const lv_font_t* tile_font = detail_font;
    const int gap = A.h >= 220 ? 8 : 4;
    const int row_h = LV_MAX(icon_px, temp_h);
    const int line = lv_font_get_line_height(cond_font);
    const int dl = lv_font_get_line_height(detail_font);
    const int el = lv_font_get_line_height(extra_font);
    const int tl = lv_font_get_line_height(tile_font);
    const int tile_pad = A.h >= 220 ? 6 : 4;
    const int tile_min = 2 * tl + 24 + 2 * tile_pad;

    int below = row_h + gap + line + 2 + dl;           // icon row, condition, detail
    bool with_extra = A.h - (below + 2 + el + gap) >= tile_min;
    if (with_extra) below += 2 + el;
    const int tile_h = A.h - below - gap;
    const bool with_tiles = tile_h >= tile_min;

    // Icon + big temperature, centred together as one row.
    lv_obj_t* row = lv_obj_create(root);
    lv_obj_set_size(row, LV_SIZE_CONTENT, row_h);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, gap * 2, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_align(row, LV_ALIGN_TOP_MID, A.x + A.w / 2 - L.w / 2, A.y);
    icon = make_icon(row, big_dscs, ICON_WX_SUN_120_W, icon_px);
    temp = page_label(row, temp_font, THEME_TEXT, "-");
    lv_label_set_recolor(temp, true);

    // One line each, as wide as the area, cut with "…" on narrow panels.
    auto line_label = [&](const lv_font_t* f, lv_color_t c) {
        lv_obj_t* l = page_label(root, f, c, "");
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_size(l, A.w, lv_font_get_line_height(f));
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        return l;
    };
    int y = A.y + row_h + gap;
    cond = line_label(cond_font, THEME_TEXT);
    lv_obj_align(cond, LV_ALIGN_TOP_MID, A.x + A.w / 2 - L.w / 2, y);
    y += line + 2;
    detail = line_label(detail_font, THEME_DIM);
    lv_obj_align(detail, LV_ALIGN_TOP_MID, A.x + A.w / 2 - L.w / 2, y);
    y += dl + 2;
    extra = nullptr;
    if (with_extra) {
        extra = line_label(extra_font, THEME_DIM);
        lv_obj_align(extra, LV_ALIGN_TOP_MID, A.x + A.w / 2 - L.w / 2, y);
        y += el;
    }

    for (int i = 0; i < WX_FORECAST_DAYS; i++) tiles[i] = nullptr;
    if (with_tiles) {
        const int tgap = gap;
        const int tw = (A.w - (WX_FORECAST_DAYS - 1) * tgap) / WX_FORECAST_DAYS;
        const int th = A.y + A.h - (y + gap);
        const int ticon = LV_MAX(24, LV_MIN(48, th - 2 * tl - 2 * tile_pad));
        for (int i = 0; i < WX_FORECAST_DAYS; i++) {
            lv_obj_t* t = page_panel(root, A.x + i * (tw + tgap), y + gap, tw, th);
            lv_obj_set_style_pad_ver(t, tile_pad, 0);
            tiles[i] = t;
            tile_day[i] = page_label(t, tile_font, THEME_DIM, "");
            lv_obj_align(tile_day[i], LV_ALIGN_TOP_MID, 0, 0);
            tile_icon[i] = make_icon(t, small_dscs, ICON_WX_SUN_48_W, ticon);
            lv_obj_align(tile_icon[i], LV_ALIGN_CENTER, 0, 0);
            tile_temp[i] = page_label(t, tile_font, THEME_TEXT, "");
            lv_label_set_recolor(tile_temp[i], true);
            lv_obj_align(tile_temp[i], LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
        }
    }

    footer = page_make_footer(root);
    apply();
}

static void destroy(void) { root = nullptr; }

static bool available(uint32_t now) { return received && now - received_ms < WX_STALE_MS; }

void pages_update_weather(const WeatherData* d) {
    wx = *d;
    received = true;
    received_ms = millis();
    apply();
}

const PageDef page_weather = {
    "weather", build, destroy, available, nullptr, nullptr, nullptr, nullptr,
};
