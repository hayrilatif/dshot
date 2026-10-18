#include <string.h>
#include <unity.h>
#include "proto.h"

void setUp(void) {}
void tearDown(void) {}

static proto_msg_t parse(const char *s)
{
    static char buf[128];
    strncpy(buf, s, sizeof buf - 1u);
    buf[sizeof buf - 1u] = '\0';
    return proto_parse(buf);
}

static void test_commands(void)
{
    proto_msg_t m = parse("STATUS");
    TEST_ASSERT_EQUAL(PROTO_STATUS, m.cmd);
    TEST_ASSERT_NULL(m.error);
    m = parse("  tx   thr\t500  ");
    TEST_ASSERT_EQUAL(PROTO_TX_THR, m.cmd);
    TEST_ASSERT_EQUAL_UINT32(500, m.args[0]);
    TEST_ASSERT_EQUAL_UINT8(1, m.nargs);
    m = parse("TX RAW 5 1");
    TEST_ASSERT_EQUAL(PROTO_TX_RAW, m.cmd);
    TEST_ASSERT_EQUAL_UINT8(2, m.nargs);
    TEST_ASSERT_EQUAL_UINT32(1, m.args[1]);
    m = parse("TX CMD 21");
    TEST_ASSERT_EQUAL(PROTO_TX_CMD, m.cmd);
    TEST_ASSERT_EQUAL_UINT8(1, m.nargs);
    m = parse("rx pol inverted");
    TEST_ASSERT_EQUAL(PROTO_RX_POL, m.cmd);
    TEST_ASSERT_EQUAL_UINT32(PROTO_POL_INVERTED, m.args[0]);
    m = parse("TX SPEED 1200");
    TEST_ASSERT_NULL(m.error);
    TEST_ASSERT_EQUAL_UINT32(1200, m.args[0]);
    TEST_ASSERT_EQUAL(PROTO_EMPTY, parse("   ").cmd);
    TEST_ASSERT_TRUE(proto_is_tx(PROTO_TX_TIMEOUT));
    TEST_ASSERT_TRUE(proto_is_rx(PROTO_RX_RESET));
    TEST_ASSERT_FALSE(proto_is_tx(PROTO_PING));
}

static void test_errors(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown", parse("FOO").error);
    TEST_ASSERT_EQUAL_STRING("unknown", parse("TX").error);
    TEST_ASSERT_EQUAL_STRING("unknown", parse("RX THR 5").error);
    TEST_ASSERT_EQUAL_STRING("unknown", parse("THR 5").error);
    TEST_ASSERT_EQUAL_STRING("range", parse("TX SPEED 400").error);
    TEST_ASSERT_EQUAL_STRING("range", parse("TX THR 2001").error);
    TEST_ASSERT_EQUAL_STRING("range", parse("TX CMD 48").error);
    TEST_ASSERT_EQUAL_STRING("range", parse("TX CMD 0").error);
    TEST_ASSERT_EQUAL_STRING("range", parse("RX POL SIDEWAYS").error);
    TEST_ASSERT_EQUAL_STRING("args", parse("TX THR").error);
    TEST_ASSERT_EQUAL_STRING("args", parse("TX THR abc").error);
    TEST_ASSERT_EQUAL_STRING("args", parse("TX THR -5").error);
    TEST_ASSERT_EQUAL_STRING("args", parse("TX THR 5 6").error);
    TEST_ASSERT_EQUAL_STRING("args", parse("TX THR 1234567890").error);
    TEST_ASSERT_EQUAL_STRING("args", parse("A B C D E").error);
}

static proto_line_result_t feed_str(proto_line_t *l, const char *s, char **line)
{
    proto_line_result_t r = PROTO_LINE_NONE;
    for (; *s; s++) {
        r = proto_line_feed(l, *s, line);
    }
    return r;
}

static void test_line_assembly(void)
{
    proto_line_t l;
    char *line = NULL;
    proto_line_init(&l);
    TEST_ASSERT_EQUAL(PROTO_LINE_READY, feed_str(&l, "STATUS\r\n", &line));
    TEST_ASSERT_EQUAL_STRING("STATUS", line);
}

static void test_overlong_line_then_recovers(void)
{
    proto_line_t l;
    char *line = NULL;
    proto_line_init(&l);
    char longline[200];
    memset(longline, 'A', sizeof longline - 2u);
    longline[sizeof longline - 2u] = '\n';
    longline[sizeof longline - 1u] = '\0';
    TEST_ASSERT_EQUAL(PROTO_LINE_OVERFLOW, feed_str(&l, longline, &line));
    TEST_ASSERT_EQUAL(PROTO_LINE_READY, feed_str(&l, "PING\n", &line));
    TEST_ASSERT_EQUAL_STRING("PING", line);
}

static void test_binary_garbage_is_unknown(void)
{
    proto_line_t l;
    char *line = NULL;
    proto_line_init(&l);
    TEST_ASSERT_EQUAL(PROTO_LINE_READY, feed_str(&l, "\x01\xff\x7f\n", &line));
    TEST_ASSERT_EQUAL_STRING("unknown", proto_parse(line).error);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_commands);
    RUN_TEST(test_errors);
    RUN_TEST(test_line_assembly);
    RUN_TEST(test_overlong_line_then_recovers);
    RUN_TEST(test_binary_garbage_is_unknown);
    return UNITY_END();
}
