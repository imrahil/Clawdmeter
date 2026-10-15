#pragma once
#include "data.h"
#include "ble.h"

// Screen order for the +/- buttons and the carousel. Daemon-fed screens
// (pace, activity, system, weather, stocks) are skipped until their first
// payload lands and again once it goes stale; Focus is always reachable by
// button, and joins the carousel only while a session or break is running.
enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_PACE,
    SCREEN_ACTIVITY,
    SCREEN_FOCUS,
    SCREEN_SYSTEM,
    SCREEN_WEATHER,
    SCREEN_STOCKS,
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_next_screen(void);       // manual: + button
void ui_prev_screen(void);       // manual: - button
void ui_update_weather(const WeatherData* data);
void ui_update_stock(const StockQuote* quote);
void ui_update_pace(const PaceData* data);
void ui_update_activity(const ActivityData* data);
void ui_update_system(const SystemData* data);
void ui_update_service_status(const ServiceStatus* status);
// A focus session or break just ended: wake to the Focus screen.
void ui_focus_finished(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(ble_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging, bool usb_power);  // percent < 0 = no battery
// Attention: mascot walk-on plus the message on the status line. Ends on a
// tap, after 2 minutes, or ui_hide_alert() when the session moves on.
void ui_show_alert(const char* message);
void ui_hide_alert(void);
