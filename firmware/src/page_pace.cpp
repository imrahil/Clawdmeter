// Pace page (the app's {"k":"h"} write): where this 5-hour window is heading
// and how the week has gone.
//   top card    — "Limit by 15:40" / "64% at reset", the window so far as a
//                 line (Clawd orange) with the projection to the reset
//                 (translucent green / amber / red) under the 100 % line.
//   bottom card — weekly % added on each of the last 7 days against the
//                 even-split share (100/7 % a day), and the week's projection.
// The status line carries the session's reset countdown. Zone 2 (the limit
// lands before the reset) also lifts Clawd's mood to Heavy (usage_rate.h).
#include "pages.h"
#include "pages_internal.h"
#include "ui.h"
#include "usage_rate.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

static const uint32_t PACE_STALE_MS = 30UL * 60 * 1000;   // sent after every fresh poll

static PaceData pace = {};
static bool     received = false;
static uint32_t received_ms = 0;

static lv_obj_t* root = nullptr;
static lv_obj_t* s_head;
static lv_obj_t* s_sub;
static lv_obj_t* s_line;
static lv_obj_t* s_proj;
static lv_obj_t* s_dot;
static lv_obj_t* w_card;
static lv_obj_t* w_head;
static lv_obj_t* w_share;
static lv_obj_t* w_bars[PACE_DAYS];
static lv_obj_t* w_days[PACE_DAYS];
static lv_obj_t* footer;

static lv_point_precise_t line_pts[PACE_SESS_MAX + 1];
static lv_point_precise_t proj_pts[2];
static lv_point_precise_t limit_pts[2];
static lv_point_precise_t share_pts[2];

static int chart_w, chart_h, dot_px;
static int bars_y, bars_h, col_w, bar_w, week_w;
static bool day_initial;

static lv_color_t zone_color(int zone) {
    return zone >= 2 ? THEME_RED : zone == 1 ? THEME_AMBER : THEME_GREEN;
}

static int chart_y_of(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (100 - pct) * (chart_h - 1) / 100;
}

static int session_now(void) {
    if (pace.n_sess > 0) return pace.sess[pace.n_sess - 1];
    const UsageData* u = page_usage();
    return u ? (int)(u->session_pct + 0.5f) : 0;
}

static void apply_session(void) {
    char buf[64];
    const int n = pace.n_sess;
    const int s_now = session_now();

    // Headline: when the limit lands, or where the window ends up.
    lv_color_t head_col = zone_color(pace.zone);
    if (s_now >= 100)        snprintf(buf, sizeof(buf), "Limit reached");
    else if (pace.eta[0])    snprintf(buf, sizeof(buf), "Limit by %s", pace.eta);
    else if (pace.proj >= 0) snprintf(buf, sizeof(buf), "%d%% at reset", pace.proj);
    else { snprintf(buf, sizeof(buf), "Too early to tell"); head_col = THEME_DIM; }
    lv_label_set_text(s_head, buf);
    lv_obj_set_style_text_color(s_head, head_col, 0);

    char reset[32];
    const UsageData* u = page_usage();
    page_reset_text(u ? u->session_reset_mins : -1, reset, sizeof(reset));
    if (reset[0]) snprintf(buf, sizeof(buf), "Now %d%%  \xC2\xB7  %s", s_now, reset);
    else          snprintf(buf, sizeof(buf), "Now %d%%", s_now);
    lv_label_set_text(s_sub, buf);

    if (n <= 0) {
        lv_obj_add_flag(s_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_proj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // The window so far: slot k ends at (k+1)/30 of the width; the last point
    // sits at "now". A window starts empty, hence the (0, 0 %) origin.
    const int w = chart_w - 1;
    const int x_now = pace.frac * w / 100;
    line_pts[0] = {0, (lv_value_precise_t)chart_y_of(0)};
    for (int k = 0; k < n; k++) {
        int x = (k + 1) * w / PACE_SESS_MAX;
        if (x > x_now || k == n - 1) x = x_now;
        line_pts[k + 1] = {(lv_value_precise_t)x, (lv_value_precise_t)chart_y_of(pace.sess[k])};
    }
    lv_line_set_points(s_line, line_pts, n + 1);
    lv_obj_clear_flag(s_line, LV_OBJ_FLAG_HIDDEN);

    const int y_now = chart_y_of(s_now);
    lv_obj_set_pos(s_dot, x_now - dot_px / 2, y_now - dot_px / 2);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_HIDDEN);

    // Projection to the reset, stopping where it meets the limit.
    if (pace.proj >= 0 && s_now < 100 && x_now < w) {
        int x_end = w, p_end = pace.proj;
        if (pace.proj > 100) {
            x_end = x_now + (100 - s_now) * (w - x_now) / (pace.proj - s_now);
            p_end = 100;
        }
        proj_pts[0] = {(lv_value_precise_t)x_now, (lv_value_precise_t)y_now};
        proj_pts[1] = {(lv_value_precise_t)x_end, (lv_value_precise_t)chart_y_of(p_end)};
        lv_line_set_points(s_proj, proj_pts, 2);
        lv_obj_set_style_line_color(s_proj, zone_color(pace.zone), 0);
        lv_obj_clear_flag(s_proj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_proj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_week(void) {
    if (!w_card) return;
    char buf[48];
    if (pace.week_proj >= 0) {
        snprintf(buf, sizeof(buf), "Week on track for %d%%", pace.week_proj);
        lv_obj_set_style_text_color(w_head, pace.week_proj >= 100 ? THEME_RED
                                          : pace.week_proj >= 80 ? THEME_AMBER : THEME_GREEN, 0);
    } else {
        snprintf(buf, sizeof(buf), "This week");
        lv_obj_set_style_text_color(w_head, THEME_TEXT, 0);
    }
    lv_label_set_text(w_head, buf);

    // Scale: the busiest day, but never so tight that a quiet week looks loud.
    const float share = 100.0f / PACE_DAYS;
    float top = share * 1.5f;
    for (int i = 0; i < pace.n_days; i++) if (pace.days[i].pct > top) top = pace.days[i].pct;

    const int bottom = bars_y + bars_h;
    for (int i = 0; i < PACE_DAYS; i++) {
        if (i >= pace.n_days) {
            lv_obj_add_flag(w_bars[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(w_days[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const PaceDay* d = &pace.days[i];
        const char initial[2] = { d->day[0], '\0' };
        lv_label_set_text(w_days[i], day_initial ? initial : d->day);
        lv_obj_set_style_text_color(w_days[i], i == pace.n_days - 1 ? THEME_TEXT : THEME_DIM, 0);
        lv_obj_set_pos(w_days[i], i * col_w, bottom + 2);
        lv_obj_clear_flag(w_days[i], LV_OBJ_FLAG_HIDDEN);

        int h;
        lv_color_t c;
        if (d->pct < 0) {                 // the app wasn't running that day
            h = 2;
            c = THEME_BAR_BG;
        } else {
            h = (int)(d->pct * bars_h / top);
            if (h < 3) h = 3;
            c = d->pct <= share ? THEME_GREEN : d->pct <= 2 * share ? THEME_AMBER : THEME_RED;
        }
        lv_obj_set_size(w_bars[i], bar_w, h);
        lv_obj_set_pos(w_bars[i], i * col_w + (col_w - bar_w) / 2, bottom - h);
        lv_obj_set_style_bg_color(w_bars[i], c, 0);
        lv_obj_clear_flag(w_bars[i], LV_OBJ_FLAG_HIDDEN);
    }

    const int sy = bottom - (int)(share * bars_h / top);
    share_pts[0] = {0, (lv_value_precise_t)sy};
    share_pts[1] = {(lv_value_precise_t)(week_w - 1), (lv_value_precise_t)sy};
    lv_line_set_points(w_share, share_pts, 2);
}

static void apply(void) {
    if (!root || !received) return;
    apply_session();
    apply_week();
    char reset[32];
    const UsageData* u = page_usage();
    page_reset_text(u ? u->session_reset_mins : -1, reset, sizeof(reset));
    page_set_footer(footer, reset);
}

static void build(lv_obj_t* r) {
    root = r;
    const Layout& L = ui_layout();
    const PageArea A = page_area(L);
    page_make_title(root, "Pace");

    const lv_font_t* f_head = page_with_symbols(L.reset_font);
    const lv_font_t* f_body = page_with_symbols(page_smaller(L.reset_font, 1));
    const lv_font_t* f_small = page_with_symbols(page_smaller(L.reset_font, 2));
    const int head_h = lv_font_get_line_height(f_head);
    const int body_h = lv_font_get_line_height(f_body);
    const int sub_h = lv_font_get_line_height(f_small);
    const int gap = page_gap(L);
    const int inner_w = A.w - 2 * L.pad_x;
    const int line_w = A.h >= 300 ? 4 : 3;
    dot_px = line_w * 3;

    // The week card needs room for its headline, bars and day names; short
    // panels keep the session card alone.
    const int week_min = body_h + 6 + 24 + sub_h + 2 * L.pad_y;
    const bool with_week = A.h - gap - week_min >= head_h + sub_h + 48 + 2 * L.pad_y;
    int a_h = with_week ? (A.h - gap) * 55 / 100 : A.h;
    if (with_week && A.h - gap - a_h < week_min) a_h = A.h - gap - week_min;
    const int b_h = A.h - gap - a_h;

    // ---- Session card ----
    lv_obj_t* a = page_card(root, A.x, A.y, A.w, a_h);
    s_head = page_label(a, f_head, THEME_TEXT, "");
    lv_label_set_long_mode(s_head, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s_head, inner_w, head_h);
    lv_obj_align(s_head, LV_ALIGN_TOP_LEFT, 0, 0);
    s_sub = page_label(a, f_small, THEME_DIM, "");
    lv_label_set_long_mode(s_sub, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s_sub, inner_w, sub_h);
    lv_obj_align(s_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    const int chart_top = head_h + 6 + dot_px / 2;
    chart_w = inner_w;
    chart_h = a_h - 2 * L.pad_y - chart_top - sub_h - 6 - dot_px / 2;
    if (chart_h < 8) chart_h = 8;

    lv_obj_t* chart = lv_obj_create(a);
    lv_obj_set_pos(chart, 0, chart_top);
    lv_obj_set_size(chart, chart_w, chart_h);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_clear_flag(chart, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(chart, LV_OBJ_FLAG_OVERFLOW_VISIBLE);   // the dot may sit on the edge

    lv_obj_t* limit = page_line(chart, THEME_DIM, 1, 6);    // the 100 % line
    lv_obj_set_style_line_opa(limit, LV_OPA_40, 0);
    limit_pts[0] = {0, 0};
    limit_pts[1] = {(lv_value_precise_t)(chart_w - 1), 0};
    lv_line_set_points(limit, limit_pts, 2);

    // Projection: translucent rather than dashed (LVGL only dashes
    // horizontal and vertical lines).
    s_proj = page_line(chart, THEME_GREEN, line_w, 0);
    lv_obj_set_style_line_opa(s_proj, LV_OPA_50, 0);
    s_line = page_line(chart, THEME_ACCENT, line_w, 0);
    s_dot = page_dot(chart, dot_px, THEME_ACCENT);
    lv_obj_add_flag(s_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_proj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);

    // ---- Week card ----
    w_card = nullptr;
    if (with_week) {
        w_card = page_card(root, A.x, A.y + a_h + gap, A.w, b_h);
        w_head = page_label(w_card, f_body, THEME_TEXT, "");
        lv_label_set_long_mode(w_head, LV_LABEL_LONG_DOT);
        lv_obj_set_size(w_head, inner_w, body_h);
        lv_obj_align(w_head, LV_ALIGN_TOP_LEFT, 0, 0);
        week_w = inner_w;
        bars_y = body_h + 6;
        bars_h = b_h - 2 * L.pad_y - bars_y - sub_h - 2;
        col_w = inner_w / PACE_DAYS;
        bar_w = col_w * 55 / 100;
        // Day names on one line: a size down where "Wed" doesn't fit a column.
        const lv_font_t* f_day = f_small;
        if (layout_text_width(f_day, "Wed") > col_w - 2) f_day = page_with_symbols(page_smaller(f_small, 1));
        day_initial = layout_text_width(f_day, "Wed") > col_w - 2;   // then "M T W…"
        w_share = page_line(w_card, THEME_DIM, 1, 4);
        lv_obj_set_style_line_opa(w_share, LV_OPA_50, 0);
        for (int i = 0; i < PACE_DAYS; i++) {
            lv_obj_t* bar = page_panel(w_card, 0, 0, bar_w, 2);
            lv_obj_set_style_radius(bar, 3, 0);
            lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
            w_bars[i] = bar;
            w_days[i] = page_label(w_card, f_day, THEME_DIM, "");
            lv_label_set_long_mode(w_days[i], LV_LABEL_LONG_CLIP);
            lv_obj_set_size(w_days[i], col_w, lv_font_get_line_height(f_day));
            lv_obj_set_style_text_align(w_days[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_align(w_days[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_add_flag(w_days[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_move_foreground(w_share);
    }

    footer = page_make_footer(root);
    apply();
}

static void destroy(void) { root = nullptr; w_card = nullptr; }

static bool available(uint32_t now) { return received && now - received_ms < PACE_STALE_MS; }

// A usage payload moved "Now" or the countdown.
static void tick(uint32_t now) {
    (void)now;
    static uint32_t seen = 0;
    if (page_usage_gen() == seen) return;
    seen = page_usage_gen();
    apply();
}

void pages_update_pace(const PaceData* d) {
    pace = *d;
    received = true;
    received_ms = millis();
    usage_rate_set_pace(d->zone);
    apply();
}

const PageDef page_pace = {
    "pace", build, destroy, available, nullptr, tick, nullptr, nullptr,
};
