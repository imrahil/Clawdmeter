#pragma once
#include <lvgl.h>

// Design tokens — single source of truth for UI colors. Anthropic-inspired
// dark palette, AMOLED-friendly (true black bg).
#define THEME_BG       lv_color_hex(0x000000)   // screen background
#define THEME_PANEL    lv_color_hex(0x1f1f1e)   // card/zone fill
#define THEME_TEXT     lv_color_hex(0xfaf9f5)   // primary text
#define THEME_DIM      lv_color_hex(0xb0aea5)   // secondary text
#define THEME_ACCENT   lv_color_hex(0xd97757)   // brand terra-cotta
#define THEME_GREEN    lv_color_hex(0x788c5d)
#define THEME_AMBER    lv_color_hex(0xd97757)
#define THEME_RED      lv_color_hex(0xc0392b)
#define THEME_BAR_BG   lv_color_hex(0x2a2a28)   // unfilled bar track
// Gain / loss as small text on THEME_PANEL: lighter steps of GREEN / RED so
// they clear 4.5:1 (the bar fills sit at 4.5 and 3.0). LOSS stays clearly
// redder than ACCENT, which the pages' sparklines are drawn in.
#define THEME_GAIN     lv_color_hex(0x93a872)   // 6.3:1 on panel
#define THEME_LOSS     lv_color_hex(0xeb5a4c)   // 4.8:1 on panel
