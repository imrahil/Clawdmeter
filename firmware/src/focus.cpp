#include "focus.h"

static FocusPhase phase = FOCUS_READY;
static bool       running = false;
static uint32_t   total_ms = FOCUS_WORK_MS;
static uint32_t   remaining_ms = FOCUS_WORK_MS;   // while paused / ready
static uint32_t   end_ms = 0;                     // while running
static int        finished = 0;                   // focus sessions completed overall

static void enter(FocusPhase p, uint32_t len, bool run, uint32_t now) {
    phase = p;
    total_ms = remaining_ms = len;
    running = run;
    end_ms = now + len;
}

uint32_t focus_remaining_ms(uint32_t now) {
    if (!running) return remaining_ms;
    const int32_t left = (int32_t)(end_ms - now);
    return left > 0 ? (uint32_t)left : 0;
}

void focus_toggle(uint32_t now) {
    if (running) {
        remaining_ms = focus_remaining_ms(now);
        running = false;
    } else {
        if (phase == FOCUS_READY) phase = FOCUS_WORK;
        end_ms = now + remaining_ms;
        running = true;
    }
}

void focus_reset(void) {
    if (phase == FOCUS_READY) finished = 0;   // already ready: start the cycle over
    enter(FOCUS_READY, FOCUS_WORK_MS, false, 0);
}

bool focus_tick(uint32_t now) {
    if (!running || focus_remaining_ms(now) > 0) return false;
    if (phase == FOCUS_WORK) {
        finished++;
        enter(FOCUS_BREAK, finished % FOCUS_ROUNDS == 0 ? FOCUS_LONG_BREAK_MS : FOCUS_SHORT_BREAK_MS,
              true, now);
    } else {
        enter(FOCUS_READY, FOCUS_WORK_MS, false, now);
    }
    return true;
}

FocusPhase focus_phase(void)      { return phase; }
bool       focus_running(void)    { return running; }
bool       focus_long_break(void) { return phase == FOCUS_BREAK && total_ms == FOCUS_LONG_BREAK_MS; }
uint32_t   focus_total_ms(void)   { return total_ms; }

int focus_rounds_done(void) {
    // During the long break the cycle shows full; it rolls over when focus resumes.
    if (phase == FOCUS_BREAK && finished > 0 && finished % FOCUS_ROUNDS == 0) return FOCUS_ROUNDS;
    return finished % FOCUS_ROUNDS;
}
