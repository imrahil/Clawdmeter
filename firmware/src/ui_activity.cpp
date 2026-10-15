// Activity screen (daemon-fed {"k":"a"}): today's Claude Code totals and the
// live sessions, from the transcripts on the host.
//   tiles — tokens today, their API-equivalent value, sessions today
//   rows  — project, state dot (terra-cotta pulsing = working, red = needs
//           you, grey = idle) and what it's doing / how long it's been idle
#include "ui.h"
#include "ui_internal.h"
#include <stdio.h>
#include <string.h>

static const uint32_t ACT_STALE_MS = 5UL * 60 * 1000;   // daemon sends every 30 s
#define PULSE_MS 600

static ActivityData act = {};
static bool     act_received = false;
static uint32_t act_ms = 0;

struct ActRow {
    lv_obj_t* panel;
    lv_obj_t* dot;
    lv_obj_t* name;
    lv_obj_t* label;
};

static lv_obj_t* root = nullptr;
static lv_obj_t* tile_val[3];
static ActRow    rows[ACT_ROWS_MAX];
static lv_obj_t* empty_lbl;
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

static void act_apply(void) {
    if (!root || !act_received) return;
    char buf[24];
    format_tokens(buf, sizeof(buf), act.tokens);
    lv_label_set_text(tile_val[0], buf);
    format_cost(buf, sizeof(buf), act.cost_cents);
    lv_label_set_text(tile_val[1], buf);
    snprintf(buf, sizeof(buf), "%d", act.sessions);
    lv_label_set_text(tile_val[2], buf);

    for (int i = 0; i < ACT_ROWS_MAX; i++) {
        ActRow* r = &rows[i];
        if (i >= act.n_rows) {
            lv_obj_add_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const ActivityRow* a = &act.rows[i];
        lv_label_set_text(r->name, a->name);
        lv_color_t dot = COL_DIM, text = COL_DIM;
        if (a->state == SESSION_WAITING) {
            dot = COL_RED;
            text = THEME_LOSS;
            lv_label_set_text(r->label, a->label[0] ? a->label : "needs you");
        } else if (a->state == SESSION_WORKING) {
            dot = text = COL_ACCENT;
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
}

static void act_build(lv_obj_t* r) {
    root = r;
    make_screen_title(root, "Activity");

    const int gap = L.usage_panel_gap;
    static const char* const CAPTIONS[3]      = { "Tokens today", "API value", "Sessions" };
    static const char* const CAPTIONS_TINY[3] = { "Tokens", "Value", "Sessions" };

    const int tile_w = (L.content_w - 2 * gap) / 3;
    const int tile_h = lv_font_get_line_height(L.f_head) + lv_font_get_line_height(L.f_small)
                     + 2 * L.panel_pad_y + 4;
    for (int i = 0; i < 3; i++) {
        lv_obj_t* t = make_panel(root, L.margin + i * (tile_w + gap), L.content_y, tile_w, tile_h);
        tile_val[i] = make_label(t, L.f_head, COL_TEXT, "-");
        lv_obj_align(tile_val[i], LV_ALIGN_TOP_LEFT, 0, 0);
        lv_point_t size;   // the long caption where it fits, else the short one
        lv_text_get_size(&size, CAPTIONS[i], L.f_small, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const bool fits = size.x <= panel_inner_w(tile_w);
        lv_obj_t* cap = make_label(t, L.f_small, COL_DIM, fits ? CAPTIONS[i] : CAPTIONS_TINY[i]);
        lv_obj_align(cap, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    const int y0 = L.content_y + tile_h + gap;
    const int row_gap = L.st_row_gap;
    int row_h = (content_bottom() - y0 - (ACT_ROWS_MAX - 1) * row_gap) / ACT_ROWS_MAX;
    const int row_max = lv_font_get_line_height(L.f_body) + 2 * L.panel_pad_y + 16;
    if (row_h > row_max) row_h = row_max;
    const int inner_w = panel_inner_w(L.content_w);
    for (int i = 0; i < ACT_ROWS_MAX; i++) {
        ActRow* a = &rows[i];
        a->panel = make_panel(root, L.margin, y0 + i * (row_h + row_gap), L.content_w, row_h);
        lv_obj_set_style_pad_top(a->panel, 0, 0);
        lv_obj_set_style_pad_bottom(a->panel, 0, 0);
        a->dot = make_dot(a->panel, L.dot_px, COL_DIM);
        lv_obj_align(a->dot, LV_ALIGN_LEFT_MID, 0, 0);
        a->name = make_label(a->panel, L.f_body, COL_TEXT, "");
        lv_label_set_long_mode(a->name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(a->name, inner_w * 58 / 100 - L.dot_px - 10);
        lv_obj_align(a->name, LV_ALIGN_LEFT_MID, L.dot_px + 10, 0);
        a->label = make_label(a->panel, L.f_body, COL_DIM, "");
        lv_obj_align(a->label, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(a->panel, LV_OBJ_FLAG_HIDDEN);
    }
    empty_lbl = make_label(root, L.f_body, COL_DIM, "No live sessions");
    lv_obj_align(empty_lbl, LV_ALIGN_TOP_MID, 0, y0 + row_h);
    lv_obj_add_flag(empty_lbl, LV_OBJ_FLAG_HIDDEN);

    act_apply();
}

static void act_destroy(void) {
    root = nullptr;
}

static bool act_available(uint32_t now) {
    return act_received && now - act_ms < ACT_STALE_MS;
}

static const char* act_status(uint32_t now) {
    (void)now;
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

// Working dots breathe.
static void act_tick(uint32_t now) {
    if (now - pulse_ms < PULSE_MS) return;
    pulse_ms = now;
    pulse_on = !pulse_on;
    for (int i = 0; i < act.n_rows && i < ACT_ROWS_MAX; i++) {
        if (act.rows[i].state != SESSION_WORKING) continue;
        lv_obj_set_style_bg_opa(rows[i].dot, pulse_on ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

void ui_update_activity(const ActivityData* d) {
    act = *d;
    act_received = true;
    act_ms = lv_tick_get();
    act_apply();
}

const ScreenDef activity_screen = {
    act_build, act_destroy, act_available, nullptr,
    act_status, act_tick, nullptr, nullptr,
};
