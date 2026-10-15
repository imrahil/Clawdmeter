// Pace screen (daemon-fed {"k":"h"}): where this 5-hour window is heading and
// how the week has gone.
//   top panel    — "Limit by 15:40" / "64% at reset", the window so far as a
//                  line (terra-cotta) with the projection to the reset
//                  (faint green / amber / red), and Clawd beside it reacting
//                  to the pace: waving, pointing, scuttling.
//   bottom panel — weekly % added on each of the last 7 days against the
//                  even-split share (100/7 % a day), and the week's projection.
#include "ui.h"
#include "ui_internal.h"
#include "splash.h"
#include <string.h>

static const uint32_t PACE_STALE_MS = 15UL * 60 * 1000;   // daemon sends after every poll

static PaceData pace = {};
static bool     pace_received = false;
static uint32_t pace_ms = 0;
static bool     pace_hot_sent = false;   // splash_set_pace() state, for the watchdog

static lv_obj_t* root = nullptr;
static lv_obj_t* s_head;
static lv_obj_t* s_sub;
static lv_obj_t* s_limit;
static lv_obj_t* s_line;
static lv_obj_t* s_proj;
static lv_obj_t* s_dot;
static lv_obj_t* w_head;
static lv_obj_t* w_share;
static lv_obj_t* w_bars[PACE_DAYS];
static lv_obj_t* w_days[PACE_DAYS];
static SplashMini* mini = nullptr;

static lv_point_precise_t line_pts[PACE_SESS_MAX + 1];
static lv_point_precise_t proj_pts[2];
static lv_point_precise_t limit_pts[2];
static lv_point_precise_t share_pts[2];

// Chart / bar geometry inside the panels, set in build.
static int chart_w, chart_h;
static int bars_y, bars_h, col_w, bar_w;

static const char* const MOODS[] = { "waving", "pointing", "crab walking" };

static lv_color_t zone_color(int zone) {
    return zone >= 2 ? COL_RED : zone == 1 ? COL_AMBER : COL_GREEN;
}

static int chart_y_of(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (100 - pct) * (chart_h - 1) / 100;
}

static lv_obj_t* make_dashed(lv_obj_t* parent, lv_color_t color, lv_opa_t opa, int width) {
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_opa(l, opa, 0);
    lv_obj_set_style_line_dash_width(l, 6, 0);
    lv_obj_set_style_line_dash_gap(l, 5, 0);
    return l;
}

static void apply_session(void) {
    char buf[64];
    const int n = pace.n_sess;
    const int s_now = n > 0 ? pace.sess[n - 1] : (int)(ui_last_usage()->session_pct + 0.5f);

    // Headline: when the limit lands, or where the window ends up.
    lv_color_t head_col = zone_color(pace.zone);
    if (s_now >= 100)       snprintf(buf, sizeof(buf), "Limit reached");
    else if (pace.eta[0])   snprintf(buf, sizeof(buf), "Limit by %s", pace.eta);
    else if (pace.proj >= 0) snprintf(buf, sizeof(buf), "%d%% at reset", pace.proj);
    else { snprintf(buf, sizeof(buf), "Too early to tell"); head_col = COL_DIM; }
    lv_label_set_text(s_head, buf);
    lv_obj_set_style_text_color(s_head, head_col, 0);

    char reset[32];
    format_reset_time(ui_last_usage()->session_reset_mins, reset, sizeof(reset));
    snprintf(buf, sizeof(buf), "Now %d%%  \xC2\xB7  %s", s_now, reset);
    lv_label_set_text(s_sub, buf);

    splash_mini_set(mini, MOODS[pace.zone < 0 ? 0 : pace.zone > 2 ? 2 : pace.zone]);

    if (n <= 0) {
        lv_obj_add_flag(s_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_proj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // The window so far: slot k ends at (k+1)/30 of the width; the last point
    // sits at "now". A window starts empty, hence the (0, 0%) origin.
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
    lv_obj_set_pos(s_dot, x_now - L.dot_px / 2, y_now - L.dot_px / 2);
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
    char buf[48];
    if (pace.week_proj >= 0) {
        snprintf(buf, sizeof(buf), "Week on track for %d%%", pace.week_proj);
        lv_obj_set_style_text_color(w_head, pace.week_proj >= 100 ? COL_RED
                                          : pace.week_proj >= 80 ? COL_AMBER : COL_GREEN, 0);
    } else {
        snprintf(buf, sizeof(buf), "This week");
        lv_obj_set_style_text_color(w_head, COL_TEXT, 0);
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
        lv_label_set_text(w_days[i], d->day);
        lv_obj_set_style_text_color(w_days[i], i == pace.n_days - 1 ? COL_TEXT : COL_DIM, 0);
        lv_obj_set_pos(w_days[i], i * col_w, bottom + 4);
        lv_obj_clear_flag(w_days[i], LV_OBJ_FLAG_HIDDEN);

        int h;
        lv_color_t c;
        if (d->pct < 0) {                 // daemon wasn't running that day
            h = 2;
            c = COL_BAR_BG;
        } else {
            h = (int)(d->pct * bars_h / top);
            if (h < 3) h = 3;
            c = d->pct <= share ? COL_GREEN : d->pct <= 2 * share ? COL_AMBER : COL_RED;
        }
        lv_obj_set_size(w_bars[i], bar_w, h);
        lv_obj_set_pos(w_bars[i], i * col_w + (col_w - bar_w) / 2, bottom - h);
        lv_obj_set_style_bg_color(w_bars[i], c, 0);
        lv_obj_clear_flag(w_bars[i], LV_OBJ_FLAG_HIDDEN);
    }

    const int sy = bottom - (int)(share * bars_h / top);
    share_pts[0] = {0, (lv_value_precise_t)sy};
    share_pts[1] = {(lv_value_precise_t)(panel_inner_w(L.content_w) - 1), (lv_value_precise_t)sy};
    lv_line_set_points(w_share, share_pts, 2);
}

static void pace_apply(void) {
    if (!root || !pace_received) return;
    apply_session();
    apply_week();
}

static void pace_build(lv_obj_t* r) {
    root = r;
    make_screen_title(root, "Pace");

    const int gap = L.usage_panel_gap;
    const int avail = content_bottom() - L.content_y;
    const int a_h = (avail - gap) * 55 / 100;
    const int b_h = avail - gap - a_h;
    const int inner_w = panel_inner_w(L.content_w);

    // ---- Session panel ----
    lv_obj_t* a = make_panel(root, L.margin, L.content_y, L.content_w, a_h);
    const int head_h = lv_font_get_line_height(L.f_head);
    const int sub_h = lv_font_get_line_height(L.f_small);
    s_head = make_label(a, L.f_head, COL_TEXT, "");
    lv_obj_align(s_head, LV_ALIGN_TOP_LEFT, 0, 0);
    s_sub = make_label(a, L.f_small, COL_DIM, "");
    lv_obj_align(s_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Clawd stands to the right of the chart, feet on its baseline.
    const int creature_w = 28 * L.mini_cell;   // widest mood (pointing)
    mini = splash_mini_new(a, MOODS, 3, L.mini_cell);
    if (mini) {
        splash_mini_set_bg(mini, lv_color_to_u16(COL_PANEL));
        lv_obj_align(splash_mini_obj(mini), LV_ALIGN_BOTTOM_RIGHT, 0, -(sub_h + 6));
    }

    const int chart_top = head_h + 8;
    chart_w = inner_w - creature_w - 12;
    chart_h = panel_inner_h(a_h) - chart_top - sub_h - 8;

    lv_obj_t* chart = lv_obj_create(a);
    lv_obj_set_pos(chart, 0, chart_top);
    lv_obj_set_size(chart, chart_w, chart_h);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_clear_flag(chart, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(chart, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_limit = make_dashed(chart, COL_DIM, LV_OPA_40, 1);      // the 100% line
    limit_pts[0] = {0, 0};
    limit_pts[1] = {(lv_value_precise_t)(chart_w - 1), 0};
    lv_line_set_points(s_limit, limit_pts, 2);

    // Projection: translucent rather than dashed — LVGL only dashes
    // horizontal and vertical lines.
    s_proj = lv_line_create(chart);
    lv_obj_set_style_line_width(s_proj, L.st_line_w, 0);
    lv_obj_set_style_line_rounded(s_proj, true, 0);
    lv_obj_set_style_line_opa(s_proj, LV_OPA_50, 0);
    s_line = lv_line_create(chart);
    lv_obj_set_style_line_width(s_line, L.st_line_w, 0);
    lv_obj_set_style_line_rounded(s_line, true, 0);
    lv_obj_set_style_line_color(s_line, COL_ACCENT, 0);
    s_dot = make_dot(chart, L.dot_px, COL_ACCENT);

    // ---- Week panel ----
    lv_obj_t* b = make_panel(root, L.margin, L.content_y + a_h + gap, L.content_w, b_h);
    w_head = make_label(b, L.f_body, COL_TEXT, "");
    lv_obj_align(w_head, LV_ALIGN_TOP_LEFT, 0, 0);
    bars_y = lv_font_get_line_height(L.f_body) + 6;
    bars_h = panel_inner_h(b_h) - bars_y - sub_h - 4;
    col_w = inner_w / PACE_DAYS;
    bar_w = col_w * 55 / 100;
    w_share = make_dashed(b, COL_DIM, LV_OPA_50, 1);
    for (int i = 0; i < PACE_DAYS; i++) {
        lv_obj_t* bar = lv_obj_create(b);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bar, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        w_bars[i] = bar;
        w_days[i] = make_label(b, L.f_small, COL_DIM, "");
        lv_obj_set_width(w_days[i], col_w);
        lv_obj_set_style_text_align(w_days[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(w_days[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_move_foreground(w_share);

    pace_apply();
}

static void pace_destroy(void) {
    splash_mini_free(mini);
    mini = nullptr;
    root = nullptr;
}

static bool pace_available(uint32_t now) {
    return pace_received && now - pace_ms < PACE_STALE_MS;
}

static const char* pace_status(uint32_t now) {
    (void)now;
    static char buf[32];
    format_reset_time(ui_last_usage()->session_reset_mins, buf, sizeof(buf));
    return buf[0] == '-' ? "" : buf;
}

static void pace_tick(uint32_t now) {
    (void)now;
    splash_mini_step(mini);
}

void pace_watchdog(uint32_t now) {
    if (pace_hot_sent && !pace_available(now)) {
        pace_hot_sent = false;
        splash_set_pace(0);
    }
}

void ui_update_pace(const PaceData* d) {
    pace = *d;
    pace_received = true;
    pace_ms = lv_tick_get();
    splash_set_pace(d->zone);
    pace_hot_sent = d->zone != 0;
    pace_apply();
}

const ScreenDef pace_screen = {
    pace_build, pace_destroy, pace_available, nullptr,
    pace_status, pace_tick, nullptr, nullptr,
};
