#include "pages.h"
#include "pages_internal.h"
#include "ui.h"
#include "clawd_director.h"
#include "idle.h"
#include <Arduino.h>
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

static const PageDef* const PAGES[PAGE_COUNT] = { nullptr, &page_weather, &page_focus };

static PageId    current = PAGE_METERS;
static lv_obj_t* root = nullptr;
static uint32_t  last_input = 0;

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
    const lv_font_t* fonts[] = { L.title_font[0], &font_tiempos_34 };
    const lv_font_t* f = &font_tiempos_34;
    for (const lv_font_t* c : fonts) {
        if (c && layout_text_width(c, text) <= room) { f = c; break; }
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
    Serial.printf("pages: %s\n", PAGES[page]->name);
}

static void step(int dir) {
    const uint32_t now = millis();
    int p = current;
    for (int i = 0; i < PAGE_COUNT; i++) {
        p = (p + dir + PAGE_COUNT) % PAGE_COUNT;
        if (p == PAGE_METERS || PAGES[p]->available(now)) break;
    }
    pages_show((PageId)p);
}

void pages_next(void) { step(+1); }
void pages_prev(void) { step(-1); }
PageId pages_current(void) { return current; }

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
    if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) { if (d->tap) d->tap(); }
    else if (d->long_press)                              d->long_press();
}

void pages_init(void) {
    lv_indev_t* indev = lv_indev_get_next(NULL);
    if (indev) {
        lv_indev_add_event_cb(indev, touch_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_indev_add_event_cb(indev, touch_cb, LV_EVENT_LONG_PRESSED, NULL);
    }
}

void pages_tick(void) {
    if (current == PAGE_METERS) return;
    const uint32_t now = millis();
    const PageDef* d = PAGES[current];
    const bool keep = d->keep_open && d->keep_open(now);
    if (!d->available(now) || (!keep && now - last_input >= PAGE_IDLE_MS)) {
        pages_show(PAGE_METERS);
        return;
    }
    if (d->tick) d->tick(now);
}
