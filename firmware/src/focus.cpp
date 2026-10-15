#include "focus.h"
#include <Arduino.h>

#define WORK_MS        (25UL * 60 * 1000)
#define SHORT_BREAK_MS (5UL * 60 * 1000)
#define LONG_BREAK_MS  (15UL * 60 * 1000)

static FocusPhase phase = FOCUS_READY;
static bool       running = false;
static uint32_t   total_ms = WORK_MS;
static uint32_t   remaining_ms = WORK_MS;   // while paused / ready
static uint32_t   end_ms = 0;               // while running
static int        finished = 0;             // focus sessions completed overall

static void enter(FocusPhase p, uint32_t len, bool run) {
    phase = p;
    total_ms = remaining_ms = len;
    running = run;
    end_ms = millis() + len;
}

uint32_t focus_remaining_ms(void) {
    if (!running) return remaining_ms;
    const int32_t left = (int32_t)(end_ms - millis());
    return left > 0 ? (uint32_t)left : 0;
}

void focus_toggle(void) {
    if (running) {
        remaining_ms = focus_remaining_ms();
        running = false;
    } else {
        if (phase == FOCUS_READY) phase = FOCUS_WORK;
        end_ms = millis() + remaining_ms;
        running = true;
    }
}

void focus_reset(void) {
    enter(FOCUS_READY, WORK_MS, false);
}

bool focus_tick(void) {
    if (!running || focus_remaining_ms() > 0) return false;
    if (phase == FOCUS_WORK) {
        finished++;
        enter(FOCUS_BREAK, finished % FOCUS_ROUNDS == 0 ? LONG_BREAK_MS : SHORT_BREAK_MS, true);
    } else {
        enter(FOCUS_READY, WORK_MS, false);
    }
    return true;
}

FocusPhase focus_phase(void)   { return phase; }
bool       focus_running(void) { return running; }
bool       focus_long_break(void) { return phase == FOCUS_BREAK && total_ms == LONG_BREAK_MS; }
uint32_t   focus_total_ms(void) { return total_ms; }

int focus_rounds_done(void) {
    // During the long break the cycle shows full; it rolls over when focus resumes.
    if (phase == FOCUS_BREAK && finished > 0 && finished % FOCUS_ROUNDS == 0) return FOCUS_ROUNDS;
    return finished % FOCUS_ROUNDS;
}
