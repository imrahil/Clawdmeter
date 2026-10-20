#include "stage.h"
#include "field.h"
#include "perf.h"
#include "theme.h"
#include "ui.h"
#include "hal/board_caps.h"
#include <Arduino.h>

#define RISE_MS 450
#define DROP_MS 380

enum StageState { S_DOWN, S_RISING, S_UP, S_DROPPING };

static Field      field;
static bool       ready = false;
static StageState st = S_DOWN;
static uint32_t   t0 = 0;
static int        W = 480, H = 480;
static bool       meters_hidden = false;   // by the stage, under the black

// Ease-out (rise: decelerates into place) and ease-in (drop: gets going).
static int32_t ease_out(int32_t p) { const int32_t q = 1000 - p; return 1000 - q * q / 1000 * q / 1000; }
static int32_t ease_in(int32_t p)  { return p * p / 1000 * p / 1000; }

// Top edge of the curtain: H = nothing up, 0 = all of it.
static void set_top(int top) {
    if (top >= H) { field_clear(&field); return; }
    if (top < 0) top = 0;
    field_set(&field, 0, top, W - 1, H - 1);
}

static void hide_meters_quietly(void* on) { ui_set_meters_hidden(on != nullptr); }

static void meters(bool hidden) {
    if (hidden == meters_hidden) return;
    meters_hidden = hidden;
    // Under the full curtain no pixel changes: skip the redraw.
    field_quietly(field.obj, hide_meters_quietly, hidden ? (void*)1 : nullptr);
}

void stage_init(lv_obj_t* parent) {
    const BoardCaps& c = board_caps();
    W = c.width;
    H = c.height;
    field_init(&field, parent, W, H, THEME_BG);
    ready = true;
}

void stage_rise(void) {
    if (!ready || st == S_UP || st == S_RISING) return;
    if (st == S_DOWN) field_show(&field);
    st = S_RISING;
    t0 = millis();
    perf_begin(PERF_STAGE_RISE);
}

void stage_drop(void) {
    if (!ready || st == S_DOWN || st == S_DROPPING) return;
    meters(false);                       // back underneath before any of it shows
    st = S_DROPPING;
    t0 = millis();
    perf_begin(PERF_STAGE_DROP);
}

void stage_hide_now(void) {
    if (!ready) return;
    meters(false);
    field_hide(&field);
    st = S_DOWN;
}

bool stage_visible(void) { return st != S_DOWN; }
bool stage_moving(void)  { return st == S_RISING || st == S_DROPPING; }

void stage_tick(void) {
    if (!ready || !stage_moving()) return;
    const uint32_t el = millis() - t0;
    if (st == S_RISING) {
        if (el >= RISE_MS) {
            set_top(0);
            st = S_UP;
            meters(true);
            perf_end(PERF_STAGE_RISE);
            return;
        }
        set_top(H - (int)(H * ease_out((int32_t)(el * 1000 / RISE_MS)) / 1000));
    } else {
        if (el >= DROP_MS) {
            field_hide(&field);
            st = S_DOWN;
            perf_end(PERF_STAGE_DROP);
            return;
        }
        set_top((int)(H * ease_in((int32_t)(el * 1000 / DROP_MS)) / 1000));
    }
}
