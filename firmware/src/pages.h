#pragma once
#include <stdint.h>
#include "weather.h"
#include "feeds.h"
#include "data.h"

// Pages: full views that take the meters' place (title, cards, status line)
// while the corner Clawd and the battery stay where they are. The meters are
// page 0; the others are reached with the side buttons:
//
//   meters → Pace → Activity → Focus → System → Weather → Stocks → meters
//
// A page whose data hasn't come (or went stale) is skipped; Focus is always
// there. A page is built when it opens and deleted when it closes, so only
// one is ever in LVGL's memory (the C6 has no PSRAM to grow it). While one
// is up the director holds Clawd in his seat (director_set_covered) and taps
// are the page's: on Focus a tap starts or pauses and a long press resets;
// on the others a tap turns the carousel on or off.
//
// Carousel: every PAGE_CAROUSEL_MS it steps to the next page (meters
// included; Focus only while it runs). It waits while an alert is up, the
// panel sleeps, or Clawd is busy on the meters (director_settled). The
// setting is kept in NVS. With the carousel off a page left alone 2 min
// goes back to the meters (Focus not while it runs). An alert always closes
// the page first.

enum PageId {
    PAGE_METERS, PAGE_PACE, PAGE_ACTIVITY, PAGE_FOCUS, PAGE_SYSTEM, PAGE_WEATHER, PAGE_STOCKS,
    PAGE_COUNT,
};

#define PAGE_CAROUSEL_MS (30UL * 1000)

void   pages_init(void);            // after ui_init() and director_init(); loads the carousel setting
void   pages_tick(void);            // every loop
void   pages_next(void);            // the + button
void   pages_prev(void);            // the - button
void   pages_show(PageId page);     // PAGE_METERS closes whatever is up
PageId pages_current(void);
const char* pages_name(PageId page);        // "meters", "pace", …
PageId pages_by_name(const char* name);     // PAGE_COUNT if unknown
void   pages_set_carousel(bool on);         // saved; shows a toast
bool   pages_carousel(void);

// The app's writes for the pages (weather.h, feeds.h).
void pages_update_weather(const WeatherData* data);
void pages_update_pace(const PaceData* data);
void pages_update_activity(const ActivityData* data);
void pages_update_system(const SystemData* data);
void pages_update_stock(const StockQuote* row);
// Every usage payload (Pace shows the live session numbers).
void pages_update_usage(const UsageData* data);

// A focus session or a break just ended: show the Focus page (unless an
// alert is up).
void pages_focus_finished(void);
