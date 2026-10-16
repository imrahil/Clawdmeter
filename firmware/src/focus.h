#pragma once
#include <stdint.h>

// Device-local focus timer (Pomodoro): 25 min of focus, then a 5 min break —
// 15 min after every fourth. A break starts on its own when focus ends; the
// next focus waits for a tap. No app involved, so it works offline.
// Pure: every call takes the time, so it runs in the native tests.

enum FocusPhase { FOCUS_READY, FOCUS_WORK, FOCUS_BREAK };

#define FOCUS_ROUNDS         4
#define FOCUS_WORK_MS        (25UL * 60 * 1000)
#define FOCUS_SHORT_BREAK_MS (5UL * 60 * 1000)
#define FOCUS_LONG_BREAK_MS  (15UL * 60 * 1000)

void       focus_toggle(uint32_t now);        // start / pause / resume
void       focus_reset(void);                 // back to ready (from a break: skip it); when ready, clear the rounds
bool       focus_tick(uint32_t now);          // true once when a focus or break ends
FocusPhase focus_phase(void);
bool       focus_running(void);
bool       focus_long_break(void);            // the current break is the long one
uint32_t   focus_remaining_ms(uint32_t now);
uint32_t   focus_total_ms(void);              // length of the current (or next) phase
int        focus_rounds_done(void);           // focus sessions finished this cycle, 0..FOCUS_ROUNDS
