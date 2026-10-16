// Stocks page (the app's {"k":"q"} writes, one per row, opt-in `stocks =`):
// symbol, name, price, day change and an intraday sparkline per row against
// the previous close (dashed). The status line says whether any market is
// open.
#include "pages.h"
#include "pages_internal.h"
#include "ui.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

struct StockRow {
    lv_obj_t* panel;
    lv_obj_t* sym;
    lv_obj_t* live;          // dot beside the symbol while the market is open
    lv_obj_t* name;
    lv_obj_t* base;          // previous close, dashed
    lv_obj_t* spark;
    lv_obj_t* price;
    lv_obj_t* chg;
    lv_point_precise_t base_pts[2];
    lv_point_precise_t spark_pts[STOCK_HIST_MAX];
    StockQuote q;
    bool have;
};
static StockRow  rows[STOCK_MAX];
static int       count = 0;            // rows the app says it's sending
static uint32_t  received_ms = 0;

static lv_obj_t* root = nullptr;
static lv_obj_t* footer;
static int       slots;                // rows that fit on this panel
static int       row_h, pad_y, spark_x, spark_w, line_w;

static void format_price(char* buf, size_t len, const char* cur, float p) {
    const char* sym = strcmp(cur, "USD") == 0 ? "$"
                    : strcmp(cur, "EUR") == 0 ? "\xE2\x82\xAC"
                    : strcmp(cur, "GBP") == 0 ? "\xC2\xA3"
                    : strcmp(cur, "JPY") == 0 ? "\xC2\xA5"
                    : nullptr;
    char num[16];
    snprintf(num, sizeof(num), p >= 10000.0f ? "%.0f" : "%.2f", p);
    if (sym) snprintf(buf, len, "%s%s", sym, num);
    else     snprintf(buf, len, "%s %s", cur, num);
}

static void apply(void) {
    if (!root) return;
    const Layout& L = ui_layout();
    const PageArea A = page_area(L);
    const int n = count < slots ? count : slots;
    const int gap = page_gap(L) < 8 ? page_gap(L) : 8;
    int h = n > 0 ? (A.h - (n - 1) * gap) / n : row_h;
    if (h > row_h) h = row_h;

    bool open = false;
    for (int i = 0; i < slots; i++) {
        StockRow* r = &rows[i];
        if (i >= n || !r->have) {
            lv_obj_add_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_y(r->panel, A.y + i * (h + gap));
        lv_obj_set_height(r->panel, h);

        const StockQuote* q = &r->q;
        open = open || q->open;
        char buf[32];
        lv_label_set_text(r->sym, q->symbol);
        lv_obj_align_to(r->live, r->sym, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
        if (q->open) lv_obj_clear_flag(r->live, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(r->live, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(r->name, q->name);

        format_price(buf, sizeof(buf), q->currency, q->price);
        lv_label_set_text(r->price, buf);

        const float pct = q->prev_close > 0 ? (q->price - q->prev_close) / q->prev_close * 100.0f : 0.0f;
        const bool up = pct >= 0.0f;
        snprintf(buf, sizeof(buf), "%s%.2f%%", up ? "+" : "\xE2\x88\x92", fabsf(pct));
        lv_label_set_text(r->chg, buf);
        lv_obj_set_style_text_color(r->chg, up ? THEME_GAIN : THEME_LOSS, 0);

        // Sparkline: 0..99 → top..bottom of the plot box. Mid-session it only
        // spans the elapsed share of the trading day, so the x axis is time.
        // Inside the row's padding: the full content height, a little air.
        const int sh = h - 2 * pad_y - 4;
        const int sy = 2;
        const int pts = q->n_hist;
        const int span_w = (spark_w - 1) * q->session_pct / 100;
        for (int k = 0; k < pts; k++) {
            r->spark_pts[k].x = pts > 1 ? k * span_w / (pts - 1) : 0;
            r->spark_pts[k].y = (99 - q->hist[k]) * (sh - 1) / 99;
        }
        lv_line_set_points(r->spark, r->spark_pts, pts);
        lv_obj_set_pos(r->spark, spark_x, sy);

        const int by = (99 - q->baseline) * (sh - 1) / 99;
        r->base_pts[0] = {0, (lv_value_precise_t)by};
        r->base_pts[1] = {(lv_value_precise_t)(spark_w - 1), (lv_value_precise_t)by};
        lv_line_set_points(r->base, r->base_pts, 2);
        lv_obj_set_pos(r->base, spark_x, sy);

        lv_obj_clear_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
    }
    page_set_footer(footer, n > 0 ? (open ? "Markets open" : "Markets closed") : "");
}

static void build(lv_obj_t* r) {
    root = r;
    const Layout& L = ui_layout();
    const PageArea A = page_area(L);
    page_make_title(root, "Stocks");

    const lv_font_t* f_big = page_with_symbols(page_smaller(L.reset_font, 1));
    const lv_font_t* f_small = page_with_symbols(page_smaller(L.reset_font, 2));
    const int big_h = lv_font_get_line_height(f_big);
    const int small_h = lv_font_get_line_height(f_small);
    pad_y = A.h >= 300 ? 10 : 6;
    const int gap = page_gap(L) < 8 ? page_gap(L) : 8;
    const int row_min = big_h + small_h + 2 * pad_y;
    row_h = row_min + 16;
    slots = (A.h + gap) / (row_min + gap);
    if (slots > STOCK_MAX) slots = STOCK_MAX;
    const int inner_w = A.w - 2 * L.pad_x;
    const int left_w = inner_w * 36 / 100;
    const int right_w = inner_w * 30 / 100;
    spark_w = inner_w - left_w - right_w - 16;
    spark_x = left_w + 8;
    line_w = A.h >= 300 ? 3 : 2;

    for (int i = 0; i < STOCK_MAX; i++) rows[i].panel = nullptr;
    for (int i = 0; i < slots; i++) {
        StockRow* s = &rows[i];
        s->panel = page_panel(root, A.x, A.y, A.w, row_h);
        lv_obj_set_style_pad_hor(s->panel, L.pad_x, 0);
        lv_obj_set_style_pad_ver(s->panel, pad_y, 0);

        s->sym = page_label(s->panel, f_big, THEME_TEXT, "");
        lv_obj_align(s->sym, LV_ALIGN_TOP_LEFT, 0, 0);
        s->live = page_dot(s->panel, 8, THEME_ACCENT);
        s->name = page_label(s->panel, f_small, THEME_DIM, "");
        lv_label_set_long_mode(s->name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(s->name, left_w, small_h);   // one line, cut with "…"
        lv_obj_align(s->name, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        s->base = page_line(s->panel, THEME_DIM, 1, 4);
        lv_obj_set_style_line_opa(s->base, LV_OPA_40, 0);
        s->spark = page_line(s->panel, THEME_ACCENT, line_w, 0);   // the change % carries up/down

        s->price = page_label(s->panel, f_big, THEME_TEXT, "");
        lv_obj_align(s->price, LV_ALIGN_TOP_RIGHT, 0, 0);
        s->chg = page_label(s->panel, f_small, THEME_GAIN, "");
        lv_obj_align(s->chg, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_add_flag(s->panel, LV_OBJ_FLAG_HIDDEN);
    }
    footer = page_make_footer(root);
    apply();
}

static void destroy(void) { root = nullptr; }

static bool available(uint32_t now) { return count > 0 && now - received_ms < PAGE_FEED_STALE_MS; }

void pages_update_stock(const StockQuote* q) {
    if (q->index < 0 || q->index >= STOCK_MAX) return;
    rows[q->index].q = *q;
    rows[q->index].have = true;
    count = q->count > STOCK_MAX ? STOCK_MAX : q->count;
    received_ms = millis();
    apply();
}

const PageDef page_stocks = {
    "stocks", build, destroy, available, nullptr, nullptr, nullptr, nullptr,
};
