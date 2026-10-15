#pragma once
#include <stdint.h>

// Device-local focus timer (Pomodoro): 25 min of focus, then a 5 min break —
// 15 min after every fourth. A break starts on its own when focus ends; the
// next focus waits for a tap. No daemon involved, so it works offline.
enum FocusPhase { FOCUS_READY, FOCUS_WORK, FOCUS_BREAK };

#define FOCUS_ROUNDS 4

void       focus_toggle(void);        // start / pause / resume
void       focus_reset(void);         // back to ready (from a break: skip it)
bool       focus_tick(void);          // true once when a focus or break ends
FocusPhase focus_phase(void);
bool       focus_running(void);
bool       focus_long_break(void);    // the current break is the long one
uint32_t   focus_remaining_ms(void);
uint32_t   focus_total_ms(void);      // length of the current (or next) phase
int        focus_rounds_done(void);   // focus sessions finished this cycle, 0..FOCUS_ROUNDS
