// feeds: the pages' tagged writes besides weather ({"k":"h"|"a"|"sys"|"q"|"ss"}).
//   pio test -d firmware -e native -f test_feeds
#include <unity.h>
#include <string.h>
#include "feeds.h"

void setUp(void) {}
void tearDown(void) {}

static void test_kind(void) {
    TEST_ASSERT_EQUAL(FEED_PACE, feed_kind("{\"k\":\"h\",\"sh\":[]}"));
    TEST_ASSERT_EQUAL(FEED_ACTIVITY, feed_kind("{\"k\":\"a\"}"));
    TEST_ASSERT_EQUAL(FEED_SYSTEM, feed_kind("{\"k\":\"sys\",\"cpu\":3}"));
    TEST_ASSERT_EQUAL(FEED_STOCK, feed_kind("{\"k\":\"q\",\"i\":0}"));
    TEST_ASSERT_EQUAL(FEED_STATUS, feed_kind("{\"k\":\"ss\",\"i\":\"none\"}"));
    TEST_ASSERT_EQUAL(FEED_NONE, feed_kind("{\"k\":\"wx\"}"));          // weather.h's
    TEST_ASSERT_EQUAL(FEED_NONE, feed_kind("{\"k\":\"hx\"}"));
    TEST_ASSERT_EQUAL(FEED_NONE, feed_kind("{\"s\":42,\"ok\":true}"));  // usage
    TEST_ASSERT_EQUAL(FEED_NONE, feed_kind(nullptr));
}

static void test_pace(void) {
    PaceData p;
    TEST_ASSERT_TRUE(feed_parse_pace(
        "{\"k\":\"h\",\"sh\":[0,4,9,140,-3],\"fr\":17,\"pr\":112,\"pz\":2,\"eta\":\"15:40\","
        "\"wd\":[[\"Thu\",-1],[\"Fri\",12],[\"Sat\",0],[\"Sun\",3],[\"Mon\",20],[\"Tue\",9],[\"Wed\",4],[\"Thu\",1]],"
        "\"wp\":74}", &p));
    TEST_ASSERT_EQUAL(5, p.n_sess);
    TEST_ASSERT_EQUAL(100, p.sess[3]);   // clamped
    TEST_ASSERT_EQUAL(0, p.sess[4]);
    TEST_ASSERT_EQUAL(17, p.frac);
    TEST_ASSERT_EQUAL(112, p.proj);
    TEST_ASSERT_EQUAL(2, p.zone);
    TEST_ASSERT_EQUAL_STRING("15:40", p.eta);
    TEST_ASSERT_EQUAL(PACE_DAYS, p.n_days);   // the eighth is dropped
    TEST_ASSERT_EQUAL(-1, p.days[0].pct);
    TEST_ASSERT_EQUAL_STRING("Fri", p.days[1].day);
    TEST_ASSERT_EQUAL(74, p.week_proj);

    TEST_ASSERT_TRUE(feed_parse_pace("{\"k\":\"h\",\"wd\":[]}", &p));   // no window yet
    TEST_ASSERT_EQUAL(0, p.n_sess);
    TEST_ASSERT_EQUAL(-1, p.proj);
    TEST_ASSERT_EQUAL(-1, p.week_proj);
    TEST_ASSERT_EQUAL(0, p.zone);
}

static void test_activity(void) {
    ActivityData a;
    TEST_ASSERT_TRUE(feed_parse_activity(
        "{\"k\":\"a\",\"ss\":[[\"clawdmeter\",2,\"needs you\",0],[\"api\",1,\"Bash\",1],"
        "[\"web\",0,\"\",12],[\"x\",1,\"Thinking\",0],[\"y\",0,\"\",50]],"
        "\"nl\":5,\"tk\":3456789012,\"co\":1234,\"ns\":7}", &a));
    TEST_ASSERT_EQUAL(ACT_ROWS_MAX, a.n_rows);
    TEST_ASSERT_EQUAL_STRING("clawdmeter", a.rows[0].name);
    TEST_ASSERT_EQUAL(SESSION_WAITING, a.rows[0].state);
    TEST_ASSERT_EQUAL_STRING("Bash", a.rows[1].label);
    TEST_ASSERT_EQUAL(12, a.rows[2].age_min);
    TEST_ASSERT_EQUAL(5, a.live);
    TEST_ASSERT_TRUE(a.tokens > 3.4e9);   // past 2^31
    TEST_ASSERT_EQUAL(1234, a.cost_cents);
    TEST_ASSERT_EQUAL(7, a.sessions);
}

static void test_system(void) {
    SystemData s;
    TEST_ASSERT_TRUE(feed_parse_system(
        "{\"k\":\"sys\",\"hn\":\"desk\",\"cpu\":23,\"ld\":\"1.42\",\"nc\":16,\"mem\":61,"
        "\"mu\":\"9.8/16 GB\",\"tmp\":54,\"dsk\":48,\"du\":\"220/460 GB\",\"up\":\"3d 4h\"}", &s));
    TEST_ASSERT_EQUAL_STRING("desk", s.host);
    TEST_ASSERT_EQUAL(23, s.cpu);
    TEST_ASSERT_EQUAL_STRING("1.42", s.load);
    TEST_ASSERT_EQUAL(16, s.cores);
    TEST_ASSERT_EQUAL_STRING("9.8/16 GB", s.mem_text);
    TEST_ASSERT_EQUAL(54, s.temp);
    TEST_ASSERT_EQUAL_STRING("3d 4h", s.uptime);
    TEST_ASSERT_TRUE(feed_parse_system("{\"k\":\"sys\",\"hn\":\"x\"}", &s));
    TEST_ASSERT_EQUAL(-1, s.temp);        // no sensor
}

static void test_stock(void) {
    StockQuote q;
    TEST_ASSERT_TRUE(feed_parse_stock(
        "{\"k\":\"q\",\"i\":1,\"n\":3,\"sy\":\"NVDA\",\"nm\":\"NVIDIA\",\"cu\":\"USD\",\"p\":181.5,"
        "\"pc\":178.25,\"o\":1,\"h\":[0,50,99,120],\"b\":12,\"fr\":40}", &q));
    TEST_ASSERT_EQUAL(1, q.index);
    TEST_ASSERT_EQUAL(3, q.count);
    TEST_ASSERT_EQUAL_STRING("NVDA", q.symbol);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 181.5f, q.price);
    TEST_ASSERT_TRUE(q.open);
    TEST_ASSERT_EQUAL(4, q.n_hist);
    TEST_ASSERT_EQUAL(99, q.hist[3]);     // clamped
    TEST_ASSERT_EQUAL(12, q.baseline);
    TEST_ASSERT_EQUAL(40, q.session_pct);

    TEST_ASSERT_TRUE(feed_parse_stock("{\"k\":\"q\",\"i\":0,\"n\":1,\"sy\":\"AMD\",\"p\":1}", &q));
    TEST_ASSERT_FALSE(q.open);
    TEST_ASSERT_EQUAL(100, q.session_pct);   // closed / absent = full width

    q.index = 42;
    TEST_ASSERT_FALSE(feed_parse_stock("{\"k\":\"q\",\"i\":5,\"n\":6}", &q));   // no such row
    TEST_ASSERT_FALSE(feed_parse_stock("{\"k\":\"q\"}", &q));
    TEST_ASSERT_EQUAL(42, q.index);       // untouched
}

static void test_status(void) {
    ServiceStatus s;
    TEST_ASSERT_TRUE(feed_parse_status("{\"k\":\"ss\",\"i\":\"major\",\"m\":\"Elevated errors on Claude\"}", &s));
    TEST_ASSERT_EQUAL(2, s.level);
    TEST_ASSERT_EQUAL_STRING("Elevated errors on Claude", s.msg);
    TEST_ASSERT_TRUE(feed_parse_status("{\"k\":\"ss\",\"i\":\"maintenance\",\"m\":\"API maintenance\"}", &s));
    TEST_ASSERT_EQUAL(1, s.level);
    TEST_ASSERT_TRUE(feed_parse_status("{\"k\":\"ss\",\"i\":\"none\",\"m\":\"leftover\"}", &s));
    TEST_ASSERT_EQUAL(0, s.level);
    TEST_ASSERT_EQUAL_STRING("", s.msg);
}

static void test_rejects(void) {
    PaceData p;
    p.frac = 77;
    TEST_ASSERT_FALSE(feed_parse_pace("{\"k\":\"a\"}", &p));   // another kind
    TEST_ASSERT_FALSE(feed_parse_pace("{\"k\":\"h\",", &p));   // cut short
    TEST_ASSERT_FALSE(feed_parse_pace(nullptr, &p));
    TEST_ASSERT_EQUAL(77, p.frac);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_kind);
    RUN_TEST(test_pace);
    RUN_TEST(test_activity);
    RUN_TEST(test_system);
    RUN_TEST(test_stock);
    RUN_TEST(test_status);
    RUN_TEST(test_rejects);
    return UNITY_END();
}
