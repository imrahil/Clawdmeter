// Stocks screen (optional, daemon-fed {"k":"q"}, one payload per row):
// symbol, name, price, day change and an intraday sparkline per row.
#include "ui.h"
#include "ui_internal.h"
#include <math.h>
#include <string.h>

// Widgets (null while the screen isn't built) + the latest quote per row.
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
static StockRow  stock_rows[STOCK_MAX];
static lv_obj_t* stocks_container = nullptr;
static int       stock_count = 0;      // rows the daemon says it's sending
static uint32_t  stock_ms = 0;

static void layout_stock_rows(void);

static void stocks_build(lv_obj_t* root) {
    stocks_container = root;
    make_screen_title(stocks_container, "Stocks");

    for (int i = 0; i < STOCK_MAX; i++) {
        StockRow* r = &stock_rows[i];
        r->panel = make_panel(stocks_container, L.margin, 0, L.content_w, L.st_row_h);
        lv_obj_set_style_pad_top(r->panel, L.st_pad_y, 0);
        lv_obj_set_style_pad_bottom(r->panel, L.st_pad_y, 0);

        r->sym = lv_label_create(r->panel);
        lv_obj_set_style_text_font(r->sym, L.st_sym_font, 0);
        lv_obj_set_style_text_color(r->sym, COL_TEXT, 0);
        lv_obj_align(r->sym, LV_ALIGN_TOP_LEFT, 0, 0);

        r->live = make_dot(r->panel, 8, COL_ACCENT);

        r->name = lv_label_create(r->panel);
        lv_label_set_long_mode(r->name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(r->name, L.st_left_w, lv_font_get_line_height(L.st_name_font));   // one line, cut with "…"
        lv_obj_set_style_text_font(r->name, L.st_name_font, 0);
        lv_obj_set_style_text_color(r->name, COL_DIM, 0);
        lv_obj_align(r->name, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        r->base = lv_line_create(r->panel);
        lv_obj_set_style_line_width(r->base, 1, 0);
        lv_obj_set_style_line_color(r->base, COL_DIM, 0);
        lv_obj_set_style_line_opa(r->base, LV_OPA_40, 0);
        lv_obj_set_style_line_dash_width(r->base, 4, 0);
        lv_obj_set_style_line_dash_gap(r->base, 4, 0);

        r->spark = lv_line_create(r->panel);
        lv_obj_set_style_line_width(r->spark, L.st_line_w, 0);
        lv_obj_set_style_line_rounded(r->spark, true, 0);

        r->price = lv_label_create(r->panel);
        lv_obj_set_style_text_font(r->price, L.st_price_font, 0);
        lv_obj_set_style_text_color(r->price, COL_TEXT, 0);
        lv_obj_align(r->price, LV_ALIGN_TOP_RIGHT, 0, 0);

        r->chg = lv_label_create(r->panel);
        lv_obj_set_style_text_font(r->chg, L.st_chg_font, 0);
        lv_obj_align(r->chg, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

        lv_obj_add_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
    }

    layout_stock_rows();
}

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

// Lay out rows for `stock_count` and redraw each row that has a quote. Row
// height shrinks so that STOCK_MAX rows still fit under the header.
static void layout_stock_rows(void) {
    const int n = stock_count;
    if (n <= 0 || !stocks_container) return;
    const int avail = L.scr_h - L.content_y - L.footer_reserve;
    int h = (avail - (n - 1) * L.st_row_gap) / n;
    if (h > L.st_row_h) h = L.st_row_h;
    const int inner_h = h - 2 * L.st_pad_y;
    const int spark_x = L.st_left_w + (L.content_w - 2 * L.panel_pad_x - L.st_left_w - L.st_spark_w) / 4;
    const int spark_h = inner_h - 8;
    const int spark_y = (inner_h - spark_h) / 2;

    for (int i = 0; i < STOCK_MAX; i++) {
        StockRow* r = &stock_rows[i];
        if (i >= n || !r->have) {
            lv_obj_add_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_y(r->panel, L.content_y + i * (h + L.st_row_gap));
        lv_obj_set_height(r->panel, h);

        const StockQuote* q = &r->q;
        char buf[32];
        lv_label_set_text(r->sym, q->symbol);
        lv_obj_align_to(r->live, r->sym, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
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
        const int pts = q->n_hist;
        const int span_w = (L.st_spark_w - 1) * q->session_pct / 100;
        for (int k = 0; k < pts; k++) {
            r->spark_pts[k].x = pts > 1 ? k * span_w / (pts - 1) : 0;
            r->spark_pts[k].y = (99 - q->hist[k]) * (spark_h - 1) / 99;
        }
        lv_line_set_points(r->spark, r->spark_pts, pts);
        lv_obj_set_style_line_color(r->spark, COL_ACCENT, 0);   // the change % carries up/down
        lv_obj_set_pos(r->spark, spark_x, spark_y);

        const int by = (99 - q->baseline) * (spark_h - 1) / 99;
        r->base_pts[0] = {0, by};
        r->base_pts[1] = {L.st_spark_w - 1, by};
        lv_line_set_points(r->base, r->base_pts, 2);
        lv_obj_set_pos(r->base, spark_x, spark_y);

        lv_obj_clear_flag(r->panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_update_stock(const StockQuote* q) {
    if (q->index < 0 || q->index >= STOCK_MAX) return;
    stock_rows[q->index].q = *q;
    stock_rows[q->index].have = true;
    stock_count = q->count > STOCK_MAX ? STOCK_MAX : q->count;
    stock_ms = lv_tick_get();
    layout_stock_rows();
}

static void stocks_destroy(void) {
    stocks_container = nullptr;
}

static bool stocks_available(uint32_t now) {
    return stock_count > 0 && now - stock_ms < EXTRA_STALE_MS;
}

static const char* stocks_status(uint32_t now) {
    (void)now;
    for (int i = 0; i < stock_count; i++) {
        if (stock_rows[i].have && stock_rows[i].q.open) return "Markets open";
    }
    return "Markets closed";
}

const ScreenDef stocks_screen = {
    stocks_build, stocks_destroy, stocks_available, nullptr,
    stocks_status, nullptr, nullptr, nullptr,
};

