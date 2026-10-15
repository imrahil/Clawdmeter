// Focus screen (device-local, see focus.h): a ring that empties as the
// session runs, the countdown inside it with Clawd above — at his laptop while
// you focus, riding his cloud on a break — and the round dots underneath.
// Tap starts / pauses; a long press resets (or skips a break).
#include "ui.h"
#include "ui_internal.h"
#include "focus.h"
#include "splash.h"
#include <stdio.h>
#include <string.h>

#define RING_RANGE 1000

static lv_obj_t*   root = nullptr;
static lv_obj_t*   ring;
static lv_obj_t*   time_lbl;
static lv_obj_t*   phase_lbl;
static lv_obj_t*   hint_lbl;
static lv_obj_t*   round_dots[FOCUS_ROUNDS];
static SplashMini* mini = nullptr;
static char        shown_time[12];
static int         shown_key = -1;

static const char* const MOODS[] = { "waving", "laptop", "cloud" };

static void focus_apply(void) {
    if (!root) return;
    const FocusPhase ph = focus_phase();
    const bool run = focus_running();
    const uint32_t left = focus_remaining_ms();
    const uint32_t total = focus_total_ms();

    char buf[12];
    const uint32_t secs = (left + 999) / 1000;   // 24:59 the moment it starts
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
    if (strcmp(buf, shown_time) != 0) {
        strlcpy(shown_time, buf, sizeof(shown_time));
        lv_label_set_text(time_lbl, buf);
        lv_arc_set_value(ring, total ? (int32_t)((uint64_t)left * RING_RANGE / total) : 0);
    }

    // The rest only changes with the phase / run state / round.
    const int key = ph * 100 + run * 10 + focus_rounds_done();
    if (key == shown_key) return;
    shown_key = key;

    const lv_color_t ink = !run && ph != FOCUS_READY ? COL_DIM : ph == FOCUS_BREAK ? COL_GREEN : COL_ACCENT;
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
        lv_obj_set_style_bg_color(round_dots[i], i < done || current ? COL_ACCENT : COL_BAR_BG, 0);
        lv_obj_set_style_bg_opa(round_dots[i], current ? LV_OPA_50 : LV_OPA_COVER, 0);
    }

    splash_mini_set(mini, ph == FOCUS_WORK ? "laptop" : ph == FOCUS_BREAK ? "cloud" : "waving");
}

static void focus_build(lv_obj_t* r) {
    root = r;
    shown_time[0] = '\0';
    shown_key = -1;
    make_screen_title(root, "Focus");

    const int small_h = lv_font_get_line_height(L.f_small);
    const int below = L.dot_px + 10 + small_h + 6;
    int d = content_bottom() - L.content_y - below;
    if (d > L.content_w) d = L.content_w;

    ring = make_arc(root, d, 270, 360, RING_RANGE);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, L.content_y);

    // Countdown a little below the ring's centre, Clawd above it, phase below.
    const int clock_h = lv_font_get_line_height(L.f_clock);
    time_lbl = make_label(ring, L.f_clock, COL_TEXT, "");
    lv_obj_align(time_lbl, LV_ALIGN_CENTER, 0, clock_h / 6);
    phase_lbl = make_label(ring, L.f_small, COL_DIM, "");
    lv_obj_align(phase_lbl, LV_ALIGN_CENTER, 0, clock_h / 6 + clock_h / 2 + small_h);
    mini = splash_mini_new(ring, MOODS, 3, L.mini_cell);
    if (mini) lv_obj_align(splash_mini_obj(mini), LV_ALIGN_BOTTOM_MID, 0, -(d / 2 + clock_h / 2 - clock_h / 6 + 4));

    const int dots_y = L.content_y + d + 8;
    const int spacing = L.dot_px * 2;
    const int x0 = L.scr_w / 2 - ((FOCUS_ROUNDS - 1) * spacing) / 2 - L.dot_px / 2;
    for (int i = 0; i < FOCUS_ROUNDS; i++) {
        round_dots[i] = make_dot(root, L.dot_px, COL_BAR_BG);
        lv_obj_set_pos(round_dots[i], x0 + i * spacing, dots_y);
    }
    hint_lbl = make_label(root, L.f_small, COL_DIM, "");
    lv_obj_align(hint_lbl, LV_ALIGN_TOP_MID, 0, dots_y + L.dot_px + 6);

    focus_apply();
}

static void focus_destroy(void) {
    splash_mini_free(mini);
    mini = nullptr;
    root = nullptr;
}

static bool focus_available(uint32_t now) { (void)now; return true; }

// The carousel only passes through while a session or break is running.
static bool focus_in_carousel(uint32_t now) {
    (void)now;
    return focus_running();
}

static const char* focus_status(uint32_t now) {
    (void)now;
    switch (focus_phase()) {
    case FOCUS_WORK:  return focus_running() ? "Focusing" : "Paused";
    case FOCUS_BREAK: return "On a break";
    default:          return "Ready to focus";
    }
}

static void focus_screen_tick(uint32_t now) {
    (void)now;
    focus_apply();
    // Paused: Clawd holds still too.
    if (focus_running() || focus_phase() == FOCUS_READY) splash_mini_step(mini);
}

static void focus_tap(void) {
    focus_toggle();
    focus_apply();
}

static void focus_long(void) {
    focus_reset();
    focus_apply();
}

const ScreenDef focus_screen = {
    focus_build, focus_destroy, focus_available, focus_in_carousel,
    focus_status, focus_screen_tick, focus_tap, focus_long,
};
