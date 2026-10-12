#include "ui.h"
#include "splash.h"
#include <lvgl.h>
#include <time.h>
#include "logo.h"
#include "clawd_still.h"
#include "icons.h"
#include "weather_icons.h"
#include "hal/board_caps.h"
#include <math.h>

// Custom fonts (scaled for 314 PPI, ~1.9x from original 165 PPI)
LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_styrene_12);
LV_FONT_DECLARE(font_mono_32);
LV_FONT_DECLARE(font_mono_18);
// Weather temperature: digits, '-' and '°' only.
LV_FONT_DECLARE(font_styrene_temp_96);
LV_FONT_DECLARE(font_styrene_temp_72);
// Symbols-only fallbacks (£ ¥ ° · € −) for the ASCII-only Styrene sizes.
LV_FONT_DECLARE(font_sym_12);
LV_FONT_DECLARE(font_sym_14);
LV_FONT_DECLARE(font_sym_16);
LV_FONT_DECLARE(font_sym_20);
LV_FONT_DECLARE(font_sym_24);
LV_FONT_DECLARE(font_sym_28);
LV_FONT_DECLARE(font_sym_48);

// The weather and stock screens print ° € −, which the ASCII-only text fonts
// lack. They use copies of those fonts with a same-size symbols font as the
// LVGL fallback. Built once in compute_layout().
static lv_font_t sym_font_pool[12];
static int       sym_font_n = 0;
static const lv_font_t* with_symbols(const lv_font_t* base, const lv_font_t* sym) {
    for (int i = 0; i < sym_font_n; i++) {
        if (sym_font_pool[i].dsc == base->dsc && sym_font_pool[i].fallback == sym) return &sym_font_pool[i];
    }
    if (sym_font_n >= (int)(sizeof(sym_font_pool) / sizeof(sym_font_pool[0]))) return base;
    lv_font_t* f = &sym_font_pool[sym_font_n++];
    *f = *base;
    f->fallback = sym;
    return f;
}

// Layout values computed from the active board's geometry. Populated once
// in ui_init() and treated as const for the rest of the program. Adding a
// new display size means extending compute_layout() with another
// breakpoint — never editing the screen-builder functions below.
struct Layout {
    int16_t scr_w, scr_h;
    int16_t margin;
    int16_t title_y;
    int16_t content_y;
    int16_t content_w;

    // Usage screen
    int16_t usage_panel_h;
    int16_t usage_panel_gap;
    int16_t usage_bar_y;
    int16_t usage_reset_y;
    int16_t bar_h;
    int16_t panel_pad_x, panel_pad_y;
    int16_t pill_pad_x, pill_pad_y;
    const lv_font_t* title_font;     // screen title / clock
    const lv_font_t* pct_font;       // big percentage number
    const lv_font_t* ent_pct_font;   // enterprise spending number
    const lv_font_t* pill_font;      // "Current" / "Weekly" pill
    const lv_font_t* reset_font;     // "Resets in ..." line
    const lv_font_t* pace_font;      // enterprise "Under/On/Over pace" line
    const lv_font_t* anim_font;      // animated status line
    int16_t anim_y;                  // status line offset from bottom
    bool    small_icons;             // 40px logo + 24px battery (vs 80/48) on small screens
    int16_t title_nudge;             // title x-shift balancing the corner logo
    int16_t logo_y;                  // logo top edge
    int16_t batt_y;                  // battery icon top edge
    int16_t batt_w;                  // battery icon width, for position math

    // Pairing hint / idle screen
    int16_t pair_y1, pair_y2, pair_y3;
    int16_t idle_px;                 // sleeping-creature size on the idle screen

    // Bluetooth screen
    int16_t bt_info_panel_h;
    int16_t bt_reset_zone_h;
    const lv_font_t* bt_title_font;
    const lv_font_t* bt_status_font;
    const lv_font_t* bt_device_font;
    const lv_font_t* bt_credit_1_font;
    const lv_font_t* bt_credit_2_font;

    // Weather screen
    int16_t wx_icon_px;              // main condition icon (drawn from the 120px art)
    int16_t wx_main_y;               // top of the icon + temperature row
    int16_t wx_cond_y;               // condition word ("Partly cloudy")
    int16_t wx_detail_y;             // feels-like / high / low line
    int16_t wx_fc_y, wx_fc_h;        // forecast tiles
    int16_t wx_fc_icon_px;           // forecast icon (drawn from the 48px art)
    const lv_font_t* wx_temp_font;
    const lv_font_t* wx_cond_font;
    const lv_font_t* wx_detail_font;
    const lv_font_t* wx_fc_font;

    // Stocks screen
    int16_t st_row_h, st_row_gap;    // row height shrinks when 5 rows must fit
    int16_t st_pad_y;
    int16_t st_left_w;               // symbol / name column
    int16_t st_spark_w;              // intraday sparkline
    int16_t st_line_w;               // sparkline stroke
    const lv_font_t* st_sym_font;
    const lv_font_t* st_name_font;
    const lv_font_t* st_price_font;
    const lv_font_t* st_chg_font;
};
static Layout L = {};

// Pick layout values from the active board's pixel dimensions. The two
// existing boards happen to land on the two breakpoints below; new ports
// inherit the closer one — visually OK, may need a polish pass for
// pixel-perfect alignment but never blocks the port from booting.
static void compute_layout(const BoardCaps& c) {
    L.scr_w = c.width;
    L.scr_h = c.height;
    L.margin = 20;
    L.title_y = 30;

    // Values shared by the two original breakpoints; the small branch below
    // overrides them wholesale.
    L.bar_h = 24;
    L.panel_pad_x = 16;
    L.panel_pad_y = 12;
    L.pill_pad_x = 18;
    L.pill_pad_y = 6;
    L.title_font   = &font_tiempos_56;
    L.pct_font     = &font_styrene_48;
    L.ent_pct_font = &font_tiempos_56;
    L.pill_font    = &font_styrene_28;
    L.reset_font   = &font_styrene_28;
    L.pace_font    = &font_styrene_16;
    L.anim_font    = &font_mono_32;
    L.anim_y = -15;
    L.small_icons = false;
    L.title_nudge = 16;
    L.logo_y = L.title_y - 10;
    L.batt_y = L.title_y;
    L.batt_w = ICON_BATTERY_W;
    L.pair_y1 = 40;
    L.pair_y2 = 120;
    L.pair_y3 = 160;
    L.idle_px = 160;

    if (c.height >= 460) {
        // Large layout — tuned for 480x480 (AMOLED-2.16).
        L.content_y = 100;
        L.usage_panel_h = 150;
        L.usage_panel_gap = 16;
        L.usage_bar_y = 56;
        L.usage_reset_y = 94;
        L.bt_info_panel_h = 160;
        L.bt_reset_zone_h = 110;
        L.bt_title_font    = &font_tiempos_56;
        L.bt_status_font   = &font_styrene_48;
        L.bt_device_font   = &font_styrene_28;
        L.bt_credit_1_font = &font_styrene_24;
        L.bt_credit_2_font = &font_styrene_20;
        L.wx_icon_px = 120;
        L.wx_main_y = 96;
        L.wx_cond_y = 226;
        L.wx_detail_y = 264;
        L.wx_fc_y = 312;
        L.wx_fc_h = 130;
        L.wx_fc_icon_px = 48;
        L.wx_temp_font   = &font_styrene_temp_96;
        L.wx_cond_font   = &font_styrene_28;
        L.wx_detail_font = with_symbols(&font_styrene_24, &font_sym_24);
        L.wx_fc_font     = with_symbols(&font_styrene_24, &font_sym_24);
        L.st_row_h = 82;
        L.st_row_gap = 10;
        L.st_pad_y = 10;
        L.st_left_w = 140;
        L.st_spark_w = 120;
        L.st_line_w = 3;
        L.st_sym_font   = &font_styrene_28;
        L.st_name_font  = &font_styrene_16;
        L.st_price_font = with_symbols(&font_styrene_28, &font_sym_28);
        L.st_chg_font   = with_symbols(&font_styrene_20, &font_sym_20);
    } else if (c.height >= 300) {
        // Compact layout — tuned for 368x448 (AMOLED-1.8).
        L.content_y = 85;
        L.usage_panel_h = 130;
        L.usage_panel_gap = 12;
        L.usage_bar_y = 48;
        L.usage_reset_y = 78;
        L.bt_info_panel_h = 140;
        L.bt_reset_zone_h = 90;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_28;
        L.bt_device_font   = &font_styrene_20;
        L.bt_credit_1_font = &font_styrene_16;
        L.bt_credit_2_font = &font_styrene_14;
        L.wx_icon_px = 96;
        L.wx_main_y = 82;
        L.wx_cond_y = 188;
        L.wx_detail_y = 220;
        L.wx_fc_y = 262;
        L.wx_fc_h = 118;
        L.wx_fc_icon_px = 40;
        L.wx_temp_font   = &font_styrene_temp_72;
        L.wx_cond_font   = &font_styrene_24;
        L.wx_detail_font = with_symbols(&font_styrene_20, &font_sym_20);
        L.wx_fc_font     = with_symbols(&font_styrene_20, &font_sym_20);
        L.st_row_h = 74;
        L.st_row_gap = 8;
        L.st_pad_y = 8;
        L.st_left_w = 100;
        L.st_spark_w = 84;
        L.st_line_w = 2;
        L.st_sym_font   = &font_styrene_24;
        L.st_name_font  = &font_styrene_14;
        L.st_price_font = with_symbols(&font_styrene_24, &font_sym_24);
        L.st_chg_font   = with_symbols(&font_styrene_16, &font_sym_16);
    } else {
        // Small layout — tuned for 240x240 (LCD-1.54 and similar square TFTs).
        // Everything shrinks: fonts two steps down, panels ~half height, and
        // the corner logo/battery switch to the 40px/24px small assets.
        L.margin = 8;
        L.title_y = 4;
        L.content_y = 44;
        L.usage_panel_h = 74;
        L.usage_panel_gap = 6;
        L.usage_bar_y = 30;
        L.usage_reset_y = 46;
        L.bar_h = 12;
        L.panel_pad_x = 10;
        L.panel_pad_y = 6;
        L.pill_pad_x = 8;
        L.pill_pad_y = 2;
        L.title_font   = &font_tiempos_34;
        L.pct_font     = &font_styrene_24;
        L.ent_pct_font = &font_tiempos_34;
        L.pill_font    = &font_styrene_14;
        L.reset_font   = &font_styrene_14;
        L.pace_font    = &font_styrene_12;
        L.anim_font    = &font_mono_18;
        // Center the status line in the strip below the weekly panel; flush
        // against the bottom edge it reads as unevenly spaced.
        L.anim_y = -10;
        L.small_icons = true;
        L.title_nudge = 8;
        L.logo_y = 2;
        L.batt_y = 10;
        L.batt_w = ICON_BATTERY_SMALL_W;
        L.pair_y1 = 12;
        L.pair_y2 = 56;
        L.pair_y3 = 80;
        L.idle_px = 96;
        L.bt_info_panel_h = 90;
        L.bt_reset_zone_h = 60;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_20;
        L.bt_device_font   = &font_styrene_14;
        L.bt_credit_1_font = &font_styrene_12;
        L.bt_credit_2_font = &font_styrene_12;
        L.wx_icon_px = 56;
        L.wx_main_y = 40;
        L.wx_cond_y = 100;
        L.wx_detail_y = 118;
        L.wx_fc_y = 140;
        L.wx_fc_h = 92;
        L.wx_fc_icon_px = 28;
        L.wx_temp_font   = with_symbols(&font_styrene_48, &font_sym_48);
        L.wx_cond_font   = &font_styrene_14;
        L.wx_detail_font = with_symbols(&font_styrene_12, &font_sym_12);
        L.wx_fc_font     = with_symbols(&font_styrene_12, &font_sym_12);
        L.st_row_h = 44;
        L.st_row_gap = 4;
        L.st_pad_y = 4;
        L.st_left_w = 70;
        L.st_spark_w = 50;
        L.st_line_w = 2;
        L.st_sym_font   = &font_styrene_14;
        L.st_name_font  = &font_styrene_12;
        L.st_price_font = with_symbols(&font_styrene_14, &font_sym_14);
        L.st_chg_font   = with_symbols(&font_styrene_12, &font_sym_12);
    }

    L.content_w = L.scr_w - 2 * L.margin;
}

// Anthropic brand palette — design tokens live in theme.h
#include "theme.h"
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG

// ---- Usage screen widgets (single non-splash view) ----
static lv_obj_t* usage_container;
static lv_obj_t* lbl_title;
// Clock fed by the daemon: base epoch (local wall-clock seconds) + the lv_tick at
// which it landed, so the title ticks forward locally between 60s payloads.
static long     clock_base_epoch = 0;
static uint32_t clock_base_ms = 0;
static int      clock_fmt = 24;   // 12 or 24, set from the daemon payload
static int      clock_last_min = -1;   // last rendered minute; avoids redrawing the title every tick
static lv_obj_t* usage_group;   // the two usage panels — shown when connected
static lv_obj_t* pair_group;    // pairing hint — shown when disconnected
static lv_obj_t* bar_session;
static lv_obj_t* lbl_session_pct;
static lv_obj_t* lbl_session_label;
static lv_obj_t* lbl_session_reset;
static lv_obj_t* bar_weekly;
static lv_obj_t* lbl_weekly_pct;
static lv_obj_t* lbl_weekly_label;
static lv_obj_t* lbl_weekly_reset;
static lv_obj_t* panel_session = nullptr;
static lv_obj_t* panel_weekly = nullptr;
// Enterprise-only widgets inside panel_session
static lv_obj_t* lbl_session_pct_sym = nullptr;  // "%" in smaller font
static lv_obj_t* lbl_spending_desc = nullptr;     // "of your monthly budget"
static lv_obj_t* lbl_spending_status = nullptr;   // "Under pace" / "On pace" / "Over pace"
static lv_obj_t* lbl_anim;      // status line: connection state + whimsical idle

// ---- Battery indicator (shared, on top) ----
static lv_obj_t* battery_img;
static lv_obj_t* logo_img;
static lv_image_dsc_t battery_dscs[6];  // empty, low, medium, full, charging, plug-zap (no battery)

// ---- Live-data freshness → which usage sub-view to show ----
// usage panels when data is flowing, an idle "Zzz" screen when the host is
// connected but no usage update landed within DATA_FRESH_MS, the pairing hint
// when BLE is down. Re-evaluated every loop in ui_tick_anim().
static lv_obj_t* idle_group;            // the "Zzz" idle screen
static uint32_t  last_data_ms = 0;      // lv_tick when the last valid usage update landed
static bool      data_received = false; // any valid update since boot
static bool      data_ok = true;        // last payload's ok flag; a {"ok":false} beat = "no fresh data"
static int       view_state = -1;       // -1 unknown / 0 pair / 1 idle / 2 usage
static const uint32_t DATA_FRESH_MS = 90000;  // usage counts as "live" within this window (daemon sends ~60s)

// ---- Shared ----
static lv_image_dsc_t logo_dsc;
static screen_t current_screen = SCREEN_USAGE;
static bool     s_ble_connected = false;   // cached BLE connection state
static uint32_t connected_at_ms = 0;       // when we last entered CONNECTED ("Connected" dwell)

// Animation state
static uint32_t anim_last_ms = 0;
static uint8_t anim_spinner_idx = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS     4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))  // 10: ping-pong 0..5..0

static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

// Forward decls — callbacks defined near ui_show_screen below
static void global_click_cb(lv_event_t* e);

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, L.panel_pad_x, 0);
    lv_obj_set_style_pad_right(panel, L.panel_pad_x, 0);
    lv_obj_set_style_pad_top(panel, L.panel_pad_y, 0);
    lv_obj_set_style_pad_bottom(panel, L.panel_pad_y, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

static void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, L.pill_font, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, L.pill_pad_x, 0);
    lv_obj_set_style_pad_right(lbl, L.pill_pad_x, 0);
    lv_obj_set_style_pad_top(lbl, L.pill_pad_y, 0);
    lv_obj_set_style_pad_bottom(lbl, L.pill_pad_y, 0);
    return lbl;
}

static void init_battery_icons(void) {
    if (L.small_icons) {
        init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_SMALL_W, ICON_BATTERY_SMALL_H, icon_battery_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_SMALL_W, ICON_BATTERY_LOW_SMALL_H, icon_battery_low_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_SMALL_W, ICON_BATTERY_MEDIUM_SMALL_H, icon_battery_medium_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_SMALL_W, ICON_BATTERY_FULL_SMALL_H, icon_battery_full_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_SMALL_W, ICON_BATTERY_CHARGING_SMALL_H, icon_battery_charging_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[5], ICON_PLUG_ZAP_SMALL_W, ICON_PLUG_ZAP_SMALL_H, icon_plug_zap_small_data);
        return;
    }
    init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_W, ICON_BATTERY_H, icon_battery_data);
    init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_W, ICON_BATTERY_LOW_H, icon_battery_low_data);
    init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_W, ICON_BATTERY_MEDIUM_H, icon_battery_medium_data);
    init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_W, ICON_BATTERY_FULL_H, icon_battery_full_data);
    init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, icon_battery_charging_data);
    init_icon_dsc_rgb565a8(&battery_dscs[5], ICON_PLUG_ZAP_W, ICON_PLUG_ZAP_H, icon_plug_zap_data);
}

// ======== Usage Screen ========

static lv_obj_t* make_usage_panel(lv_obj_t* parent, int y, const char* pill_text,
                                  lv_obj_t** out_pct, lv_obj_t** out_pill,
                                  lv_obj_t** out_bar, lv_obj_t** out_reset) {
    lv_obj_t* panel = make_panel(parent, L.margin, y, L.content_w, L.usage_panel_h);

    *out_pct = lv_label_create(panel);
    lv_label_set_text(*out_pct, "---%");
    lv_obj_set_style_text_font(*out_pct, L.pct_font, 0);
    lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
    lv_obj_set_pos(*out_pct, 0, 0);

    *out_pill = make_pill(panel, pill_text);
    lv_obj_align(*out_pill, LV_ALIGN_TOP_RIGHT, 0, 1);

    *out_bar = make_bar(panel, 0, L.usage_bar_y,
                        L.content_w - 2 * L.panel_pad_x, L.bar_h);

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, L.reset_font, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, L.usage_reset_y);

    return panel;
}

// Pairing hint — shown when disconnected so the screen isn't empty and the
// user knows how to (re)pair. Wording matches the 3-second release gesture.
static void build_pair_group(lv_obj_t* parent) {
    pair_group = lv_obj_create(parent);
    lv_obj_set_size(pair_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(pair_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(pair_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pair_group, 0, 0);
    lv_obj_set_style_pad_all(pair_group, 0, 0);
    lv_obj_clear_flag(pair_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* l1 = lv_label_create(pair_group);
    lv_label_set_text(l1, "To pair");
    lv_obj_set_style_text_font(l1, L.bt_status_font, 0);
    lv_obj_set_style_text_color(l1, COL_TEXT, 0);
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, L.pair_y1);

    lv_obj_t* l2 = lv_label_create(pair_group);
    lv_label_set_text(l2, "hold the power button");
    lv_obj_set_style_text_font(l2, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l2, COL_DIM, 0);
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, L.pair_y2);

    lv_obj_t* l3 = lv_label_create(pair_group);
    lv_label_set_text(l3, "for 3 seconds, then release");
    lv_obj_set_style_text_font(l3, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l3, COL_DIM, 0);
    lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, L.pair_y3);

    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);  // ui_update_ble_status decides
}

// Idle "Zzz" screen — shown when the host is connected but no usage update has
// landed recently (token expired, daemon down, host asleep…). Full-screen, like
// the pairing hint, so we never render hours-old numbers as if they were live.
static void build_idle_group(lv_obj_t* parent) {
    idle_group = lv_obj_create(parent);
    lv_obj_set_size(idle_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(idle_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(idle_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idle_group, 0, 0);
    lv_obj_set_style_pad_all(idle_group, 0, 0);
    lv_obj_clear_flag(idle_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    // A shrunk-down resting creature (the official cloud-ride animation)
    // sits between the header and the status line; the animated "Listening…"
    // status line carries the words, so no extra text is needed here.
    lv_obj_t* creature = splash_mini_create(idle_group, "cloud", L.idle_px);
    if (creature) lv_obj_align(creature, LV_ALIGN_CENTER, 0, -20);

    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);  // update_view_state decides
}

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(usage_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Usage");
    lv_obj_set_style_text_font(lbl_title, L.title_font, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    // The nudge balances the corner logo on the left; smaller on small
    // screens where the logo is 40px and the battery icon sits closer.
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);

    // Usage panels (shown when connected) live in a transparent full-size group
    // so they can be toggled against the pairing hint as one unit.
    usage_group = lv_obj_create(usage_container);
    lv_obj_set_size(usage_group, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_group, 0, 0);
    lv_obj_set_style_bg_opa(usage_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_group, 0, 0);
    lv_obj_set_style_pad_all(usage_group, 0, 0);
    lv_obj_clear_flag(usage_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    panel_session = make_usage_panel(usage_group, L.content_y, "Current",
                     &lbl_session_pct, &lbl_session_label,
                     &bar_session, &lbl_session_reset);

    // Enterprise-only overlays inside panel_session — hidden until enterprise data arrives
    lbl_session_pct_sym = lv_label_create(panel_session);
    lv_label_set_text(lbl_session_pct_sym, "%");
    lv_obj_set_style_text_font(lbl_session_pct_sym, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_session_pct_sym, COL_TEXT, 0);
    lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);

    lbl_spending_desc = lv_label_create(panel_session);
    lv_label_set_text(lbl_spending_desc, "of your monthly budget");
    lv_obj_set_style_text_font(lbl_spending_desc, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_spending_desc, COL_DIM, 0);
    lv_obj_set_pos(lbl_spending_desc, 0, L.usage_reset_y);
    lv_obj_add_flag(lbl_spending_desc, LV_OBJ_FLAG_HIDDEN);

    lbl_spending_status = lv_label_create(panel_session);
    lv_label_set_text(lbl_spending_status, "");
    lv_obj_set_style_text_font(lbl_spending_status, L.pace_font, 0);
    lv_obj_set_pos(lbl_spending_status, 0, L.usage_reset_y + 20);
    lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);

    panel_weekly = make_usage_panel(usage_group,
                     L.content_y + L.usage_panel_h + L.usage_panel_gap, "Weekly",
                     &lbl_weekly_pct, &lbl_weekly_label,
                     &bar_weekly, &lbl_weekly_reset);
    // Recolor enabled so enterprise period box can color pace and reset separately
    lv_label_set_recolor(lbl_weekly_reset, true);

    build_pair_group(usage_container);
    build_idle_group(usage_container);

    // Status line — always visible on the usage view. Driven by ui_tick_anim().
    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, L.anim_font, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, L.anim_y);
}

// ======== Weather / Stocks screens (optional, daemon-fed) ========
// Both stay out of the tap rotation until their first payload lands, and drop
// out again if the daemon stops refreshing them for EXTRA_STALE_MS.

static const uint32_t EXTRA_STALE_MS = 3UL * 60 * 60 * 1000;

static lv_obj_t* make_screen_container(lv_obj_t* scr) {
    lv_obj_t* c = lv_obj_create(scr);
    lv_obj_set_size(c, L.scr_w, L.scr_h);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(c, global_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    return c;
}

// Header title in the same slot as the Usage title. Long names step down to
// the smaller Tiempos, then re-centre in the gap between mascot and battery,
// and only then get cut with "…" — they never run under the corner icons.
static lv_obj_t* make_screen_title(lv_obj_t* parent) {
    lv_obj_t* t = lv_label_create(parent);
    lv_obj_set_style_text_font(t, L.title_font, 0);
    lv_obj_set_style_text_color(t, COL_TEXT, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);
    return t;
}

static void set_screen_title(lv_obj_t* t, const char* text) {
    const int mascot_w = L.small_icons ? CLAWD_STILL_SMALL_W : CLAWD_STILL_W;
    const int center = L.scr_w / 2 + L.title_nudge;
    const int left = L.margin + mascot_w + 8;
    const int right = L.scr_w - L.margin - L.batt_w - 8;
    const int centered_w = 2 * LV_MIN(center - left, right - center);
    const int gap_w = right - left;

    auto width_of = [&](const lv_font_t* f) {
        lv_point_t size;
        lv_text_get_size(&size, text, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        return (int)size.x;
    };

    lv_label_set_text(t, text);
    lv_obj_set_size(t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    const lv_font_t* fonts[] = {L.title_font, &font_tiempos_34};
    for (const lv_font_t* f : fonts) {
        if (width_of(f) <= centered_w) {
            lv_obj_set_style_text_font(t, f, 0);
            lv_obj_align(t, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);
            return;
        }
    }
    const lv_font_t* f = &font_tiempos_34;
    const int w = LV_MIN(width_of(f), gap_w);
    lv_obj_set_style_text_font(t, f, 0);
    lv_obj_set_size(t, w, lv_font_get_line_height(f));
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, left + (gap_w - w) / 2, L.title_y);
}

// ---- Weather ----

enum {
    WX_SUN, WX_MOON, WX_CLOUD_SUN, WX_CLOUD_MOON, WX_CLOUD, WX_FOG,
    WX_DRIZZLE, WX_RAIN, WX_SNOW, WX_THUNDER, WX_ICON_COUNT,
};
static lv_image_dsc_t wx_big_dscs[WX_ICON_COUNT];
static lv_image_dsc_t wx_small_dscs[WX_ICON_COUNT];

static void init_weather_icons(void) {
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

// The art is white; the sun takes the brand accent, everything else stays ivory.
static void set_wx_icon(lv_obj_t* img, const lv_image_dsc_t* dscs, int icon) {
    lv_image_set_src(img, &dscs[icon]);
    lv_obj_set_style_image_recolor(img, icon == WX_SUN ? COL_ACCENT : COL_TEXT, 0);
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

static lv_obj_t* weather_container;
static lv_obj_t* wx_title;
static lv_obj_t* wx_icon;
static lv_obj_t* wx_temp;
static lv_obj_t* wx_cond;
static lv_obj_t* wx_detail;
static lv_obj_t* wx_tiles[WX_FORECAST_DAYS];
static lv_obj_t* wx_tile_day[WX_FORECAST_DAYS];
static lv_obj_t* wx_tile_icon[WX_FORECAST_DAYS];
static lv_obj_t* wx_tile_temp[WX_FORECAST_DAYS];
static bool      weather_received = false;
static uint32_t  weather_ms = 0;

static void init_weather_screen(lv_obj_t* scr) {
    weather_container = make_screen_container(scr);
    wx_title = make_screen_title(weather_container);

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

    // Forecast: equal tiles across the content width.
    const int gap = L.usage_panel_gap;
    const int tile_w = (L.content_w - (WX_FORECAST_DAYS - 1) * gap) / WX_FORECAST_DAYS;
    for (int i = 0; i < WX_FORECAST_DAYS; i++) {
        lv_obj_t* tile = make_panel(weather_container, L.margin + i * (tile_w + gap),
                                    L.wx_fc_y, tile_w, L.wx_fc_h);
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
}

void ui_update_weather(const WeatherData* d) {
    char buf[64];
    set_screen_title(wx_title, d->location[0] ? d->location : "Weather");
    set_wx_icon(wx_icon, wx_big_dscs, wx_icon_for(d->code, d->is_day));
    snprintf(buf, sizeof(buf), "%ld\xC2\xB0", lroundf(d->temp));
    lv_label_set_text(wx_temp, buf);
    lv_label_set_text(wx_cond, wx_text_for(d->code));
    snprintf(buf, sizeof(buf), "Feels %d\xC2\xB0  \xC2\xB7  H %d\xC2\xB0  L %d\xC2\xB0",
             d->feels, d->hi, d->lo);
    lv_label_set_text(wx_detail, buf);

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

    weather_received = true;
    weather_ms = lv_tick_get();
}

// ---- Stocks ----

struct StockRow {
    lv_obj_t* panel;
    lv_obj_t* sym;
    lv_obj_t* live;          // dot beside the symbol while the market is open
    lv_obj_t* name;
    lv_obj_t* base;          // previous close, dashed
    lv_obj_t* spark;
    lv_obj_t* price;
    lv_obj_t* chg;
    lv_point_precise_t base_pts[2];
    lv_point_precise_t spark_pts[STOCK_HIST_MAX];
    StockQuote q;
    bool have;
};
static StockRow  stock_rows[STOCK_MAX];
static lv_obj_t* stocks_container;
static int       stock_count = 0;      // rows the daemon says it's sending
static uint32_t  stock_ms = 0;

static void init_stocks_screen(lv_obj_t* scr) {
    stocks_container = make_screen_container(scr);
    set_screen_title(make_screen_title(stocks_container), "Stocks");

    for (int i = 0; i < STOCK_MAX; i++) {
        StockRow* r = &stock_rows[i];
        r->panel = make_panel(stocks_container, L.margin, 0, L.content_w, L.st_row_h);
        lv_obj_set_style_pad_top(r->panel, L.st_pad_y, 0);
        lv_obj_set_style_pad_bottom(r->panel, L.st_pad_y, 0);

        r->sym = lv_label_create(r->panel);
        lv_obj_set_style_text_font(r->sym, L.st_sym_font, 0);
        lv_obj_set_style_text_color(r->sym, COL_TEXT, 0);
        lv_obj_align(r->sym, LV_ALIGN_TOP_LEFT, 0, 0);

        r->live = lv_obj_create(r->panel);
        lv_obj_set_size(r->live, 8, 8);
        lv_obj_set_style_radius(r->live, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(r->live, COL_ACCENT, 0);
        lv_obj_set_style_bg_opa(r->live, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(r->live, 0, 0);
        lv_obj_clear_flag(r->live, LV_OBJ_FLAG_CLICKABLE);

        r->name = lv_label_create(r->panel);
        lv_label_set_long_mode(r->name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(r->name, L.st_left_w, lv_font_get_line_height(L.st_name_font));   // one line, cut with "…"
        lv_obj_set_style_text_font(r->name, L.st_name_font, 0);
        lv_obj_set_style_text_color(r->name, COL_DIM, 0);
        lv_obj_align(r->name, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        r->base = lv_line_create(r->panel);
        lv_obj_set_style_line_width(r->base, 1, 0);
        lv_obj_set_style_line_color(r->base, COL_DIM, 0);
        lv_obj_set_style_line_opa(r->base, LV_OPA_40, 0);
        lv_obj_set_style_line_dash_width(r->base, 4, 0);
        lv_obj_set_style_line_dash_gap(r->base, 4, 0);

        r->spark = lv_line_create(r->panel);
        lv_obj_set_style_line_width(r->spark, L.st_line_w, 0);
        lv_obj_set_style_line_rounded(r->spark, true, 0);

        r->price = lv_label_create(r->panel);
        lv_obj_set_style_text_font(r->price, L.st_price_font, 0);
        lv_obj_set_style_text_color(r->price, COL_TEXT, 0);
        lv_obj_align(r->price, LV_ALIGN_TOP_RIGHT, 0, 0);

        r->chg = lv_label_create(r->panel);
        lv_obj_set_style_text_font(r->chg, L.st_chg_font, 0);
        lv_obj_align(r->chg, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

        r->have = false;
        lv_obj_add_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void format_price(char* buf, size_t len, const char* cur, float p) {
    const char* sym = strcmp(cur, "USD") == 0 ? "$"
                    : strcmp(cur, "EUR") == 0 ? "\xE2\x82\xAC"
                    : strcmp(cur, "GBP") == 0 ? "\xC2\xA3"
                    : strcmp(cur, "JPY") == 0 ? "\xC2\xA5"
                    : nullptr;
    char num[16];
    snprintf(num, sizeof(num), p >= 10000.0f ? "%.0f" : "%.2f", p);
    if (sym) snprintf(buf, len, "%s%s", sym, num);
    else     snprintf(buf, len, "%s %s", cur, num);
}

// Lay out rows for `stock_count` and redraw each row that has a quote. Row
// height shrinks so that STOCK_MAX rows still fit under the header.
static void layout_stock_rows(void) {
    const int n = stock_count;
    if (n <= 0) return;
    const int avail = L.scr_h - L.content_y - L.margin;
    int h = (avail - (n - 1) * L.st_row_gap) / n;
    if (h > L.st_row_h) h = L.st_row_h;
    const int inner_h = h - 2 * L.st_pad_y;
    const int spark_x = L.st_left_w + (L.content_w - 2 * L.panel_pad_x - L.st_left_w - L.st_spark_w) / 4;
    const int spark_h = inner_h - 8;
    const int spark_y = (inner_h - spark_h) / 2;

    for (int i = 0; i < STOCK_MAX; i++) {
        StockRow* r = &stock_rows[i];
        if (i >= n || !r->have) {
            lv_obj_add_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_y(r->panel, L.content_y + i * (h + L.st_row_gap));
        lv_obj_set_height(r->panel, h);

        const StockQuote* q = &r->q;
        char buf[32];
        lv_label_set_text(r->sym, q->symbol);
        lv_obj_align_to(r->live, r->sym, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
        if (q->open) lv_obj_clear_flag(r->live, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(r->live, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(r->name, q->name);

        format_price(buf, sizeof(buf), q->currency, q->price);
        lv_label_set_text(r->price, buf);

        const float pct = q->prev_close > 0 ? (q->price - q->prev_close) / q->prev_close * 100.0f : 0.0f;
        const bool up = pct >= 0.0f;
        snprintf(buf, sizeof(buf), "%s%.2f%%", up ? "+" : "\xE2\x88\x92", fabsf(pct));
        lv_label_set_text(r->chg, buf);
        lv_obj_set_style_text_color(r->chg, up ? COL_GREEN : COL_RED, 0);

        // Sparkline: 0..99 → top..bottom of the plot box. Mid-session it only
        // spans the elapsed share of the trading day, so the x axis is time.
        const int pts = q->n_hist;
        const int span_w = (L.st_spark_w - 1) * q->session_pct / 100;
        for (int k = 0; k < pts; k++) {
            r->spark_pts[k].x = pts > 1 ? k * span_w / (pts - 1) : 0;
            r->spark_pts[k].y = (99 - q->hist[k]) * (spark_h - 1) / 99;
        }
        lv_line_set_points(r->spark, r->spark_pts, pts);
        lv_obj_set_style_line_color(r->spark, up ? COL_GREEN : COL_RED, 0);
        lv_obj_set_pos(r->spark, spark_x, spark_y);

        const int by = (99 - q->baseline) * (spark_h - 1) / 99;
        r->base_pts[0] = {0, by};
        r->base_pts[1] = {L.st_spark_w - 1, by};
        lv_line_set_points(r->base, r->base_pts, 2);
        lv_obj_set_pos(r->base, spark_x, spark_y);

        lv_obj_clear_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_update_stock(const StockQuote* q) {
    if (q->index < 0 || q->index >= STOCK_MAX) return;
    stock_rows[q->index].q = *q;
    stock_rows[q->index].have = true;
    stock_count = q->count > STOCK_MAX ? STOCK_MAX : q->count;
    stock_ms = lv_tick_get();
    layout_stock_rows();
}

static bool screen_available(screen_t s) {
    const uint32_t now = lv_tick_get();
    switch (s) {
    case SCREEN_WEATHER: return weather_received && now - weather_ms < EXTRA_STALE_MS;
    case SCREEN_STOCKS:  return stock_count > 0 && now - stock_ms < EXTRA_STALE_MS;
    default:             return true;
    }
}

// ---- Attention: the mascot walk-on (splash_mascot_attention) plus the
// message on the status line. Ends on a tap, after 2 minutes, or ui_hide_alert().
static const uint32_t ALERT_TIMEOUT_MS = 120000;
static bool        alert_active = false;
static lv_timer_t* alert_timer;

static void alert_timer_cb(lv_timer_t* t) { (void)t; ui_hide_alert(); }

// Registered on the input device, so it sees every tap. A tap that dismisses
// does nothing else.
static void alert_tap_cb(lv_event_t* e) {
    if (!alert_active) return;
    ui_hide_alert();
    lv_indev_stop_processing((lv_indev_t*)lv_event_get_user_data(e));
}

static void init_alert(void) {
    alert_timer = lv_timer_create(alert_timer_cb, ALERT_TIMEOUT_MS, NULL);
    lv_timer_pause(alert_timer);
    lv_indev_t* indev = lv_indev_get_next(NULL);
    if (indev) lv_indev_add_event_cb(indev, alert_tap_cb, LV_EVENT_CLICKED, indev);
    lv_obj_set_style_text_align(lbl_anim, LV_TEXT_ALIGN_CENTER, 0);
}

// ======== Public API ========

void ui_init(void) {
    compute_layout(board_caps());

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

#ifndef BOARD_HAS_PSRAM
    // Static corner mascot (see clawd_still.h) — the animated one needs PSRAM.
    if (L.small_icons) init_icon_dsc_rgb565a8(&logo_dsc, CLAWD_STILL_SMALL_W, CLAWD_STILL_SMALL_H, clawd_still_small_data);
    else               init_icon_dsc_rgb565a8(&logo_dsc, CLAWD_STILL_W, CLAWD_STILL_H, clawd_still_data);
#endif
    init_battery_icons();

    init_usage_screen(scr);
    init_weather_icons();
    init_weather_screen(scr);
    init_stocks_screen(scr);
    splash_init(scr);

    if (splash_get_root()) {
        lv_obj_add_event_cb(splash_get_root(), global_click_cb, LV_EVENT_CLICKED, NULL);
    }

    // Corner mascot in the old logo slot. The still Clawd is shorter than the
    // 80/40 px slot the spark logo used; center it vertically in that slot.
    {
        const int slot  = L.small_icons ? LOGO_SMALL_HEIGHT : LOGO_HEIGHT;
        const int art_h = L.small_icons ? CLAWD_STILL_SMALL_H : CLAWD_STILL_H;
        const int top   = L.logo_y + (slot - art_h) / 2;
#ifdef BOARD_HAS_PSRAM
        // Animated: idles, does acts, and takes walk-off/lurk trips.
        splash_mascot_create(scr, L.margin, top + art_h, L.small_icons ? 2 : 3);
#else
        logo_img = lv_image_create(scr);
        lv_image_set_src(logo_img, &logo_dsc);
        lv_obj_set_pos(logo_img, L.margin, top);
#endif
    }

    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, L.scr_w - L.batt_w - L.margin, L.batt_y);
    // Boards without battery telemetry never show the indicator (per the HAL
    // contract; previously every board drew the empty-battery glyph).
    if (!board_caps().has_battery) {
        lv_obj_del(battery_img);
        battery_img = nullptr;
    }

    init_alert();
}

void ui_update(const UsageData* data) {
    if (!data->valid) return;
    data_ok = data->ok;
    if (!data->ok) return;          // a {"ok":false} "no data" beat → fall through to idle, keep last numbers
    last_data_ms = lv_tick_get();   // a real usage update just landed
    data_received = true;

    if (data->clock_epoch > 0) {    // daemon supplied wall-clock time → drive the title clock
        clock_base_epoch = data->clock_epoch;
        clock_base_ms = last_data_ms;
        clock_fmt = data->clock_fmt;
    } else if (clock_base_epoch != 0) {   // clock turned off daemon-side → revert title to "Usage"
        clock_base_epoch = 0;
        clock_last_min = -1;
        lv_label_set_text(lbl_title, "Usage");
    }

    int s_pct = (int)(data->session_pct + 0.5f);

    if (data->enterprise) {
        // Spending box: big number-only label + small "%" symbol + desc + pace
        lv_obj_set_style_text_font(lbl_session_pct, L.ent_pct_font, 0);
        lv_label_set_text(lbl_session_label, "Spending");
        lv_obj_add_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status,   LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_text_font(lbl_session_pct, L.pct_font, 0);
        lv_label_set_text(lbl_session_label, "Current");
        lv_obj_clear_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    }

    char buf[48];

    // Pace vars used in both enterprise blocks below
    const char* pace_text = "Under pace";
    lv_color_t  pace_color = COL_GREEN;
    const char* pace_hex   = "788c5d";   // matches THEME_GREEN
    if (data->session_pct > (float)data->time_pct + 15.0f) {
        pace_text = "Over pace";  pace_color = COL_RED;   pace_hex = "c0392b";
    } else if (data->session_pct > (float)data->time_pct - 15.0f) {
        pace_text = "On pace";    pace_color = COL_AMBER; pace_hex = "d97757";
    }

    if (data->enterprise) {
        lv_label_set_text_fmt(lbl_session_pct, "%d", s_pct);
        lv_obj_align_to(lbl_session_pct_sym, lbl_session_pct,
                        LV_ALIGN_OUT_RIGHT_TOP, 4, 12);
    } else {
        lv_label_set_text_fmt(lbl_session_pct, "%d%%", s_pct);
        format_reset_time(data->session_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_session_reset, buf);
    }

    lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_session, pct_color(data->session_pct), LV_PART_INDICATOR);

    if (data->enterprise) {
        // Period box: time % + dynamic pace color + "Resets <date>" label
        lv_label_set_text(lbl_weekly_label, "Period");
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", data->time_pct);
        lv_bar_set_value(bar_weekly, data->time_pct, LV_ANIM_ON);
        lv_color_t bar_pace = (data->session_pct <= (float)data->time_pct) ? COL_GREEN :
                              (data->session_pct <= (float)data->time_pct + 15.0f) ? COL_AMBER :
                              COL_RED;
        lv_obj_set_style_bg_color(bar_weekly, bar_pace, LV_PART_INDICATOR);
        snprintf(buf, sizeof(buf), "#%s %s# - #faf9f5 Resets %s#",
                 pace_hex, pace_text, data->reset_date);
        lv_label_set_text(lbl_weekly_reset, buf);
    } else {
        int w_pct = (int)(data->weekly_pct + 0.5f);
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", w_pct);
        lv_bar_set_value(bar_weekly, w_pct, LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar_weekly, pct_color(data->weekly_pct), LV_PART_INDICATOR);
        format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_weekly_reset, buf);
    }
}

// Pick the usage-view sub-screen: pairing hint (BLE down), the idle "Zzz" screen
// (connected but data has gone stale), or the live usage panels. Only re-lays-out
// on an actual change. The animated status line stays visible everywhere — it
// reads "Listening…" on the idle screen, keeping it alive rather than frozen.
static void update_view_state(void) {
    if (!usage_group || !pair_group || !idle_group) return;
    int v;
    if (!s_ble_connected) {
        v = 0;  // pairing hint
    } else if (data_received && data_ok && (lv_tick_get() - last_data_ms) < DATA_FRESH_MS) {
        v = 2;  // live usage
    } else {
        v = 1;  // idle / Zzz
    }
    if (v == view_state) return;
    view_state = v;
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(v == 0 ? pair_group : v == 1 ? idle_group : usage_group,
                      LV_OBJ_FLAG_HIDDEN);
}

void ui_tick_anim(void) {
    if (current_screen != SCREEN_USAGE) return;
    update_view_state();
    if (view_state == 1) splash_mini_tick();   // animate the sleeping creature on the idle screen

    uint32_t now = lv_tick_get();

    // Title clock: once the daemon has sent wall-clock time, replace "Usage" with
    // the live time, advanced locally so it ticks every minute between payloads.
    if (clock_base_epoch > 0) {
        time_t cur = (time_t)(clock_base_epoch + (now - clock_base_ms) / 1000);
        struct tm tmv;
        gmtime_r(&cur, &tmv);   // epoch is already local wall-clock → gmtime keeps it as-is
        if (tmv.tm_min != clock_last_min) {   // only rewrite the title when the minute changes
            clock_last_min = tmv.tm_min;
            char tbuf[12];
            if (clock_fmt == 12) {
                int h12 = tmv.tm_hour % 12;
                if (h12 == 0) h12 = 12;
                snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h12, tmv.tm_min,
                         tmv.tm_hour < 12 ? "AM" : "PM");
            } else {
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
            }
            lv_label_set_text(lbl_title, tbuf);
        }
    }

    if (alert_active) return;          // the status line holds the alert message

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms < spinner_ms[anim_spinner_idx]) return;
    anim_last_ms = now;
    anim_phase = (anim_phase + 1) % SPINNER_PHASES;
    anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                    : (SPINNER_PHASES - anim_phase);

    // Status text by priority. Whimsical messages only when connected & settled.
    const char* text;
    if (!s_ble_connected) {
        text = "Waiting";              // advertising / waiting for a host connection
    } else if (view_state == 1) {      // idle — alternate so it reads as alive AND data-less
        text = (anim_msg_idx & 1) ? "No data" : "Listening";
    } else if (now - connected_at_ms < 5000) {
        text = "Connected";
    } else {
        text = anim_messages[anim_msg_idx];
    }

    // All states share the whimsical style: "<glyph> <Title-case word>…"
    static char buf[80];
    snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
             spinner_frames[anim_spinner_idx], text);
    lv_label_set_text(lbl_anim, buf);
}

static screen_t prev_non_splash_screen = SCREEN_USAGE;
static void apply_battery_visibility(void) {
    if (!battery_img) return;
    if (current_screen == SCREEN_SPLASH) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    else                                  lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
}

static void global_click_cb(lv_event_t* e) {
    (void)e;
    ui_next_screen();
}

void ui_next_screen(void) {
    int s = current_screen;
    do {
        s = (s + 1) % SCREEN_COUNT;
    } while (!screen_available((screen_t)s));
    ui_show_screen((screen_t)s);
}

void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(weather_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(stocks_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:  splash_show(); break;
    case SCREEN_USAGE:   lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_WEATHER: lv_obj_clear_flag(weather_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_STOCKS:  lv_obj_clear_flag(stocks_container, LV_OBJ_FLAG_HIDDEN); break;
    default: break;
    }

    splash_mascot_set_visible(screen != SCREEN_SPLASH);
    if (logo_img) {
        if (screen == SCREEN_SPLASH) lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                          lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    }

    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    apply_battery_visibility();
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_ble_status(ble_state_t state, const char* name, const char* mac) {
    (void)name; (void)mac;
    bool was_connected = s_ble_connected;
    s_ble_connected = (state == BLE_STATE_CONNECTED);

    if (s_ble_connected && !was_connected) connected_at_ms = lv_tick_get();
    // pair / idle / usage — picked from connection + data freshness.
    update_view_state();
}

void ui_update_battery(int percent, bool charging, bool usb_power) {
    if (!battery_img) return;
    int idx;
    if (percent < 0 && usb_power) {
        idx = 5;   // no battery, running on USB: show the plug-zap glyph, not an empty battery
    } else if (charging) {
        idx = 4;
    } else if (percent < 0) {
        idx = 0;
    } else if (percent <= 10) {
        idx = 0;
    } else if (percent <= 35) {
        idx = 1;
    } else if (percent <= 75) {
        idx = 2;
    } else {
        idx = 3;
    }
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    apply_battery_visibility();
}

void ui_show_alert(const char* message) {
    alert_active = true;
    if (current_screen != SCREEN_USAGE) ui_show_screen(SCREEN_USAGE);   // the message lives on the status line there
    splash_mascot_attention(true);
    lv_label_set_long_mode(lbl_anim, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_anim, L.content_w);
    lv_label_set_text(lbl_anim, message);
    lv_timer_reset(alert_timer);
    lv_timer_resume(alert_timer);
}

void ui_hide_alert(void) {
    if (!alert_active) return;
    alert_active = false;
    splash_mascot_attention(false);
    lv_label_set_long_mode(lbl_anim, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_anim, LV_SIZE_CONTENT);
    lv_timer_pause(alert_timer);
}
