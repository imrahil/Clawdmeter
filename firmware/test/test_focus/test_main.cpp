// focus: the device-local Pomodoro timer.
//   pio test -d firmware -e native -f test_focus
#include <unity.h>
#include "focus.h"

void setUp(void) { focus_reset(); focus_reset(); }   // to ready, then a fresh cycle
void tearDown(void) {}

static const uint32_t MIN = 60UL * 1000;

static void test_ready_until_tapped(void) {
    TEST_ASSERT_EQUAL(FOCUS_READY, focus_phase());
    TEST_ASSERT_FALSE(focus_running());
    TEST_ASSERT_EQUAL_UINT32(FOCUS_WORK_MS, focus_remaining_ms(123456));
    TEST_ASSERT_FALSE(focus_tick(10 * 60 * MIN));            // nothing runs by itself
}

static void test_pause_and_resume(void) {
    focus_toggle(1000);
    TEST_ASSERT_EQUAL(FOCUS_WORK, focus_phase());
    TEST_ASSERT_EQUAL_UINT32(FOCUS_WORK_MS - 10 * MIN, focus_remaining_ms(1000 + 10 * MIN));
    focus_toggle(1000 + 10 * MIN);                           // pause
    TEST_ASSERT_FALSE(focus_running());
    TEST_ASSERT_EQUAL_UINT32(15 * MIN, focus_remaining_ms(1000 + 50 * MIN));   // frozen
    focus_toggle(1000 + 50 * MIN);                           // resume
    TEST_ASSERT_EQUAL_UINT32(5 * MIN, focus_remaining_ms(1000 + 60 * MIN));
}

static void test_work_then_break_then_ready(void) {
    focus_toggle(0);
    TEST_ASSERT_FALSE(focus_tick(FOCUS_WORK_MS - 1));
    TEST_ASSERT_TRUE(focus_tick(FOCUS_WORK_MS));             // focus over: the break starts itself
    TEST_ASSERT_EQUAL(FOCUS_BREAK, focus_phase());
    TEST_ASSERT_TRUE(focus_running());
    TEST_ASSERT_FALSE(focus_long_break());
    TEST_ASSERT_EQUAL(1, focus_rounds_done());
    TEST_ASSERT_FALSE(focus_tick(FOCUS_WORK_MS));            // reported once
    TEST_ASSERT_TRUE(focus_tick(FOCUS_WORK_MS + FOCUS_SHORT_BREAK_MS));
    TEST_ASSERT_EQUAL(FOCUS_READY, focus_phase());           // the next focus waits for a tap
    TEST_ASSERT_FALSE(focus_running());
}

static void test_fourth_round_gets_the_long_break(void) {
    uint32_t t = 0;
    for (int i = 0; i < FOCUS_ROUNDS; i++) {
        focus_toggle(t);
        t += FOCUS_WORK_MS;
        TEST_ASSERT_TRUE(focus_tick(t));
        if (i < FOCUS_ROUNDS - 1) {
            TEST_ASSERT_FALSE(focus_long_break());
            t += FOCUS_SHORT_BREAK_MS;
            TEST_ASSERT_TRUE(focus_tick(t));
        }
    }
    TEST_ASSERT_TRUE(focus_long_break());
    TEST_ASSERT_EQUAL_UINT32(FOCUS_LONG_BREAK_MS, focus_total_ms());
    TEST_ASSERT_EQUAL(FOCUS_ROUNDS, focus_rounds_done());    // the cycle reads full
    TEST_ASSERT_TRUE(focus_tick(t + FOCUS_LONG_BREAK_MS));
    TEST_ASSERT_EQUAL(0, focus_rounds_done());               // and rolls over
}

static void test_reset_skips_a_break(void) {
    focus_toggle(0);
    TEST_ASSERT_TRUE(focus_tick(FOCUS_WORK_MS));
    focus_reset();
    TEST_ASSERT_EQUAL(FOCUS_READY, focus_phase());
    TEST_ASSERT_EQUAL_UINT32(FOCUS_WORK_MS, focus_remaining_ms(0));
}

static void test_reset_when_ready_clears_the_rounds(void) {
    focus_toggle(0);
    TEST_ASSERT_TRUE(focus_tick(FOCUS_WORK_MS));
    focus_reset();                                           // skip the break: one round kept
    TEST_ASSERT_EQUAL(1, focus_rounds_done());
    focus_reset();                                           // ready already: the cycle starts over
    TEST_ASSERT_EQUAL(0, focus_rounds_done());
}

static void test_millis_wrap(void) {
    const uint32_t start = 0xFFFFFFFFu - 5 * MIN;           // millis() wraps mid-session
    focus_toggle(start);
    TEST_ASSERT_EQUAL_UINT32(FOCUS_WORK_MS - 10 * MIN, focus_remaining_ms(start + 10 * MIN));
    TEST_ASSERT_TRUE(focus_tick(start + FOCUS_WORK_MS));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ready_until_tapped);
    RUN_TEST(test_pause_and_resume);
    RUN_TEST(test_work_then_break_then_ready);
    RUN_TEST(test_fourth_round_gets_the_long_break);
    RUN_TEST(test_reset_skips_a_break);
    RUN_TEST(test_reset_when_ready_clears_the_rounds);
    RUN_TEST(test_millis_wrap);
    return UNITY_END();
}
