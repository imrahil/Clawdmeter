#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "data.h"
#include "ui.h"
#include "ble.h"
#include "splash.h"
#include "usage_rate.h"
#include "idle.h"
#include "idle_cfg.h"
#include "brightness.h"
#include "focus.h"

#include "hal/board_caps.h"
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include "hal/input_hal.h"
#include "hal/power_hal.h"
#include "hal/imu_hal.h"
#include "hal/sound_hal.h"

static UsageData usage = {};

// ---- LVGL draw buffers (partial render mode) ----
// PSRAM-equipped boards (S3) can comfortably hold larger strips. PSRAM-free
// boards (e.g. ESP32-C6) allocate from internal SRAM, so we shrink the strip
// — 480×20 RGB565 = 19 KB × 2 buffers = 38 KB, fits beside everything else.
#ifdef BOARD_HAS_PSRAM
#define BUF_LINES 40
#define LV_BUF_CAPS (MALLOC_CAP_SPIRAM)
#else
#define BUF_LINES 20
#define LV_BUF_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#endif
static uint16_t* buf1 = nullptr;
static uint16_t* buf2 = nullptr;

static uint32_t my_tick(void) { return millis(); }

static void my_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    display_hal_draw_bitmap(area->x1, area->y1, w, h, (uint16_t*)px_map);
    lv_display_flush_ready(disp);
}

static void rounder_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    display_hal_round_area(&area->x1, &area->y1, &area->x2, &area->y2);
}

// Touch policy is driven by IDLE_WAKE_ON_TOUCH:
//   true  → a press edge while asleep wakes the device and the first touch is
//           swallowed (mirrors the button wake-consumption); a press while
//           awake counts as activity.
//   false → touch never counts as activity and is fully swallowed while the
//           panel is dark, so pets/sleeves can't wake it overnight and LVGL
//           can't quietly toggle splash<->usage on a black panel.
static void my_touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    uint16_t x, y;
    bool pressed;
    touch_hal_read(&x, &y, &pressed);
    const bool raw_pressed = pressed;

    if (IDLE_WAKE_ON_TOUCH) {
        static bool touch_was = false;
        static bool touch_wake_swallowed = false;
        if (raw_pressed && !touch_was) {
            // Press edge — consume as wake if asleep.
            if (idle_consume_wake_press()) {
                touch_wake_swallowed = true;
                pressed = false;
            }
        } else if (!raw_pressed && touch_was) {
            // Release edge.
            if (touch_wake_swallowed) {
                touch_wake_swallowed = false;
                pressed = false;
            }
        } else if (raw_pressed && touch_wake_swallowed) {
            // Held finger through wake — keep hiding until release.
            pressed = false;
        }
        touch_was = raw_pressed;
    } else if (idle_is_asleep()) {
        pressed = false;
    }

    if (pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Parse a usage payload (untagged JSON object) into UsageData.
static void parse_usage(JsonDocument& doc, UsageData* out) {
    out->session_pct = doc["s"] | 0.0f;
    out->session_reset_mins = doc["sr"] | -1;
    out->weekly_pct = doc["w"] | 0.0f;
    out->weekly_reset_mins = doc["wr"] | -1;
    strlcpy(out->status, doc["st"] | "unknown", sizeof(out->status));
    // as<bool>, not `| false`: daemons send 1, and `|` only accepts a JSON bool.
    out->chime = doc["c"].as<bool>();   // absent (old daemon / chime off) → stay silent
    out->beep = doc["b"].as<bool>();
    strlcpy(out->alert_msg, doc["m"] | "", sizeof(out->alert_msg));
    out->alert_clear = doc["x"].as<bool>();
    const char* acct = doc["acct"] | "pro";
    out->enterprise = (strcmp(acct, "ent") == 0);
    out->time_pct = doc["tp"] | 0;
    out->period_days = doc["pd"] | 30;
    strlcpy(out->reset_date, doc["rd"] | "", sizeof(out->reset_date));
    out->clock_epoch = doc["t"] | 0L;
    out->clock_fmt = doc["tf"] | 24;
    out->ok = doc["ok"] | false;
    out->valid = true;
}

// {"k":"wx"} — weather screen payload (daemon/extras.py weather_payload).
static void parse_weather(JsonDocument& doc, WeatherData* out) {
    strlcpy(out->location, doc["loc"] | "", sizeof(out->location));
    out->temp   = doc["t"] | 0.0f;
    out->feels  = doc["fl"] | 0;
    out->code   = doc["c"] | 0;
    out->is_day = doc["d"].as<bool>();
    out->hi     = doc["hi"] | 0;
    out->lo     = doc["lo"] | 0;
    strlcpy(out->sun, doc["sun"] | "", sizeof(out->sun));
    out->wind = doc["ws"] | -1;
    out->uv   = doc["uv"] | -1;
    out->aqi  = doc["aq"] | -1;
    strlcpy(out->rain, doc["rn"] | "", sizeof(out->rain));
    JsonArray f = doc["f"].as<JsonArray>();
    out->n_days = 0;
    for (JsonArray day : f) {
        if (out->n_days >= WX_FORECAST_DAYS) break;
        WeatherDay* d = &out->days[out->n_days++];
        strlcpy(d->day, day[0] | "", sizeof(d->day));
        d->code = day[1] | 0;
        d->hi   = day[2] | 0;
        d->lo   = day[3] | 0;
    }
}

// {"k":"q"} — one stock row (daemon/extras.py quote_payload).
static bool parse_quote(JsonDocument& doc, StockQuote* out) {
    out->index = doc["i"] | -1;
    out->count = doc["n"] | 0;
    if (out->index < 0 || out->index >= STOCK_MAX) return false;
    strlcpy(out->symbol, doc["sy"] | "", sizeof(out->symbol));
    strlcpy(out->name, doc["nm"] | "", sizeof(out->name));
    strlcpy(out->currency, doc["cu"] | "", sizeof(out->currency));
    out->price      = doc["p"] | 0.0f;
    out->prev_close = doc["pc"] | 0.0f;
    out->open       = doc["o"].as<bool>();
    out->baseline   = doc["b"] | 0;
    int fr = doc["fr"] | 100;            // absent (closed / older daemon) = full width
    out->session_pct = (uint8_t)(fr < 1 ? 1 : fr > 100 ? 100 : fr);
    JsonArray h = doc["h"].as<JsonArray>();
    out->n_hist = 0;
    for (JsonVariant v : h) {
        if (out->n_hist >= STOCK_HIST_MAX) break;
        int val = v | 0;
        out->hist[out->n_hist++] = (uint8_t)(val < 0 ? 0 : val > 99 ? 99 : val);
    }
    return true;
}

// {"k":"h"} — pace (daemon/extras.py history_payload).
static void parse_pace(JsonDocument& doc, PaceData* out) {
    out->n_sess = 0;
    for (JsonVariant v : doc["sh"].as<JsonArray>()) {
        if (out->n_sess >= PACE_SESS_MAX) break;
        int val = v | 0;
        out->sess[out->n_sess++] = (uint8_t)(val < 0 ? 0 : val > 100 ? 100 : val);
    }
    out->frac = doc["fr"] | 100;
    out->proj = doc["pr"] | -1;
    out->zone = doc["pz"] | 0;
    strlcpy(out->eta, doc["eta"] | "", sizeof(out->eta));
    out->n_days = 0;
    for (JsonArray day : doc["wd"].as<JsonArray>()) {
        if (out->n_days >= PACE_DAYS) break;
        PaceDay* d = &out->days[out->n_days++];
        strlcpy(d->day, day[0] | "", sizeof(d->day));
        d->pct = day[1] | -1;
    }
    out->week_proj = doc["wp"] | -1;
}

// {"k":"a"} — Claude Code activity (daemon/extras.py activity_payload).
static void parse_activity(JsonDocument& doc, ActivityData* out) {
    out->n_rows = 0;
    for (JsonArray row : doc["ss"].as<JsonArray>()) {
        if (out->n_rows >= ACT_ROWS_MAX) break;
        ActivityRow* r = &out->rows[out->n_rows++];
        strlcpy(r->name, row[0] | "", sizeof(r->name));
        r->state = row[1] | 0;
        strlcpy(r->label, row[2] | "", sizeof(r->label));
        r->age_min = row[3] | 0;
    }
    out->live = doc["nl"] | out->n_rows;
    out->tokens = doc["tk"] | 0.0;
    out->cost_cents = doc["co"] | 0L;
    out->sessions = doc["ns"] | 0;
}

// {"k":"sys"} — host stats (daemon/extras.py system_payload).
static void parse_system(JsonDocument& doc, SystemData* out) {
    strlcpy(out->host, doc["hn"] | "", sizeof(out->host));
    out->cpu = doc["cpu"] | 0;
    strlcpy(out->load, doc["ld"] | "", sizeof(out->load));
    out->mem = doc["mem"] | 0;
    strlcpy(out->mem_text, doc["mu"] | "", sizeof(out->mem_text));
    out->temp = doc["tmp"] | -1;
    out->disk = doc["dsk"] | 0;
    strlcpy(out->disk_text, doc["du"] | "", sizeof(out->disk_text));
    strlcpy(out->uptime, doc["up"] | "", sizeof(out->uptime));
}

// {"k":"ss"} — status.claude.com (daemon/extras.py status_payload).
static void parse_service_status(JsonDocument& doc, ServiceStatus* out) {
    const char* ind = doc["i"] | "none";
    out->level = strcmp(ind, "major") == 0 || strcmp(ind, "critical") == 0 ? 2
               : strcmp(ind, "none") == 0 ? 0 : 1;
    strlcpy(out->msg, doc["m"] | "", sizeof(out->msg));
}

// ---- Serial command buffer ----
#define CMD_BUF_SIZE 64
static char cmd_buf[CMD_BUF_SIZE];
static int cmd_pos = 0;

static void send_screenshot() {
#ifndef BOARD_HAS_PSRAM
    // A full RGB565 framebuffer doesn't fit in internal SRAM on PSRAM-free
    // boards (e.g. 480×480×2 = 460 KB). Capture is unsupported there.
    Serial.println("SCREENSHOT_UNSUPPORTED");
    return;
#else
    const uint32_t w = board_caps().width;
    const uint32_t h = board_caps().height;
    const uint32_t row_bytes = w * 2;
    const uint32_t buf_size = row_bytes * h;
    uint8_t* sbuf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!sbuf) {
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    lv_draw_buf_t draw_buf;
    lv_draw_buf_init(&draw_buf, w, h, LV_COLOR_FORMAT_RGB565, row_bytes, sbuf, buf_size);

    lv_result_t res = lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565, &draw_buf);
    if (res != LV_RESULT_OK) {
        heap_caps_free(sbuf);
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    Serial.printf("SCREENSHOT_START %lu %lu %lu\n",
        (unsigned long)w, (unsigned long)h, (unsigned long)buf_size);
    Serial.flush();
    Serial.write(sbuf, buf_size);
    Serial.flush();
    Serial.println();
    Serial.println("SCREENSHOT_END");
    heap_caps_free(sbuf);
#endif
}

static void check_serial_cmd() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_pos] = '\0';
            if (strcmp(cmd_buf, "screenshot") == 0) send_screenshot();
            else if (strcmp(cmd_buf, "buzz") == 0)  sound_hal_play_reset();
            cmd_pos = 0;
        } else if (cmd_pos < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_pos++] = c;
        }
    }
}

// Each board provides this. Must bring up the shared I2C bus (Wire.begin
// with the board's SDA/SCL pins) and any board-private hardware that has
// to settle before display/touch (e.g. an IO expander gating the LCD
// reset line). Called exactly once at the start of setup().
extern "C" void board_init(void);

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("{\"ready\":true}");

    board_init();

    display_hal_init();
    display_hal_begin();
    idle_init();        // takes over panel brightness and starts the idle timer
    brightness_init();  // load the user's saved brightness level and apply via idle

    power_hal_init();
    imu_hal_init();
    sound_hal_init();
    touch_hal_init();

    // ---- LVGL ----
    const int W = board_caps().width;
    const int H = board_caps().height;

    lv_init();
    lv_tick_set_cb(my_tick);

    buf1 = (uint16_t*)heap_caps_malloc(W * BUF_LINES * 2, LV_BUF_CAPS);
    buf2 = (uint16_t*)heap_caps_malloc(W * BUF_LINES * 2, LV_BUF_CAPS);

    lv_display_t* disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, W * BUF_LINES * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touch_cb);

    ble_init();
    input_hal_init();

    ui_init();
    ui_update_ble_status(ble_get_state(), ble_get_device_name(), ble_get_mac_address());
    ui_update_battery(power_hal_battery_pct(), power_hal_is_charging(), power_hal_is_vbus_in());
    ui_show_screen(SCREEN_SPLASH);

    Serial.printf("Dashboard ready (%s, %dx%d), waiting for data on BLE...\n",
        board_caps().name, W, H);
}

static ble_state_t last_ble_state = BLE_STATE_INIT;

// ---- +/- buttons ----
// A short press navigates; holding past NAV_HOLD_MS sends the button's HID key
// instead, held until release (BOOT: Space for voice-mode push-to-talk, KEY:
// Shift+Tab mode toggle). The first press from sleep only wakes the panel.
#define NAV_HOLD_MS 400

struct NavButton {
    bool     was = false;
    bool     swallowed = false;   // this press woke the panel
    bool     hid = false;         // held long enough: the HID key is down
    uint32_t down_ms = 0;
};

static void nav_button_tick(NavButton& b, bool held, void (*on_short)(void),
                            uint8_t hid_key, uint8_t hid_mods) {
    const uint32_t now = millis();
    if (held && !b.was) {
        b.swallowed = idle_consume_wake_press();
        b.hid = false;
        b.down_ms = now;
    } else if (held && !b.swallowed && !b.hid && now - b.down_ms >= NAV_HOLD_MS) {
        b.hid = true;
        ble_keyboard_press(hid_key, hid_mods);
    } else if (!held && b.was) {
        if (b.hid)              ble_keyboard_release();
        else if (!b.swallowed)  on_short();
        b.swallowed = b.hid = false;
    }
    b.was = held;
}

// Hold-to-pair gesture: hold the PWR button ~3s, then RELEASE → clear all BLE
// bonds and re-advertise. Clearing on *release* (not while held) is deliberate:
// holding to power the device OFF (AXP hardware shutdown at 8s) must not wipe
// the bond — a power-off hold never releases before shutdown. To stop a
// "chicken-out" release just before 8s from pairing, the gesture disarms at 6s.
//
//   ~1.5s long-press edge → PENDING
//   3.0s (+1500)          → ARMED   (release from here clears bonds)
//   6.0s (+4500)          → DISARMED (no clear; AXP powers off at 8s)
#define PAIR_ARM_AFTER_LONG_MS    1500   // 3.0s total
#define PAIR_DISARM_AFTER_LONG_MS 4500   // 6.0s total
enum pair_state_t { PAIR_IDLE, PAIR_PENDING, PAIR_ARMED };
static pair_state_t pair_state        = PAIR_IDLE;
static uint32_t     pair_long_seen_ms = 0;

static void pair_tick(void) {
    if (pair_state == PAIR_IDLE && power_hal_pwr_long_pressed()) {
        pair_state = PAIR_PENDING;
        pair_long_seen_ms = millis();
        (void)power_hal_pwr_released();  // drain any stale release edge
        Serial.println("PWR long-press: hold to ~3s then release to pair");
        return;
    }
    if (pair_state == PAIR_IDLE) return;

    if (power_hal_pwr_released()) {
        if (pair_state == PAIR_ARMED) {
            Serial.println("Pair: released in window — clearing bonds, advertising");
            ble_clear_bonds();
        } else {
            Serial.println("Pair: released too early — cancelled");
        }
        pair_state = PAIR_IDLE;
        return;
    }

    uint32_t held = millis() - pair_long_seen_ms;
    if (pair_state == PAIR_PENDING && held >= PAIR_ARM_AFTER_LONG_MS) {
        pair_state = PAIR_ARMED;
        Serial.println("Pair: armed — release to pair");
    } else if (pair_state == PAIR_ARMED && held >= PAIR_DISARM_AFTER_LONG_MS) {
        pair_state = PAIR_IDLE;  // power-off territory; don't pair
        Serial.println("Pair: disarmed (holding toward power-off)");
    }
}

void loop() {
    idle_tick();
    lv_timer_handler();
    ui_tick_anim();
    ble_tick();
    power_hal_tick();
    imu_hal_tick();
    sound_hal_tick();
    splash_tick();
    splash_mascot_tick();
    // Rotation transition (blank + ramp) would fight the idle fade — skip
    // ticks while the panel is dark. A rotation that happens during sleep
    // is detected by the next tick after wake and ramped in then.
    if (!idle_is_asleep()) display_hal_tick();

    // ---- Physical buttons ----
    //   PRIMARY   short: previous screen (-)  hold: HID Space (Claude Code voice-mode PTT)
    //   SECONDARY short: next screen (+)      hold: HID Shift+Tab (mode toggle)
    //   One-button boards: PRIMARY short = next screen.
    //   PWR       → on splash: cycle animations; elsewhere: cycle brightness;
    //               hold ~3s + release: pairing mode
    // First press from sleep is consumed as a wake-only event by
    // idle_consume_wake_press(); the normal action fires from the second
    // press. Activity bookkeeping happens inside idle_consume_wake_press
    // so no separate idle_note_activity() call is needed here.
    {
        const bool two = board_caps().button_count >= 2;
        static NavButton primary, secondary;
        nav_button_tick(primary, input_hal_is_held(INPUT_BTN_PRIMARY),
                        two ? ui_prev_screen : ui_next_screen, 0x2C, 0);   // HID Space, no mods
        if (two) {
            nav_button_tick(secondary, input_hal_is_held(INPUT_BTN_SECONDARY),
                            ui_next_screen, 0x2B, 0x02);                   // HID Tab + LEFT_SHIFT
        }

        if (power_hal_pwr_pressed()) {
            if (!idle_consume_wake_press()) {
                // On splash: cycle animations. Elsewhere: cycle screen
                // brightness (the +/- buttons move between screens).
                if (ui_get_current_screen() == SCREEN_SPLASH) splash_next();
                else                                          brightness_cycle();
            }
        }

        pair_tick();
    }

    ble_state_t bs = ble_get_state();
    if (bs != last_ble_state) {
        last_ble_state = bs;
        ui_update_ble_status(bs, ble_get_device_name(), ble_get_mac_address());
    }

    static int  last_pct      = -2;
    static bool last_charging = false;
    static bool last_vbus     = false;
    int  pct      = power_hal_battery_pct();
    bool charging = power_hal_is_charging();
    bool vbus     = power_hal_is_vbus_in();
    if (pct != last_pct || charging != last_charging || vbus != last_vbus) {
        if (pct != last_pct) ble_set_battery_level(pct);
        last_pct = pct;
        last_charging = charging;
        last_vbus = vbus;
        ui_update_battery(pct, charging, vbus);
    }

    // Focus timer: a session or break just ended → chime, wake, show it.
    if (focus_tick()) {
        Serial.println("focus phase done — chime");
        sound_hal_play_reset();
        idle_note_activity();
        ui_focus_finished();
    }

    check_serial_cmd();

    if (ble_has_data()) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, ble_get_data());
        const char* kind = err ? "" : (doc["k"] | "");
        if (err) {
            Serial.printf("JSON parse error: %s\n", err.c_str());
            ble_send_nack();
        } else if (strcmp(kind, "wx") == 0) {
            static WeatherData weather;
            parse_weather(doc, &weather);
            ui_update_weather(&weather);
            ble_send_ack();
        } else if (strcmp(kind, "h") == 0) {
            static PaceData pace;
            parse_pace(doc, &pace);
            ui_update_pace(&pace);
            ble_send_ack();
        } else if (strcmp(kind, "a") == 0) {
            static ActivityData act;
            parse_activity(doc, &act);
            ui_update_activity(&act);
            ble_send_ack();
        } else if (strcmp(kind, "sys") == 0) {
            static SystemData sys;
            parse_system(doc, &sys);
            ui_update_system(&sys);
            ble_send_ack();
        } else if (strcmp(kind, "ss") == 0) {
            ServiceStatus st;
            parse_service_status(doc, &st);
            ui_update_service_status(&st);
            ble_send_ack();
        } else if (strcmp(kind, "q") == 0) {
            static StockQuote quote;
            if (parse_quote(doc, &quote)) {
                ui_update_stock(&quote);
                ble_send_ack();
            } else {
                ble_send_nack();
            }
        } else {
            parse_usage(doc, &usage);
            int g_before = usage_rate_group();
            bool session_reset = usage_rate_sample(usage.session_pct);
            int g_after = usage_rate_group();
            // 5-hour session limit refilled → chime so the user knows they can
            // use Claude again (no-op on boards without a buzzer). Gated on the
            // daemon's opt-in `chime` config; the `buzz` serial cmd ignores it.
            if (session_reset && usage.chime) {
                Serial.println("session reset detected — chime");
                sound_hal_play_reset();
            }
            if (session_reset) splash_celebrate();   // Clawd jumps for joy on the splash
            // Claude Code hook fired on the host. Not gated on `chime`: the daemon
            // only sets `b` when that hook ran.
            if (usage.beep) {
                Serial.println("attention — chime");
                sound_hal_play_reset();
            }
            if (usage.alert_msg[0]) {
                Serial.println("attention — mascot");
                idle_note_activity();
                ui_show_alert(usage.alert_msg);
            }
            if (usage.alert_clear) ui_hide_alert();
            if (g_after != g_before) {
                Serial.printf("usage rate: group %d -> %d (s=%.2f%%)\n",
                    g_before, g_after, usage.session_pct);
                if (splash_is_active()) splash_pick_for_current_rate();
            }
            ui_update(&usage);
            ble_send_ack();
        }
    }

    delay(5);
}
