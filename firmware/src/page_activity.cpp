// Activity page (the app's {"k":"a"} write): today's Claude Code totals and
// the live sessions, from the transcripts on the computer.
//   tiles — tokens today, their API-equivalent value, sessions today
//   rows  — project, state dot (Clawd orange breathing = working, red = needs
//           you, grey = idle) and what it's doing / how long it's been idle
// The status line counts who needs you or is working.
#include "pages.h"
#include "pages_internal.h"
#include "ui.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

static const uint32_t ACT_STALE_MS = 5UL * 60 * 1000;   // the app sends every 30 s
#define PULSE_MS 600

static ActivityData act = {};
static bool     received = false;
static uint32_t received_ms = 0;

struct ActRow {
    lv_obj_t* panel;
    lv_obj_t* dot;
    lv_obj_t* name;
    lv_obj_t* label;
};

static lv_obj_t* root = nullptr;
static lv_obj_t* tile_val[3];
static int       n_tiles;
static ActRow    rows[ACT_ROWS_MAX];
static int       n_slots;              // rows that fit on this panel
static lv_obj_t* empty_lbl;
static lv_obj_t* footer;
static bool      pulse_on = true;
static uint32_t  pulse_ms = 0;

static void format_tokens(char* buf, size_t len, double n) {
    if (n < 1e3)       snprintf(buf, len, "%.0f", n);
    else if (n < 1e6)  snprintf(buf, len, "%.1fk", n / 1e3);
    else if (n < 1e9)  snprintf(buf, len, "%.1fM", n / 1e6);
    else               snprintf(buf, len, "%.2fB", n / 1e9);
}

static void format_cost(char* buf, size_t len, long cents) {
    if (cents < 100000) snprintf(buf, len, "$%ld.%02ld", cents / 100, cents % 100);
    else                snprintf(buf, len, "$%.1fk", cents / 100000.0);
}

static const char* status_text(void) {
    static char buf[32];
    int working = 0, waiting = 0;
    for (int i = 0; i < act.n_rows; i++) {
        if (act.rows[i].state == SESSION_WAITING) waiting++;
        else if (act.rows[i].state == SESSION_WORKING) working++;
    }
    if (waiting)      snprintf(buf, sizeof(buf), "%d needs you", waiting);
    else if (working) snprintf(buf, sizeof(buf), "%d working", working);
    else              snprintf(buf, sizeof(buf), "All quiet");
    return buf;
}

static void apply(void) {
    if (!root || !received) return;
    char buf[24];
    format_tokens(buf, sizeof(buf), act.tokens);
    lv_label_set_text(tile_val[0], buf);
    format_cost(buf, sizeof(buf), act.cost_cents);
    lv_label_set_text(tile_val[1], buf);
    snprintf(buf, sizeof(buf), "%d", act.sessions);
    if (tile_val[2]) lv_label_set_text(tile_val[2], buf);

    for (int i = 0; i < n_slots; i++) {
        ActRow* r = &rows[i];
        if (i >= act.n_rows) {
            lv_obj_add_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const ActivityRow* a = &act.rows[i];
        lv_label_set_text(r->name, a->name);
        lv_color_t dot = THEME_DIM, text = THEME_DIM;
        if (a->state == SESSION_WAITING) {
            dot = THEME_RED;
            text = THEME_LOSS;
            lv_label_set_text(r->label, a->label[0] ? a->label : "needs you");
        } else if (a->state == SESSION_WORKING) {
            dot = text = THEME_ACCENT;
            lv_label_set_text(r->label, a->label);
        } else if (a->age_min < 1) {
            lv_label_set_text(r->label, "idle");
        } else {
            snprintf(buf, sizeof(buf), "idle %dm", a->age_min);
            lv_label_set_text(r->label, buf);
        }
        lv_obj_set_style_bg_color(r->dot, dot, 0);
        lv_obj_set_style_bg_opa(r->dot, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(r->label, text, 0);
        lv_obj_clear_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (act.n_rows == 0) lv_obj_clear_flag(empty_lbl, LV_OBJ_FLAG_HIDDEN);
    else                 lv_obj_add_flag(empty_lbl, LV_OBJ_FLAG_HIDDEN);
    page_set_footer(footer, status_text());
}

static void build(lv_obj_t* r) {
    root = r;
    const Layout& L = ui_layout();
    const PageArea A = page_area(L);
    page_make_title(root, "Activity");

    const lv_font_t* f_head = page_with_symbols(L.reset_font);
    const lv_font_t* f_body = page_with_symbols(page_smaller(L.reset_font, 1));
    const lv_font_t* f_small = page_with_symbols(page_smaller(L.reset_font, 2));
    const int gap = page_gap(L);
    static const char* const CAPTIONS[3]      = { "Tokens today", "API value", "Sessions" };
    static const char* const CAPTIONS_TINY[3] = { "Tokens", "Value", "Sessions" };

    // Three tiles where a wide reading ("$123.45") fits one; on a narrow
    // panel two (tokens and value), the session count goes.
    const int tile_hpad = L.pad_x < 12 ? L.pad_x : 12;
    const lv_font_t* f_min = page_with_symbols(page_smaller(f_small, 1));
    n_tiles = layout_text_width(f_min, "$123.45") <= (A.w - 2 * gap) / 3 - 2 * tile_hpad ? 3 : 2;
    const int tile_w = (A.w - (n_tiles - 1) * gap) / n_tiles;
    const int tile_pad = L.pad_y < 10 ? L.pad_y : 10;
    const int inner = tile_w - 2 * tile_hpad;
    // The biggest value font that fits that reading in a tile.
    const lv_font_t* f_val = f_head;
    while (f_val != f_min && layout_text_width(f_val, "$123.45") > inner)
        f_val = page_with_symbols(page_smaller(f_val, 1));
    const lv_font_t* f_cap = f_small;
    if (layout_text_width(f_cap, "Sessions") > inner) f_cap = page_with_symbols(page_smaller(f_small, 1));
    const int tile_h = lv_font_get_line_height(f_val) + lv_font_get_line_height(f_cap) + 2 * tile_pad + 2;
    for (int i = 0; i < 3; i++) tile_val[i] = nullptr;
    for (int i = 0; i < n_tiles; i++) {
        lv_obj_t* t = page_panel(root, A.x + i * (tile_w + gap), A.y, tile_w, tile_h);
        lv_obj_set_style_pad_hor(t, tile_hpad, 0);
        lv_obj_set_style_pad_ver(t, tile_pad, 0);
        tile_val[i] = page_label(t, f_val, THEME_TEXT, "-");
        lv_label_set_long_mode(tile_val[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(tile_val[i], inner);
        lv_obj_align(tile_val[i], LV_ALIGN_TOP_LEFT, 0, 0);
        // The long caption where it fits, else the short one.
        const bool fits = layout_text_width(f_cap, CAPTIONS[i]) <= inner;
        lv_obj_t* cap = page_label(t, f_cap, THEME_DIM, fits ? CAPTIONS[i] : CAPTIONS_TINY[i]);
        lv_label_set_long_mode(cap, LV_LABEL_LONG_DOT);
        lv_obj_set_size(cap, inner, lv_font_get_line_height(f_cap));
        lv_obj_align(cap, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    // Session rows below, as many as fit (at most four).
    const int y0 = A.y + tile_h + gap;
    const int body_h = lv_font_get_line_height(f_body);
    const int row_gap = gap < 8 ? gap : 8;
    const int row_min = body_h + 8;
    const int row_max = body_h + 2 * L.pad_y + 8;
    const int room = A.y + A.h - y0;
    n_slots = (room + row_gap) / (row_min + row_gap);
    if (n_slots > ACT_ROWS_MAX) n_slots = ACT_ROWS_MAX;
    if (n_slots < 0) n_slots = 0;
    int row_h = n_slots ? (room - (n_slots - 1) * row_gap) / n_slots : row_min;
    if (row_h > row_max) row_h = row_max;
    const int dot_px = body_h >= 24 ? 12 : 8;
    const int inner_w = A.w - 2 * L.pad_x;
    for (int i = 0; i < n_slots; i++) {
        ActRow* a = &rows[i];
        a->panel = page_panel(root, A.x, y0 + i * (row_h + row_gap), A.w, row_h);
        lv_obj_set_style_pad_hor(a->panel, L.pad_x, 0);
        a->dot = page_dot(a->panel, dot_px, THEME_DIM);
        lv_obj_align(a->dot, LV_ALIGN_LEFT_MID, 0, 0);
        a->name = page_label(a->panel, f_body, THEME_TEXT, "");
        lv_label_set_long_mode(a->name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(a->name, inner_w * 58 / 100 - dot_px - 10, body_h);
        lv_obj_align(a->name, LV_ALIGN_LEFT_MID, dot_px + 10, 0);
        a->label = page_label(a->panel, f_body, THEME_DIM, "");
        lv_label_set_long_mode(a->label, LV_LABEL_LONG_DOT);
        lv_obj_set_size(a->label, inner_w * 42 / 100 - 6, body_h);
        lv_obj_set_style_text_align(a->label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(a->label, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(a->panel, LV_OBJ_FLAG_HIDDEN);
    }
    empty_lbl = page_label(root, f_body, THEME_DIM, "No live sessions");
    lv_obj_align(empty_lbl, LV_ALIGN_TOP_MID, A.x + A.w / 2 - L.w / 2, y0 + (room - body_h) / 2);
    lv_obj_add_flag(empty_lbl, LV_OBJ_FLAG_HIDDEN);

    footer = page_make_footer(root);
    apply();
}

static void destroy(void) { root = nullptr; }

static bool available(uint32_t now) { return received && now - received_ms < ACT_STALE_MS; }

// Working dots breathe.
static void tick(uint32_t now) {
    if (now - pulse_ms < PULSE_MS) return;
    pulse_ms = now;
    pulse_on = !pulse_on;
    for (int i = 0; i < act.n_rows && i < n_slots; i++) {
        if (act.rows[i].state != SESSION_WORKING) continue;
        lv_obj_set_style_bg_opa(rows[i].dot, pulse_on ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

void pages_update_activity(const ActivityData* d) {
    act = *d;
    received = true;
    received_ms = millis();
    apply();
}

const PageDef page_activity = {
    "activity", build, destroy, available, nullptr, tick, nullptr, nullptr,
};
