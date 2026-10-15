// System screen (daemon-fed {"k":"sys"}, opt-in): the host the daemon runs
// on. Four gauges — CPU, memory, CPU temperature, disk — titled with the
// host name; the status line carries the uptime.
#include "ui.h"
#include "ui_internal.h"
#include <stdio.h>
#include <string.h>

static const uint32_t SYS_STALE_MS = 5UL * 60 * 1000;   // daemon sends every 30 s

static SystemData sys = {};
static bool       sys_received = false;
static uint32_t   sys_ms = 0;

enum { G_CPU, G_MEM, G_TEMP, G_DISK, G_COUNT };

static lv_obj_t* root = nullptr;
static lv_obj_t* title;
static lv_obj_t* arcs[G_COUNT];
static lv_obj_t* values[G_COUNT];
static lv_obj_t* captions[G_COUNT];

static lv_color_t temp_color(int c) {
    return c >= 80 ? COL_RED : c >= 60 ? COL_AMBER : COL_GREEN;
}

static void set_gauge(int g, int pct, const char* value, lv_color_t color, const char* caption) {
    lv_arc_set_value(arcs[g], pct < 0 ? 0 : pct > 100 ? 100 : pct);
    lv_obj_set_style_arc_color(arcs[g], color, LV_PART_INDICATOR);
    lv_label_set_text(values[g], value);
    lv_label_set_text(captions[g], caption);
}

static void sys_apply(void) {
    if (!root || !sys_received) return;
    set_screen_title(title, sys.host[0] ? sys.host : "System");
    char v[16], c[40];
    const bool tiny = L.small_icons;

    snprintf(v, sizeof(v), "%d%%", sys.cpu);
    snprintf(c, sizeof(c), tiny ? "CPU" : "CPU \xC2\xB7 load %s", sys.load);
    set_gauge(G_CPU, sys.cpu, v, pct_color(sys.cpu), c);

    snprintf(v, sizeof(v), "%d%%", sys.mem);
    snprintf(c, sizeof(c), tiny ? "Memory" : "Memory \xC2\xB7 %s", sys.mem_text);
    set_gauge(G_MEM, sys.mem, v, pct_color(sys.mem), c);

    if (sys.temp >= 0) snprintf(v, sizeof(v), "%d\xC2\xB0" "C", sys.temp);
    else               snprintf(v, sizeof(v), "--");
    set_gauge(G_TEMP, sys.temp, v, temp_color(sys.temp), "CPU temp");

    snprintf(v, sizeof(v), "%d%%", sys.disk);
    snprintf(c, sizeof(c), tiny ? "Disk" : "Disk \xC2\xB7 %s", sys.disk_text);
    set_gauge(G_DISK, sys.disk, v, pct_color(sys.disk), c);
}

static void sys_build(lv_obj_t* r) {
    root = r;
    title = make_screen_title(root, "System");

    const int gap = L.usage_panel_gap;
    const int tile_w = (L.content_w - gap) / 2;
    const int tile_h = (content_bottom() - L.content_y - gap) / 2;
    const int inner_w = panel_inner_w(tile_w);
    const int cap_h = lv_font_get_line_height(L.f_small);
    int d = panel_inner_h(tile_h) - cap_h - 4;
    if (d > inner_w) d = inner_w;

    for (int g = 0; g < G_COUNT; g++) {
        lv_obj_t* t = make_panel(root, L.margin + (g % 2) * (tile_w + gap),
                                 L.content_y + (g / 2) * (tile_h + gap), tile_w, tile_h);
        // 270° gauge, open at the bottom.
        arcs[g] = make_arc(t, d, 135, 270, 100);
        lv_obj_align(arcs[g], LV_ALIGN_TOP_MID, 0, 0);
        values[g] = make_label(arcs[g], L.f_head, COL_TEXT, "-");
        lv_obj_align(values[g], LV_ALIGN_CENTER, 0, 0);
        captions[g] = make_label(t, L.f_small, COL_DIM, "");
        lv_label_set_long_mode(captions[g], LV_LABEL_LONG_DOT);
        lv_obj_set_size(captions[g], inner_w, cap_h);   // one line, cut with "…"
        lv_obj_set_style_text_align(captions[g], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(captions[g], LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    sys_apply();
}

static void sys_destroy(void) {
    root = nullptr;
}

static bool sys_available(uint32_t now) {
    return sys_received && now - sys_ms < SYS_STALE_MS;
}

static const char* sys_status(uint32_t now) {
    (void)now;
    static char buf[24];
    if (!sys.uptime[0]) return "";
    snprintf(buf, sizeof(buf), "Up %s", sys.uptime);
    return buf;
}

void ui_update_system(const SystemData* d) {
    sys = *d;
    sys_received = true;
    sys_ms = lv_tick_get();
    sys_apply();
}

const ScreenDef system_screen = {
    sys_build, sys_destroy, sys_available, nullptr,
    sys_status, nullptr, nullptr, nullptr,
};
