#include "pages.h"
#include "pages_internal.h"
#include "ui.h"
#include "clawd_director.h"
#include "idle.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_styrene_12);
LV_FONT_DECLARE(font_sym_12);
LV_FONT_DECLARE(font_sym_14);
LV_FONT_DECLARE(font_sym_16);
LV_FONT_DECLARE(font_sym_20);
LV_FONT_DECLARE(font_sym_24);
LV_FONT_DECLARE(font_sym_28);
LV_FONT_DECLARE(font_sym_48);

// A page left alone this long goes back to the meters (Focus not while it
// runs): the meters are home, and Clawd only lives fully there.
#define PAGE_IDLE_MS (2UL * 60 * 1000)

static const PageDef* const PAGES[PAGE_COUNT] = {
    nullptr, &page_pace, &page_activity, &page_focus, &page_system, &page_weather, &page_stocks,
};

static PageId    current = PAGE_METERS;
static lv_obj_t* root = nullptr;
static uint32_t  last_input = 0;
static bool      carousel = false;
static uint32_t  carousel_at = 0;     // when it last stepped (or was turned on)

// ─── Helpers for the page files ──────────────────────────────────────────────

PageArea page_area(const Layout& L) {
    const int top = L.card[0].y;
    const int bottom = L.card[1].y + L.card[1].h;
    return { L.card[0].x, (int16_t)top, L.card[0].w, (int16_t)(bottom - top) };
}

lv_obj_t* page_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t* page_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 8, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return p;
}

lv_obj_t* page_dot(lv_obj_t* parent, int px, lv_color_t color) {
    lv_obj_t* d = page_panel(parent, 0, 0, px, px);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, color, 0);
    return d;
}

lv_obj_t* page_arc(lv_obj_t* parent, int d, int width, int rotation, int sweep, int range_max) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_set_size(a, d, d);
    lv_arc_set_rotation(a, rotation);
    lv_arc_set_bg_angles(a, 0, sweep);
    lv_arc_set_range(a, 0, range_max);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, THEME_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, THEME_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

lv_obj_t* page_line(lv_obj_t* parent, lv_color_t color, int width, int dash) {
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_rounded(l, dash <= 0, 0);
    if (dash > 0) {
        lv_obj_set_style_line_dash_width(l, dash, 0);
        lv_obj_set_style_line_dash_gap(l, dash, 0);
    }
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

lv_obj_t* page_card(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* c = page_panel(parent, x, y, w, h);
    const Layout& L = ui_layout();
    lv_obj_set_style_pad_hor(c, L.pad_x, 0);
    lv_obj_set_style_pad_ver(c, L.pad_y, 0);
    return c;
}

int page_gap(const Layout& L) {
    const int g = L.card[1].y - (L.card[0].y + L.card[0].h);
    return g > 4 ? g : 4;
}

static UsageData last_usage;
static bool      have_usage = false;
static uint32_t  usage_gen = 0;

void pages_update_usage(const UsageData* d) {
    if (!d->valid || !d->ok) return;
    last_usage = *d;
    have_usage = true;
    usage_gen++;
}

uint32_t page_usage_gen(void) { return usage_gen; }

const UsageData* page_usage(void) { return have_usage ? &last_usage : nullptr; }

void page_reset_text(int mins, char* buf, size_t len) {
    if (mins < 0)         snprintf(buf, len, "%s", "");
    else if (mins < 60)   snprintf(buf, len, "Resets in %dm", mins);
    else if (mins < 1440) snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    else                  snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
}

lv_color_t page_pct_color(int pct) {
    return pct >= 80 ? THEME_RED : pct >= 50 ? THEME_AMBER : THEME_GREEN;
}

void page_init_icon(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static const lv_font_t* const STYRENE[] = {
    &font_styrene_48, &font_styrene_28, &font_styrene_24, &font_styrene_20,
    &font_styrene_16, &font_styrene_14, &font_styrene_12,
};
static const lv_font_t* const SYMBOLS[] = {
    &font_sym_48, &font_sym_28, &font_sym_24, &font_sym_20, &font_sym_16, &font_sym_14, &font_sym_12,
};
#define N_STYRENE (int)(sizeof(STYRENE) / sizeof(STYRENE[0]))

static int styrene_index(const lv_font_t* f) {
    for (int i = 0; i < N_STYRENE; i++)
        if (STYRENE[i] == f || (f && STYRENE[i]->dsc == f->dsc)) return i;
    return -1;
}

const lv_font_t* page_smaller(const lv_font_t* f, int steps) {
    const int i = styrene_index(f);
    if (i < 0) return f;
    const int j = i + steps < N_STYRENE ? i + steps : N_STYRENE - 1;
    return STYRENE[j];
}

const lv_font_t* page_with_symbols(const lv_font_t* f) {
    static lv_font_t copies[N_STYRENE];
    static bool made[N_STYRENE];
    const int i = styrene_index(f);
    if (i < 0) return f;
    if (!made[i]) {
        copies[i] = *STYRENE[i];
        copies[i].fallback = SYMBOLS[i];
        made[i] = true;
    }
    return &copies[i];
}

// The title slot: between the corner Clawd's slot and the battery, centred
// on the meters' title line. The meters' title font when it fits, then the
// smaller Tiempos, then cut with "…".
void page_set_title(lv_obj_t* t, const char* text) {
    if (!t) return;
    const Layout& L = ui_layout();
    const int logo_w = L.small_icons ? LAYOUT_LOGO_W_SMALL : LAYOUT_LOGO_W;
    const int left = L.logo_x + logo_w + 8;
    const int right = L.batt_x - 8;
    const int room = 2 * LV_MIN(L.title_cx - left, right - L.title_cx);
    // Candidates, largest first: the meters' title font, the small Tiempos
    // (only if it is smaller), then Styrene sizes below it (a long name in
    // the sans reads better than one cut short).
    const lv_font_t* base = L.title_font[0];
    const lv_font_t* fonts[8] = { base };
    int n = 1;
    if (lv_font_get_line_height(&font_tiempos_34) < lv_font_get_line_height(base)) fonts[n++] = &font_tiempos_34;
    for (const lv_font_t* c : STYRENE) {
        if (n < 8 && c != base && lv_font_get_line_height(c) < lv_font_get_line_height(base)
            && lv_font_get_line_height(c) >= lv_font_get_line_height(base) / 2)
            fonts[n++] = c;
    }
    const lv_font_t* f = fonts[n - 1];
    for (int i = 0; i < n; i++) {
        if (layout_text_width(fonts[i], text) <= room) { f = fonts[i]; break; }
    }
    lv_obj_set_style_text_font(t, f, 0);
    lv_label_set_text(t, text);
    const int w = LV_MIN((int)layout_text_width(f, text), room);
    lv_obj_set_size(t, w, lv_font_get_line_height(f));
    const int y = f == L.title_font[0] ? L.title_y[0]
                : L.title_y[0] + (lv_font_get_line_height(L.title_font[0]) - lv_font_get_line_height(f)) / 2;
    lv_obj_set_pos(t, L.title_cx - w / 2, y);
}

lv_obj_t* page_make_title(lv_obj_t* parent, const char* text) {
    if (!ui_layout().title_font[0]) return nullptr;   // no title room on this panel
    lv_obj_t* t = page_label(parent, ui_layout().title_font[0], THEME_TEXT, "");
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    page_set_title(t, text);
    return t;
}

lv_obj_t* page_make_footer(lv_obj_t* parent) {
    const Layout& L = ui_layout();
    lv_obj_t* f = page_label(parent, L.status_font, THEME_ACCENT, "");
    lv_label_set_long_mode(f, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(f, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(f, L.status_w, lv_font_get_line_height(L.status_font));
    lv_obj_align(f, LV_ALIGN_BOTTOM_MID, 0, L.status_y);
    return f;
}

void page_set_footer(lv_obj_t* f, const char* text) {
    if (!f) return;
    char buf[64];
    if (text[0]) snprintf(buf, sizeof(buf), "\xE2\x9C\xBB %s\xE2\x80\xA6", text);   // ✻ text…
    else         buf[0] = '\0';
    if (strcmp(lv_label_get_text(f), buf) != 0) lv_label_set_text(f, buf);
}

void pages_note_input(void) { last_input = millis(); }

// ─── Navigation ──────────────────────────────────────────────────────────────

static void close_page(void) {
    if (current == PAGE_METERS) return;
    PAGES[current]->destroy();
    lv_obj_delete(root);
    root = nullptr;
    current = PAGE_METERS;
    ui_set_meters_hidden(false);
    director_set_covered(false);
}

void pages_show(PageId page) {
    if (page == current) return;
    if (page != PAGE_METERS && director_alerting()) return;   // the alert owns the meters
    close_page();
    if (page == PAGE_METERS) {
        Serial.println("pages: meters");
        return;
    }
    director_set_covered(true);       // Clawd to his seat first: one Clawd, never under a page
    ui_set_meters_hidden(true);
    root = lv_obj_create(ui_page_layer());
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    current = page;
    last_input = millis();
    PAGES[page]->build(root);
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    // The C6 has no PSRAM: the pool's peak is what a page must stay under.
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    Serial.printf("pages: %s (LVGL pool %lu of %lu KB used, peak %lu KB)\n", PAGES[page]->name,
                  (unsigned long)((mon.total_size - mon.free_size) / 1024),
                  (unsigned long)(mon.total_size / 1024), (unsigned long)(mon.max_used / 1024));
#else
    Serial.printf("pages: %s\n", PAGES[page]->name);
#endif
}

static bool in_carousel(int p, uint32_t now) {
    if (p == PAGE_METERS) return true;
    const PageDef* d = PAGES[p];
    if (!d->available(now)) return false;
    return d->in_carousel ? d->in_carousel(now) : true;
}

static void step(int dir, bool by_carousel) {
    const uint32_t now = millis();
    int p = current;
    for (int i = 0; i < PAGE_COUNT; i++) {
        p = (p + dir + PAGE_COUNT) % PAGE_COUNT;
        if (p == PAGE_METERS) break;
        if (by_carousel ? in_carousel(p, now) : PAGES[p]->available(now)) break;
    }
    carousel_at = now;            // a button press restarts the carousel's count too
    pages_show((PageId)p);
}

void pages_next(void) { step(+1, false); }
void pages_prev(void) { step(-1, false); }
PageId pages_current(void) { return current; }

const char* pages_name(PageId page) {
    if (page == PAGE_METERS) return "meters";
    return page < PAGE_COUNT ? PAGES[page]->name : "?";
}

PageId pages_by_name(const char* name) {
    if (strcmp(name, "meters") == 0) return PAGE_METERS;
    for (int p = 1; p < PAGE_COUNT; p++)
        if (strcmp(name, PAGES[p]->name) == 0) return (PageId)p;
    return PAGE_COUNT;
}

// ─── Carousel ────────────────────────────────────────────────────────────────

static void toast_done(lv_timer_t* t) {
    lv_obj_delete((lv_obj_t*)lv_timer_get_user_data(t));
}

// A short note in the middle of the panel, on the top layer (it outlives a
// page change).
static void toast(const char* text) {
    const Layout& L = ui_layout();
    lv_obj_t* box = page_label(lv_layer_top(), page_with_symbols(L.reset_font), THEME_TEXT, text);
    lv_obj_set_style_bg_color(box, THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_pad_hor(box, 16, 0);
    lv_obj_set_style_pad_ver(box, 10, 0);
    lv_obj_set_style_border_color(box, THEME_ACCENT, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_timer_t* t = lv_timer_create(toast_done, 1500, box);
    lv_timer_set_repeat_count(t, 1);
}

void pages_set_carousel(bool on) {
    if (on != carousel) {
        Preferences prefs;
        prefs.begin("clawdmeter", false);
        prefs.putUChar("carousel", on ? 1 : 0);
        prefs.end();
    }
    carousel = on;
    carousel_at = millis();
    Serial.printf("pages: carousel %s\n", on ? "on" : "off");
    toast(on ? "Carousel on" : "Carousel off");
}

bool pages_carousel(void) { return carousel; }

static void carousel_tick(uint32_t now) {
    if (!carousel || now - carousel_at < PAGE_CAROUSEL_MS) return;
    if (director_alerting() || idle_is_asleep()) { carousel_at = now; return; }
    // On the meters, wait for Clawd to be back in his seat: a page never
    // cuts a show, the face or a trip short.
    if (current == PAGE_METERS && !director_settled()) return;
    step(+1, true);
}

void pages_focus_finished(void) {
    if (director_alerting()) return;
    pages_show(PAGE_FOCUS);
    last_input = millis();
}

// ─── Touch ───────────────────────────────────────────────────────────────────
// The director hears every press too (it notes you're here, and ignores the
// tap while covered); a page acts on the click.
static void touch_cb(lv_event_t* e) {
    if (current == PAGE_METERS) return;
    pages_note_input();
    const PageDef* d = PAGES[current];
    if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
        if (d->tap) d->tap();
        else        pages_set_carousel(!carousel);
    } else if (d->long_press) {
        d->long_press();
    }
}

void pages_init(void) {
    Preferences prefs;
    prefs.begin("clawdmeter", true);
    carousel = prefs.getUChar("carousel", 0) != 0;
    prefs.end();
    carousel_at = millis();
    Serial.printf("pages: carousel %s\n", carousel ? "on" : "off");
    lv_indev_t* indev = lv_indev_get_next(NULL);
    if (indev) {
        lv_indev_add_event_cb(indev, touch_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_indev_add_event_cb(indev, touch_cb, LV_EVENT_LONG_PRESSED, NULL);
    }
}

void pages_tick(void) {
    const uint32_t now = millis();
    carousel_tick(now);
    if (current == PAGE_METERS) return;
    const PageDef* d = PAGES[current];
    const bool keep = carousel || (d->keep_open && d->keep_open(now));
    if (!d->available(now) || (!keep && now - last_input >= PAGE_IDLE_MS)) {
        pages_show(PAGE_METERS);
        return;
    }
    if (d->tick) d->tick(now);
}
