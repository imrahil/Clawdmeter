#include "ui.h"
#include "ui_internal.h"
#include "splash.h"
#include "idle.h"
#include <lvgl.h>
#include <Preferences.h>
#include <time.h>
#include "logo.h"
#include "clawd_still.h"
#include "icons.h"
#include "hal/board_caps.h"
#include <math.h>

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


Layout L = {};

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
        L.wx_main_y = 90;
        L.wx_cond_y = 216;
        L.wx_detail_y = 252;
        L.wx_extra_y = 284;
        L.wx_fc_y = 316;
        L.wx_fc_h = 100;
        L.wx_fc_icon_px = 40;
        L.wx_fc_pad_y = 4;
        L.wx_temp_font   = &font_styrene_temp_96;
        L.wx_cond_font   = &font_styrene_28;
        L.wx_detail_font = with_symbols(&font_styrene_24, &font_sym_24);
        L.wx_extra_font  = with_symbols(&font_styrene_20, &font_sym_20);
        L.wx_fc_font     = with_symbols(&font_styrene_24, &font_sym_24);
        L.st_row_h = 82;
        L.st_row_gap = 8;
        L.st_pad_y = 8;
        L.st_left_w = 140;
        L.st_spark_w = 120;
        L.st_line_w = 3;
        L.st_sym_font   = &font_styrene_28;
        L.st_name_font  = &font_styrene_16;
        L.st_price_font = with_symbols(&font_styrene_28, &font_sym_28);
        L.st_chg_font   = with_symbols(&font_styrene_20, &font_sym_20);
        L.f_head  = with_symbols(&font_styrene_28, &font_sym_28);
        L.f_body  = with_symbols(&font_styrene_24, &font_sym_24);
        L.f_small = with_symbols(&font_styrene_16, &font_sym_16);
        L.f_clock = &font_tiempos_56;
        L.mini_cell = 2;
        L.arc_w = 10;
        L.dot_px = 12;
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
        L.wx_extra_y = 247;
        L.wx_fc_y = 272;
        L.wx_fc_h = 104;
        L.wx_fc_icon_px = 40;
        L.wx_fc_pad_y = 6;
        L.wx_temp_font   = &font_styrene_temp_72;
        L.wx_cond_font   = &font_styrene_24;
        L.wx_detail_font = with_symbols(&font_styrene_20, &font_sym_20);
        L.wx_extra_font  = with_symbols(&font_styrene_16, &font_sym_16);
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
        L.f_head  = with_symbols(&font_styrene_24, &font_sym_24);
        L.f_body  = with_symbols(&font_styrene_20, &font_sym_20);
        L.f_small = with_symbols(&font_styrene_14, &font_sym_14);
        L.f_clock = &font_tiempos_56;
        L.mini_cell = 2;
        L.arc_w = 8;
        L.dot_px = 10;
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
        L.wx_icon_px = 48;
        L.wx_main_y = 38;
        L.wx_cond_y = 88;
        L.wx_detail_y = 105;
        L.wx_extra_y = 120;
        L.wx_fc_y = 137;
        L.wx_fc_h = 66;
        L.wx_fc_icon_px = 26;
        L.wx_fc_pad_y = 5;
        L.wx_temp_font   = with_symbols(&font_styrene_48, &font_sym_48);
        L.wx_cond_font   = &font_styrene_14;
        L.wx_detail_font = with_symbols(&font_styrene_12, &font_sym_12);
        L.wx_extra_font  = with_symbols(&font_styrene_12, &font_sym_12);
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
        L.f_head  = with_symbols(&font_styrene_14, &font_sym_14);
        L.f_body  = with_symbols(&font_styrene_12, &font_sym_12);
        L.f_small = with_symbols(&font_styrene_12, &font_sym_12);
        L.f_clock = &font_tiempos_34;
        L.mini_cell = 1;
        L.arc_w = 5;
        L.dot_px = 8;
    }

    L.content_w = L.scr_w - 2 * L.margin;
    L.footer_reserve = -L.anim_y + lv_font_get_line_height(L.anim_font) + 6;
}


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
static UsageData last_usage = {};
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

lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

void format_reset_time(int mins, char* buf, size_t len) {
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


lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
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

lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
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

void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

lv_obj_t* make_pill(lv_obj_t* parent, const char* text) {
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

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t* make_dot(lv_obj_t* parent, int px, lv_color_t color) {
    lv_obj_t* d = lv_obj_create(parent);
    lv_obj_set_size(d, px, px);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, color, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_pad_all(d, 0, 0);
    lv_obj_clear_flag(d, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return d;
}

lv_obj_t* make_arc(lv_obj_t* parent, int d, int rotation, int sweep, int range_max) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_set_size(a, d, d);
    lv_arc_set_rotation(a, rotation);
    lv_arc_set_bg_angles(a, 0, sweep);
    lv_arc_set_range(a, 0, range_max);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_width(a, L.arc_w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, L.arc_w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
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
}


// Header title in the same slot as the Usage title. Long names step down to
// the smaller Tiempos, then re-centre in the gap between mascot and battery,
// and only then get cut with "…" — they never run under the corner icons.
void set_screen_title(lv_obj_t* t, const char* text) {
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


lv_obj_t* make_screen_title(lv_obj_t* parent, const char* text) {
    lv_obj_t* t = lv_label_create(parent);
    lv_obj_set_style_text_font(t, L.title_font, 0);
    lv_obj_set_style_text_color(t, COL_TEXT, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);
    set_screen_title(t, text);
    return t;
}


static bool spinner_step(uint32_t now);
static void update_view_state(void);

// ======== Screen framework ========
// Usage is permanent (it carries the idle creature and the alert). Every other
// non-splash screen is a ScreenDef built on show and deleted on leave.

static const ScreenDef* screen_def(screen_t s) {
    switch (s) {
    case SCREEN_PACE:     return &pace_screen;
    case SCREEN_ACTIVITY: return &activity_screen;
    case SCREEN_FOCUS:    return &focus_screen;
    case SCREEN_SYSTEM:   return &system_screen;
    case SCREEN_WEATHER:  return &weather_screen;
    case SCREEN_STOCKS:   return &stocks_screen;
    default:              return nullptr;
    }
}

static lv_obj_t*        screen_root = nullptr;   // root of the built ScreenDef screen
static const ScreenDef* screen_built = nullptr;

static void teardown_screen(void) {
    if (!screen_built) return;
    screen_built->destroy();
    lv_obj_delete(screen_root);
    screen_root = nullptr;
    screen_built = nullptr;
}

static void build_screen(const ScreenDef* def) {
    lv_obj_t* c = lv_obj_create(lv_screen_active());
    lv_obj_set_size(c, L.scr_w, L.scr_h);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    screen_root = c;
    screen_built = def;
    def->build(c);
}

static bool screen_available(screen_t s, uint32_t now) {
    const ScreenDef* d = screen_def(s);
    return d ? d->available(now) : true;
}

static bool screen_in_carousel(screen_t s, uint32_t now) {
    const ScreenDef* d = screen_def(s);
    if (!d) return true;
    return d->in_carousel ? d->in_carousel(now) : d->available(now);
}

// ======== Status line ========
// One label at the bottom of every screen but the splash: Usage's whimsical
// spinner, each other screen's one live fact, the attention message, and —
// alternating with the rest while status.claude.com reports trouble — the
// incident, behind an amber / red dot.

static lv_obj_t* lbl_status;
static lv_obj_t* status_dot;
static char      status_shown[96];
static bool      status_scrolling = false;

static ServiceStatus svc = {};
static uint32_t      svc_ms = 0;
static const uint32_t SVC_STALE_MS = 30UL * 60 * 1000;
#define INCIDENT_SWAP_MS 6000   // incident and the screen's own text take turns

static void init_status_line(lv_obj_t* scr) {
    lbl_status = make_label(scr, L.anim_font, COL_ACCENT, "");
    lv_obj_set_style_text_align(lbl_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, L.anim_y);
    status_dot = make_dot(scr, L.dot_px, COL_AMBER);
    lv_obj_add_flag(status_dot, LV_OBJ_FLAG_HIDDEN);
}

// Show `text`; scroll it when it can't fit the content width. `dot` (if not
// null) colours the incident dot left of the text.
static void status_set(const char* text, lv_color_t color, const lv_color_t* dot) {
    if (strcmp(text, status_shown) == 0) return;
    strlcpy(status_shown, text, sizeof(status_shown));
    const int room = L.content_w - (dot ? L.dot_px + 10 : 0);
    lv_point_t size;
    lv_text_get_size(&size, text, L.anim_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const bool scroll = size.x > room;
    if (scroll != status_scrolling) {
        status_scrolling = scroll;
        lv_label_set_long_mode(lbl_status, scroll ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl_status, scroll ? room : LV_SIZE_CONTENT);
    }
    lv_obj_set_style_text_color(lbl_status, color, 0);
    lv_label_set_text(lbl_status, text);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, dot ? (L.dot_px + 10) / 2 : 0, L.anim_y);
    if (dot) {
        lv_obj_set_style_bg_color(status_dot, *dot, 0);
        lv_obj_update_layout(lbl_status);
        lv_obj_align_to(status_dot, lbl_status, LV_ALIGN_OUT_LEFT_MID, -10, 0);
        lv_obj_clear_flag(status_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(status_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

// Forget what's on the line so the next status_set() / tick redraws it.
static void status_invalidate(void) {
    status_shown[0] = '\x01';
    status_shown[1] = '\0';
}

static bool incident_active(uint32_t now) {
    return svc.level > 0 && svc.msg[0] && now - svc_ms < SVC_STALE_MS;
}

void ui_update_service_status(const ServiceStatus* s) {
    svc = *s;
    svc_ms = lv_tick_get();
    status_invalidate();
}

// Usage's status text by priority. Whimsical messages only when connected & settled.
static const char* usage_status_text(uint32_t now) {
    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }
    if (!s_ble_connected) return "Waiting";       // advertising / waiting for a host connection
    if (view_state == 1) return (anim_msg_idx & 1) ? "No data" : "Listening";   // idle: alive AND data-less
    if (now - connected_at_ms < 5000) return "Connected";
    return anim_messages[anim_msg_idx];
}

static bool alert_active = false;

static void tick_status_line(uint32_t now) {
    if (current_screen == SCREEN_SPLASH || alert_active) return;
    const bool stepped = spinner_step(now);

    if (incident_active(now) && (now / INCIDENT_SWAP_MS) % 2 == 1) {
        const lv_color_t dot = svc.level >= 2 ? COL_RED : COL_AMBER;
        status_set(svc.msg, COL_TEXT, &dot);
        return;
    }
    const ScreenDef* d = screen_def(current_screen);
    const char* fact = d ? d->status_text(now) : usage_status_text(now);
    if (!fact[0]) {
        status_set("", COL_ACCENT, nullptr);
        return;
    }
    if (!stepped && status_shown[0]) return;   // the spinner sets the redraw cadence
    char buf[96];
    snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6", spinner_frames[anim_spinner_idx], fact);
    status_set(buf, COL_ACCENT, nullptr);
}

// ======== Carousel ========
// Every 30 s the next screen with fresh data comes up on its own. A tap on any
// screen without a tap action of its own pauses / resumes it (remembered
// across reboots); +/- restart the 30 s.

#define CAROUSEL_MS 30000
static bool     carousel_on = true;
static uint32_t carousel_ms = 0;
static lv_obj_t*   toast = nullptr;
static lv_timer_t* toast_timer = nullptr;

static void toast_hide_cb(lv_timer_t* t) {
    (void)t;
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(toast_timer);
}

static void init_toast(void) {
    toast = make_pill(lv_layer_top(), "");
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_border_color(toast, COL_DIM, 0);
    lv_obj_set_style_border_opa(toast, LV_OPA_40, 0);
    lv_obj_set_style_text_font(toast, L.f_body, 0);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -L.footer_reserve - 8);
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    toast_timer = lv_timer_create(toast_hide_cb, 1500, NULL);
    lv_timer_pause(toast_timer);
}

static void show_toast(const char* text) {
    lv_label_set_text(toast, text);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(toast_timer);
    lv_timer_resume(toast_timer);
}

static void carousel_toggle(void) {
    carousel_on = !carousel_on;
    carousel_ms = lv_tick_get();
    Preferences prefs;
    prefs.begin("clawdmeter", false);
    prefs.putUChar("carousel", carousel_on ? 1 : 0);
    prefs.end();
    show_toast(carousel_on ? "Carousel on" : "Carousel paused");
}

static void step_screen(int dir, bool carousel) {
    const uint32_t now = lv_tick_get();
    int s = current_screen;
    for (int i = 0; i < SCREEN_COUNT; i++) {
        s = (s + dir + SCREEN_COUNT) % SCREEN_COUNT;
        if (carousel ? screen_in_carousel((screen_t)s, now) : screen_available((screen_t)s, now)) break;
    }
    carousel_ms = now;
    if (s != current_screen) ui_show_screen((screen_t)s);
}

void ui_next_screen(void) { step_screen(+1, false); }
void ui_prev_screen(void) { step_screen(-1, false); }

static void tick_carousel(uint32_t now) {
    if (!carousel_on || alert_active || idle_is_asleep()) {
        carousel_ms = now;
        return;
    }
    if (now - carousel_ms >= CAROUSEL_MS) step_screen(+1, true);
}

// ======== Touch ========
// Buttons navigate; a tap is the screen's own action (Focus: start / pause)
// or, elsewhere, pauses the carousel. A long press is screen-specific (Focus:
// reset). While an alert is up, any tap only dismisses it.

static void touch_cb(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (alert_active) {
        if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED) ui_hide_alert();
        return;
    }
    const ScreenDef* d = screen_def(current_screen);
    if (code == LV_EVENT_SHORT_CLICKED) {
        if (d && d->tap) d->tap();
        else             carousel_toggle();
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (d && d->long_press) d->long_press();
    }
}

const UsageData* ui_last_usage(void) { return &last_usage; }

// ---- Attention: the mascot walk-on (splash_mascot_attention) plus the
// message on the status line. Ends on a tap, after 2 minutes, or ui_hide_alert().
static const uint32_t ALERT_TIMEOUT_MS = 120000;
static lv_timer_t* alert_timer;

static void alert_timer_cb(lv_timer_t* t) { (void)t; ui_hide_alert(); }

static void init_input(void) {
    alert_timer = lv_timer_create(alert_timer_cb, ALERT_TIMEOUT_MS, NULL);
    lv_timer_pause(alert_timer);
    lv_indev_t* indev = lv_indev_get_next(NULL);
    if (indev) {
        lv_indev_add_event_cb(indev, touch_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_indev_add_event_cb(indev, touch_cb, LV_EVENT_LONG_PRESSED, NULL);
    }
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
    splash_init(scr);
    init_status_line(scr);

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

    init_toast();
    init_input();

    Preferences prefs;
    prefs.begin("clawdmeter", true);
    carousel_on = prefs.getUChar("carousel", 1) != 0;
    prefs.end();
    carousel_ms = lv_tick_get();
}

void ui_update(const UsageData* data) {
    if (!data->valid) return;
    last_usage = *data;
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
        lv_label_set_text(lbl_weekly_label, "Weekly");   // back from an enterprise payload
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


// Advance the shared ✻ spinner on its own cadence; false when it isn't due yet.
static bool spinner_step(uint32_t now) {
    if (now - anim_last_ms < spinner_ms[anim_spinner_idx]) return false;
    anim_last_ms = now;
    anim_phase = (anim_phase + 1) % SPINNER_PHASES;
    anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                    : (SPINNER_PHASES - anim_phase);
    return true;
}


void ui_tick_anim(void) {
    const uint32_t now = lv_tick_get();
    tick_carousel(now);
    pace_watchdog(now);

    const ScreenDef* d = screen_def(current_screen);
    if (d) {
        // Data can go stale under a screen that's showing: move on.
        if (!d->available(now) && current_screen != SCREEN_FOCUS) {
            ui_show_screen(SCREEN_USAGE);
            return;
        }
        if (d->tick) d->tick(now);
        tick_status_line(now);
        return;
    }
    if (current_screen != SCREEN_USAGE) return;
    update_view_state();
    if (view_state == 1) splash_mini_tick();   // animate the sleeping creature on the idle screen

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

    tick_status_line(now);
}

static void apply_battery_visibility(void) {
    if (!battery_img) return;
    if (current_screen == SCREEN_SPLASH) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    else                                  lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
}

void ui_show_screen(screen_t screen) {
    teardown_screen();
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    if (screen == SCREEN_SPLASH)      splash_show();
    else if (screen == SCREEN_USAGE)  lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    else if (screen_def(screen))      build_screen(screen_def(screen));

    // The status line, its dot, the mascot and the battery stay above whatever
    // was just built.
    if (screen == SCREEN_SPLASH) {
        lv_obj_add_flag(lbl_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(lbl_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(lbl_status);
        lv_obj_move_foreground(status_dot);
    }
    if (!alert_active) {
        status_invalidate();
        status_set("", COL_ACCENT, nullptr);   // the new screen's text lands on the next tick
    }

    splash_mascot_set_visible(screen != SCREEN_SPLASH);
    if (logo_img) {
        if (screen == SCREEN_SPLASH) lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                          lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(logo_img);
    }
    if (battery_img) lv_obj_move_foreground(battery_img);

    current_screen = screen;
    apply_battery_visibility();
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_focus_finished(void) {
    carousel_ms = lv_tick_get();
    if (!alert_active) ui_show_screen(SCREEN_FOCUS);
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
    lv_obj_add_flag(status_dot, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_status, L.content_w);
    lv_obj_set_style_text_color(lbl_status, COL_ACCENT, 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, L.anim_y);
    lv_label_set_text(lbl_status, message);
    status_scrolling = true;
    status_invalidate();
    lv_timer_reset(alert_timer);
    lv_timer_resume(alert_timer);
}

void ui_hide_alert(void) {
    if (!alert_active) return;
    alert_active = false;
    splash_mascot_attention(false);
    status_invalidate();
    lv_timer_pause(alert_timer);
}
