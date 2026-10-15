#pragma once

// Native desktop simulator "board" — an SDL2 window standing in for the
// 480×480 AMOLED so shared code (main.cpp, ui.cpp, splash.cpp) can be
// developed without hardware. No pins here, just geometry and the key map.
//
//   mouse / left-drag    touch (tap: the screen's action, or pause the carousel)
//   space                play/pause scenario playback
//   left / right         step one scenario state (pauses playback)
//   1..9                 jump to scenario state N (pauses playback)
//   d                    toggle BLE connected/disconnected
//   b                    PRIMARY button: previous screen (hold: HID Space PTT on hardware)
//   n                    SECONDARY button: next screen (hold: HID Shift+Tab on hardware)
//   p                    PWR button (short press; hold ~3s + release = pair)
//   c                    toggle charging       - / =   battery down / up 5%
//   u                    toggle no battery (USB power only → plug-zap icon)
//   s                    save screenshot BMP to the current directory
//   esc / window close   quit
//
// Scenario: sim/scenario.jsonl (relative to the firmware/ dir), overridable
// with SIM_SCENARIO=<path>. One JSON object per line — the daemon payload
// plus optional "name" and "hold_ms" (default 3000). Lines starting with #
// are comments. Missing file → a small built-in state list.
//
// Headless / CI: SDL_VIDEODRIVER=dummy SIM_AUTOSHOT_MS=<ms> saves a
// screenshot (SIM_AUTOSHOT_PATH, default sim-autoshot.bmp) after <ms> and
// exits. SIM_SCRIPT="<ms>:<key>,..." presses keys on a timeline (see
// sim_platform.h), e.g. SIM_SCRIPT=1500:n,1800:n to land on the third screen.

// Other panel sizes, to check the compact / small layouts:
//   PLATFORMIO_BUILD_FLAGS="-DLCD_WIDTH=368 -DLCD_HEIGHT=448" pio run -e sim
#define BOARD_NAME  "Simulator"
#ifndef LCD_WIDTH
#define LCD_WIDTH   480
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT  480
#endif
