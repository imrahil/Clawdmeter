// weather: the Weather page's write ({"k":"wx"}) and the WMO code tables.
//   pio test -d firmware -e native -f test_weather
#include <unity.h>
#include <string.h>
#include "weather.h"

void setUp(void) {}
void tearDown(void) {}

static const char* FULL =
    "{\"k\":\"wx\",\"loc\":\"Warsaw\",\"t\":12.8,\"fl\":11,\"c\":2,\"d\":1,\"hi\":15,\"lo\":7,"
    "\"f\":[[\"Tue\",80,16,8],[\"Wed\",61,19,13],[\"Thu\",3,18,12],[\"Fri\",0,20,9]],"
    "\"sun\":\"Sunset 18:45\",\"ws\":14,\"uv\":3,\"aq\":22,\"rn\":\"Rain in 30 min\"}";

static void test_is_payload(void) {
    TEST_ASSERT_TRUE(weather_is_payload(FULL));
    TEST_ASSERT_FALSE(weather_is_payload("{\"s\":42,\"ok\":true}"));
    TEST_ASSERT_FALSE(weather_is_payload(nullptr));
}

static void test_parse_full(void) {
    WeatherData w;
    TEST_ASSERT_TRUE(weather_parse(FULL, &w));
    TEST_ASSERT_EQUAL_STRING("Warsaw", w.location);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.8f, w.temp);
    TEST_ASSERT_EQUAL(11, w.feels);
    TEST_ASSERT_EQUAL(2, w.code);
    TEST_ASSERT_TRUE(w.is_day);
    TEST_ASSERT_EQUAL(15, w.hi);
    TEST_ASSERT_EQUAL(7, w.lo);
    TEST_ASSERT_EQUAL(WX_FORECAST_DAYS, w.n_days);        // the fourth day is dropped
    TEST_ASSERT_EQUAL_STRING("Wed", w.days[1].day);
    TEST_ASSERT_EQUAL(61, w.days[1].code);
    TEST_ASSERT_EQUAL(13, w.days[1].lo);
    TEST_ASSERT_EQUAL_STRING("Sunset 18:45", w.sun);
    TEST_ASSERT_EQUAL(14, w.wind);
    TEST_ASSERT_EQUAL(3, w.uv);
    TEST_ASSERT_EQUAL(22, w.aqi);
    TEST_ASSERT_EQUAL_STRING("Rain in 30 min", w.rain);
}

static void test_optional_fields_absent(void) {
    WeatherData w;
    TEST_ASSERT_TRUE(weather_parse("{\"k\":\"wx\",\"loc\":\"X\",\"t\":-3.4,\"c\":71,\"d\":0}", &w));
    TEST_ASSERT_EQUAL(-1, w.wind);
    TEST_ASSERT_EQUAL(-1, w.uv);
    TEST_ASSERT_EQUAL(-1, w.aqi);
    TEST_ASSERT_EQUAL_STRING("", w.rain);
    TEST_ASSERT_EQUAL(0, w.n_days);
    TEST_ASSERT_FALSE(w.is_day);
}

static void test_rejects(void) {
    WeatherData w;
    memset(&w, 0x5A, sizeof(w));
    TEST_ASSERT_FALSE(weather_parse("{\"s\":42,\"ok\":true}", &w));     // usage, not weather
    TEST_ASSERT_FALSE(weather_parse("{\"k\":\"wx\",", &w));            // truncated
    TEST_ASSERT_EQUAL_HEX8(0x5A, ((unsigned char*)&w)[0]);              // untouched
}

static void test_long_strings_are_cut(void) {
    WeatherData w;
    TEST_ASSERT_TRUE(weather_parse("{\"k\":\"wx\",\"loc\":\"Llanfairpwllgwyngyllgogerychwyrndrobwll\"}", &w));
    TEST_ASSERT_EQUAL(sizeof(w.location) - 1, strlen(w.location));
}

static void test_codes(void) {
    TEST_ASSERT_EQUAL(WX_SUN, weather_icon_for(0, true));
    TEST_ASSERT_EQUAL(WX_MOON, weather_icon_for(0, false));
    TEST_ASSERT_EQUAL(WX_CLOUD_MOON, weather_icon_for(2, false));
    TEST_ASSERT_EQUAL(WX_FOG, weather_icon_for(48, true));
    TEST_ASSERT_EQUAL(WX_RAIN, weather_icon_for(81, true));
    TEST_ASSERT_EQUAL(WX_SNOW, weather_icon_for(86, true));
    TEST_ASSERT_EQUAL(WX_THUNDER, weather_icon_for(99, true));
    TEST_ASSERT_EQUAL_STRING("Partly cloudy", weather_text_for(2));
    TEST_ASSERT_EQUAL_STRING("", weather_text_for(42));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_is_payload);
    RUN_TEST(test_parse_full);
    RUN_TEST(test_optional_fields_absent);
    RUN_TEST(test_rejects);
    RUN_TEST(test_long_strings_are_cut);
    RUN_TEST(test_codes);
    return UNITY_END();
}
