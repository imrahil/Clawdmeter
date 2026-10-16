// Focus page (focus.h, device-local): a ring that empties as the session
// runs, the countdown and phase inside it, the round dots and a hint under
// it. Tap starts / pauses; a long press resets (or skips a break; from
// ready, it starts the cycle of four over). The corner Clawd keeps you
// company from his seat.
#include "pages.h"
#include "pages_internal.h"
#include "ui.h"
#include "focus.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#define RING_RANGE 1000

static lv_obj_t* root = nullptr;
static lv_obj_t* ring;
static lv_obj_t* time_lbl;
static lv_obj_t* phase_lbl;
static lv_obj_t* hint_lbl;
static lv_obj_t* footer;
static lv_obj_t* dots[FOCUS_ROUNDS];
static char      shown_time[12];
static int       shown_key = -1;

static const char* status_text(void) {
    switch (focus_phase()) {
    case FOCUS_WORK:  return focus_running() ? "Focusing" : "Paused";
    case FOCUS_BREAK: return "On a break";
    default:          return "Ready to focus";
    }
}

static void apply(uint32_t now) {
    if (!root) return;
    const FocusPhase ph = focus_phase();
    const bool run = focus_running();
    const uint32_t left = focus_remaining_ms(now);
    const uint32_t total = focus_total_ms();

    char buf[12];
    const uint32_t secs = (left + 999) / 1000;   // 25:00 until the first second has gone
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
    if (strcmp(buf, shown_time) != 0) {
        strncpy(shown_time, buf, sizeof(shown_time) - 1);
        lv_label_set_text(time_lbl, buf);
        lv_arc_set_value(ring, total ? (int32_t)((uint64_t)left * RING_RANGE / total) : 0);
    }

    // The rest only changes with the phase, the run state or the round.
    const int key = ph * 100 + run * 10 + focus_rounds_done();
    if (key == shown_key) return;
    shown_key = key;
    const lv_color_t ink = !run && ph != FOCUS_READY ? THEME_DIM : ph == FOCUS_BREAK ? THEME_GREEN : THEME_ACCENT;
    lv_obj_set_style_arc_color(ring, ink, LV_PART_INDICATOR);
    lv_label_set_text(phase_lbl, ph == FOCUS_BREAK ? (focus_long_break() ? "Long break" : "Short break")
                               : ph == FOCUS_WORK ? (run ? "Focus" : "Paused") : "Ready");
    lv_label_set_text(hint_lbl, ph == FOCUS_READY ? "Tap to start"
                              : ph == FOCUS_BREAK ? "Hold to skip the break"
                              : run ? "Tap to pause \xC2\xB7 hold to reset"
                                    : "Tap to resume \xC2\xB7 hold to reset");
    const int done = focus_rounds_done();
    for (int i = 0; i < FOCUS_ROUNDS; i++) {
        const bool current = (i == done && ph == FOCUS_WORK);
        lv_obj_set_style_bg_color(dots[i], i < done || current ? THEME_ACCENT : THEME_BAR_BG, 0);
        lv_obj_set_style_bg_opa(dots[i], current ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
    page_set_footer(footer, status_text());
}

static void build(lv_obj_t* r) {
    root = r;
    shown_time[0] = '\0';
    shown_key = -1;
    const Layout& L = ui_layout();
    const PageArea A = page_area(L);
    page_make_title(root, "Focus");

    const lv_font_t* small = page_with_symbols(page_smaller(L.reset_font, 2));
    const int small_h = lv_font_get_line_height(small);
    const int dot = A.h >= 220 ? 12 : 8;
    const int below = dot + 8 + small_h + 4;
    int d = A.h - below;
    if (d > A.w) d = A.w;
    const int cx = A.x + A.w / 2 - L.w / 2;   // LV_ALIGN_TOP_MID x offset of the area's centre

    ring = page_arc(root, d, LV_MAX(4, LV_MIN(10, d / 24)), 270, 360, RING_RANGE);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, cx, A.y);

    const lv_font_t* clock = L.title_font[0] ? L.title_font[0] : L.pct_font;
    time_lbl = page_label(ring, clock, THEME_TEXT, "");
    lv_obj_align(time_lbl, LV_ALIGN_CENTER, 0, -small_h / 2);
    phase_lbl = page_label(ring, small, THEME_DIM, "");
    lv_obj_align(phase_lbl, LV_ALIGN_CENTER, 0, lv_font_get_line_height(clock) / 2 + 2);

    const int dots_y = A.y + d + 8;
    const int spacing = dot * 2;
    const int x0 = A.x + A.w / 2 - ((FOCUS_ROUNDS - 1) * spacing) / 2 - dot / 2;
    for (int i = 0; i < FOCUS_ROUNDS; i++) {
        dots[i] = page_dot(root, dot, THEME_BAR_BG);
        lv_obj_set_pos(dots[i], x0 + i * spacing, dots_y);
    }
    hint_lbl = page_label(root, small, THEME_DIM, "");
    lv_obj_align(hint_lbl, LV_ALIGN_TOP_MID, cx, dots_y + dot + 4);

    footer = page_make_footer(root);
    apply(millis());
}

static void destroy(void) { root = nullptr; }
static bool available(uint32_t now) { (void)now; return true; }
static bool keep_open(uint32_t now) { (void)now; return focus_running(); }
static void tick(uint32_t now) { apply(now); }
static void tap(void) { focus_toggle(millis()); apply(millis()); }
static void long_press(void) { focus_reset(); apply(millis()); }

const PageDef page_focus = {
    "focus", build, destroy, available, keep_open, tick, tap, long_press,
};
