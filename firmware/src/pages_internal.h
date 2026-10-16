#pragma once
// Shared by pages.cpp and the page files (page_weather.cpp, page_focus.cpp).
#include <lvgl.h>
#include <stdint.h>
#include "layout.h"
#include "theme.h"
#include "data.h"
#include <stddef.h>

struct PageDef {
    const char* name;
    void (*build)(lv_obj_t* root);      // create widgets under root and apply the data
    void (*destroy)(void);              // forget widget pointers; root is deleted after
    bool (*available)(uint32_t now);    // reachable with the buttons
    bool (*keep_open)(uint32_t now);    // NULL = closes after PAGE_IDLE_MS like the rest
    void (*tick)(uint32_t now);         // NULL = nothing moves
    void (*tap)(void);                  // NULL = a tap turns the carousel on / off
    void (*long_press)(void);           // NULL = nothing
    bool (*in_carousel)(uint32_t now);  // NULL = when available
};

extern const PageDef page_pace;
extern const PageDef page_activity;
extern const PageDef page_focus;
extern const PageDef page_weather;
extern const PageDef page_system;
extern const PageDef page_stocks;

// The app re-sends its feeds every 30 s to 15 min; a page whose data is
// older than this is gone from the buttons and the carousel.
static const uint32_t PAGE_FEED_STALE_MS = 3UL * 60 * 60 * 1000;

// The area the meters' two cards cover: pages lay out inside it, so they
// clear the header and the status line on every panel (round ones too).
struct PageArea { int16_t x, y, w, h; };
PageArea page_area(const Layout& L);

// Title in the meters' title slot, cut to fit between the corner Clawd and
// the battery. NULL when the layout has no title room.
lv_obj_t* page_make_title(lv_obj_t* parent, const char* text);
void      page_set_title(lv_obj_t* title, const char* text);
// The status line's slot: "<glyph> text…" in Clawd orange, one line.
lv_obj_t* page_make_footer(lv_obj_t* parent);
void      page_set_footer(lv_obj_t* footer, const char* text);   // "" hides it

lv_obj_t* page_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text);
lv_obj_t* page_panel(lv_obj_t* parent, int x, int y, int w, int h);
lv_obj_t* page_dot(lv_obj_t* parent, int px, lv_color_t color);
lv_obj_t* page_arc(lv_obj_t* parent, int d, int width, int rotation, int sweep, int range_max);
// A line (lv_line) with rounded caps; `dash` > 0 dashes it (LVGL dashes only
// horizontal and vertical lines).
lv_obj_t* page_line(lv_obj_t* parent, lv_color_t color, int width, int dash);
// Green under 50 %, amber to 80 %, red above: the meters' bar colours.
lv_color_t page_pct_color(int pct);

// Font helpers. The text fonts are ASCII-only; with_symbols() returns a copy
// with the same-size symbols font (° · € £ ¥ −) as its LVGL fallback.
// smaller() steps down the Styrene sizes (48 28 24 20 16 14 12).
const lv_font_t* page_with_symbols(const lv_font_t* f);
const lv_font_t* page_smaller(const lv_font_t* f, int steps);

void page_init_icon(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data);   // RGB565A8

// Card styling shared with the meters: a page_panel() padded like a card.
lv_obj_t* page_card(lv_obj_t* parent, int x, int y, int w, int h);
// The gap between the meters' two cards.
int page_gap(const Layout& L);

// The latest usage payload (for pages that show the live session numbers);
// NULL before the first one.
const UsageData* page_usage(void);
uint32_t         page_usage_gen(void);   // bumps with every usage payload
// "Resets in 2h 10m"; "" when unknown.
void page_reset_text(int mins, char* buf, size_t len);

// Something on the page was touched or pressed (keeps it open).
void pages_note_input(void);
