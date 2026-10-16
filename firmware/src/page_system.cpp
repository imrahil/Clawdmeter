// System page (the app's {"k":"sys"} write, opt-in `system_stats = on`): the
// computer the app runs on. Four gauges (CPU, memory, CPU temperature, disk)
// titled with the host name; the status line carries the uptime.
#include "pages.h"
#include "pages_internal.h"
#include "ui.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

static const uint32_t SYS_STALE_MS = 5UL * 60 * 1000;   // the app sends every 30 s

static SystemData sys = {};
static bool       received = false;
static uint32_t   received_ms = 0;

enum { G_CPU, G_MEM, G_TEMP, G_DISK, G_COUNT };

static lv_obj_t* root = nullptr;
static lv_obj_t* title;
static lv_obj_t* arcs[G_COUNT];
static lv_obj_t* values[G_COUNT];
static lv_obj_t* captions[G_COUNT];
static lv_obj_t* footer;
static bool      tiny;                 // captions without the details

static lv_color_t temp_color(int c) {
    return c >= 80 ? THEME_RED : c >= 60 ? THEME_AMBER : THEME_GREEN;
}

static void set_gauge(int g, int pct, const char* value, lv_color_t color, const char* caption) {
    lv_arc_set_value(arcs[g], pct < 0 ? 0 : pct > 100 ? 100 : pct);
    lv_obj_set_style_arc_color(arcs[g], color, LV_PART_INDICATOR);
    lv_label_set_text(values[g], value);
    lv_label_set_text(captions[g], caption);
}

static void apply(void) {
    if (!root || !received) return;
    page_set_title(title, sys.host[0] ? sys.host : "System");
    char v[16], c[40];

    snprintf(v, sizeof(v), "%d%%", sys.cpu);
    if (tiny || !sys.load[0]) snprintf(c, sizeof(c), "CPU");
    else                      snprintf(c, sizeof(c), "CPU \xC2\xB7 load %s", sys.load);
    set_gauge(G_CPU, sys.cpu, v, page_pct_color(sys.cpu), c);

    snprintf(v, sizeof(v), "%d%%", sys.mem);
    if (tiny || !sys.mem_text[0]) snprintf(c, sizeof(c), "Memory");
    else                          snprintf(c, sizeof(c), "Memory \xC2\xB7 %s", sys.mem_text);
    set_gauge(G_MEM, sys.mem, v, page_pct_color(sys.mem), c);

    if (sys.temp >= 0) snprintf(v, sizeof(v), "%d\xC2\xB0" "C", sys.temp);
    else               snprintf(v, sizeof(v), "--");
    set_gauge(G_TEMP, sys.temp, v, temp_color(sys.temp), "CPU temp");

    snprintf(v, sizeof(v), "%d%%", sys.disk);
    if (tiny || !sys.disk_text[0]) snprintf(c, sizeof(c), "Disk");
    else                           snprintf(c, sizeof(c), "Disk \xC2\xB7 %s", sys.disk_text);
    set_gauge(G_DISK, sys.disk, v, page_pct_color(sys.disk), c);

    char up[24] = "";
    if (sys.uptime[0]) snprintf(up, sizeof(up), "Up %s", sys.uptime);
    page_set_footer(footer, up);
}

static void build(lv_obj_t* r) {
    root = r;
    const Layout& L = ui_layout();
    const PageArea A = page_area(L);
    title = page_make_title(root, "System");

    const lv_font_t* f_head = page_with_symbols(L.reset_font);
    const lv_font_t* f_small = page_with_symbols(page_smaller(L.reset_font, 2));
    const int gap = page_gap(L);
    const int tile_w = (A.w - gap) / 2;
    const int tile_h = (A.h - gap) / 2;
    const int pad = L.pad_y < 10 ? L.pad_y : 10;
    const int inner_w = tile_w - 2 * pad;
    const int cap_h = lv_font_get_line_height(f_small);
    int d = tile_h - 2 * pad - cap_h - 2;
    if (d > inner_w) d = inner_w;
    tiny = tile_w < 180;

    for (int g = 0; g < G_COUNT; g++) {
        lv_obj_t* t = page_panel(root, A.x + (g % 2) * (tile_w + gap), A.y + (g / 2) * (tile_h + gap),
                                 tile_w, tile_h);
        lv_obj_set_style_pad_all(t, pad, 0);
        // 270° gauge, open at the bottom.
        arcs[g] = page_arc(t, d, LV_MAX(4, LV_MIN(10, d / 12)), 135, 270, 100);
        lv_obj_align(arcs[g], LV_ALIGN_TOP_MID, 0, 0);
        values[g] = page_label(arcs[g], d >= 110 ? f_head : page_with_symbols(page_smaller(L.reset_font, 1)),
                               THEME_TEXT, "-");
        lv_obj_align(values[g], LV_ALIGN_CENTER, 0, 0);
        captions[g] = page_label(t, f_small, THEME_DIM, "");
        lv_label_set_long_mode(captions[g], LV_LABEL_LONG_DOT);
        lv_obj_set_size(captions[g], inner_w, cap_h);   // one line, cut with "…"
        lv_obj_set_style_text_align(captions[g], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(captions[g], LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    footer = page_make_footer(root);
    apply();
}

static void destroy(void) { root = nullptr; }

static bool available(uint32_t now) { return received && now - received_ms < SYS_STALE_MS; }

void pages_update_system(const SystemData* d) {
    sys = *d;
    received = true;
    received_ms = millis();
    apply();
}

const PageDef page_system = {
    "system", build, destroy, available, nullptr, nullptr, nullptr, nullptr,
};
