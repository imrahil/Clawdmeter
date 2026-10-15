#pragma once
// Shared between ui.cpp and the per-screen files (ui_<screen>.cpp). Not for
// code outside the UI: main.cpp talks to ui.h only.
#include <lvgl.h>
#include <stdint.h>
#include "data.h"
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

// Layout values computed from the active board's geometry. Populated once
// in ui_init() and treated as const for the rest of the program. Adding a
// new display size means extending compute_layout() with another
// breakpoint — never editing the screen-builder functions.
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
    int16_t wx_extra_y;              // wind / UV / air quality line
    int16_t wx_fc_y, wx_fc_h;        // forecast tiles
    int16_t wx_fc_icon_px;           // forecast icon (drawn from the 48px art)
    int16_t wx_fc_pad_y;             // forecast tile top/bottom padding
    const lv_font_t* wx_temp_font;
    const lv_font_t* wx_cond_font;
    const lv_font_t* wx_detail_font;
    const lv_font_t* wx_extra_font;
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

    // Shared by the Pace / Activity / Focus / System screens
    const lv_font_t* f_head;         // panel headline, stat values
    const lv_font_t* f_body;         // row text (with ° · fallbacks)
    const lv_font_t* f_small;        // captions, day labels (with ° · fallbacks)
    const lv_font_t* f_clock;        // focus countdown
    int16_t mini_cell;               // px per art cell for in-screen creatures
    int16_t arc_w;                   // gauge / ring stroke
    int16_t dot_px;                  // session state dot

    // Everything below the header shares Usage's animated status line at the
    // bottom; content stops this far above the bottom edge to clear it.
    int16_t footer_reserve;
};
extern Layout L;

// ---- Widget helpers (ui.cpp) ----
lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h);
lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h);
lv_obj_t* make_pill(lv_obj_t* parent, const char* text);
lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text);
lv_obj_t* make_dot(lv_obj_t* parent, int px, lv_color_t color);
// Display-only arc (no knob, not clickable): `sweep` degrees starting at
// `rotation` (0 = 3 o'clock, clockwise), range 0..range_max.
lv_obj_t* make_arc(lv_obj_t* parent, int d, int rotation, int sweep, int range_max);
void      init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data);
// Header title in the Usage title's slot; set_screen_title() fits long text
// between the corner mascot and the battery.
lv_obj_t* make_screen_title(lv_obj_t* parent, const char* text);
void      set_screen_title(lv_obj_t* t, const char* text);
lv_color_t pct_color(float pct);
void       format_reset_time(int mins, char* buf, size_t len);   // "Resets in 2h 10m"
// Inner size of a make_panel() of the given outer size.
static inline int panel_inner_w(int w) { return w - 2 * L.panel_pad_x; }
static inline int panel_inner_h(int h) { return h - 2 * L.panel_pad_y; }
// Content area between the header and the status line.
static inline int content_bottom(void) { return L.scr_h - L.footer_reserve; }

// Latest usage payload (for screens that show the live session numbers).
const UsageData* ui_last_usage(void);
// Pace data stale → the splash stops racing (ui_pace.cpp; called every loop).
void pace_watchdog(uint32_t now);

// ---- Screen modules ----
// Non-usage screens are built when shown and deleted when left, so LVGL's
// pool only ever holds one of them (PSRAM-free boards can't grow the pool).
// Each module keeps its data in statics and re-applies it on build.
struct ScreenDef {
    void (*build)(lv_obj_t* root);           // create widgets under root, apply data
    void (*destroy)(void);                   // forget widget pointers; root is deleted after
    bool (*available)(uint32_t now);         // reachable by +/- (has fresh data)
    bool (*in_carousel)(uint32_t now);       // NULL = same as available
    const char* (*status_text)(uint32_t now);// the status line's fact; "" = blank
    void (*tick)(uint32_t now);              // NULL = nothing to animate
    void (*tap)(void);                       // NULL = tap toggles the carousel
    void (*long_press)(void);                // NULL = nothing
};

extern const ScreenDef weather_screen;
extern const ScreenDef stocks_screen;
extern const ScreenDef pace_screen;
extern const ScreenDef activity_screen;
extern const ScreenDef focus_screen;
extern const ScreenDef system_screen;

// Weather / stocks / system data goes stale after this long without a payload.
static const uint32_t EXTRA_STALE_MS = 3UL * 60 * 60 * 1000;
