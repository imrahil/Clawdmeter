#pragma once
#include <lvgl.h>

// A cheap refresh instrument for the `perf` serial command. While a window
// is open (the stage curtain rising or dropping, Clawd performing), every LVGL
// refresh that sends pixels to the panel is counted: how long it took
// (render + flush, since the flush is synchronous), how much of that was the
// flush itself, how many pixels went out, and the longest gap between two
// such frames (the visible stutter). `perf` prints the last run of each.

enum PerfWindow { PERF_STAGE_RISE, PERF_STAGE_DROP, PERF_PERFORM, PERF_WINDOW_COUNT };

void perf_attach(lv_display_t* disp, uint32_t refr_period_ms);   // once, after the display exists
void perf_begin(PerfWindow w);          // closes any other open window first
void perf_end(PerfWindow w);            // no-op unless w is the open window
void perf_print(void);                  // the `perf` serial command
