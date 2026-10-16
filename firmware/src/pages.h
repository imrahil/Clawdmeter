#pragma once
#include <stdint.h>
#include "weather.h"

// Pages: full views that take the meters' place (title, cards, status line)
// while the corner Clawd and the battery stay where they are. The meters are
// page 0; the others are reached with the side buttons:
//
//   meters → Weather (once the app has sent it) → Focus → meters
//
// A page is built when it opens and deleted when it closes, so only one is
// ever in LVGL's memory (the C6 has no PSRAM to grow it). While one is up the
// director holds Clawd in his seat (director_set_covered) and taps are the
// page's: on Focus a tap starts or pauses, a long press resets. A page goes
// back to the meters after PAGE_IDLE_MS without a touch or button (Focus not
// while it runs), and an alert always closes it first.

enum PageId { PAGE_METERS, PAGE_WEATHER, PAGE_FOCUS, PAGE_COUNT };

void   pages_init(void);            // after ui_init() and director_init()
void   pages_tick(void);            // every loop
void   pages_next(void);            // the + button
void   pages_prev(void);            // the - button
void   pages_show(PageId page);     // PAGE_METERS closes whatever is up
PageId pages_current(void);

// A `{"k":"wx"}` write landed.
void pages_update_weather(const WeatherData* data);

// A focus session or a break just ended: show the Focus page (unless an
// alert is up).
void pages_focus_finished(void);
