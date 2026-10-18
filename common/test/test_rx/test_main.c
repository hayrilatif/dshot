#include <unity.h>
#include "burst_helper.h"
#include "dshot_rx_decode.h"
#include "dshot_rx_stats.h"
#include "dshot_tx_buf.h"
#include "test_vectors.h"

void setUp(void) {}
void tearDown(void) {}

static const dshot_speed_t SPEEDS[] = {DSHOT_150, DSHOT_300, DSHOT_600, DSHOT_1200};

static void check_all_vectors(uint32_t tick_hz)
{
    for (unsigned s = 0; s < 4u; s++) {
        uint16_t bit = dshot_tx_timing(tick_hz, SPEEDS[s]).period;
        for (size_t i = 0; i < DSHOT_VECTOR_COUNT; i++) {
            const dshot_vector_t *v = &DSHOT_VECTORS[i];
            dshot_rx_burst_t b = make_burst(v->frame, bit, v->inverted);
            dshot_burst_result_t r = dshot_decode_burst(&b, tick_hz, v->inverted);
            TEST_ASSERT_TRUE(r.is_frame);
            TEST_ASSERT_EQUAL_HEX16(v->frame, r.raw);
            TEST_ASSERT_TRUE(r.frame.crc_ok);
            TEST_ASSERT_EQUAL_UINT16(v->value, r.frame.value);
            TEST_ASSERT_EQUAL(SPEEDS[s], r.speed);
        }
    }
}

static void test_vectors_all_speeds_84mhz(void)
{
    check_all_vectors(84000000u);
}

static void test_vectors_all_speeds_72mhz(void)
{
    check_all_vectors(72000000u);
}

static void test_all_zero_frame(void)
{
    dshot_rx_burst_t b = make_burst(0x0000, 140, false);
    dshot_burst_result_t r = dshot_decode_burst(&b, 84000000u, false);
    TEST_ASSERT_TRUE(r.frame.crc_ok);
    TEST_ASSERT_EQUAL_UINT16(0, r.frame.value);
    TEST_ASSERT_EQUAL_UINT32(0, r.t1h_ns);
    TEST_ASSERT_UINT32_WITHIN(15, 625, r.t0h_ns);
}

static void test_timing_report(void)
{
    dshot_rx_burst_t b = make_burst(0x82C6, 140, false);
    dshot_burst_result_t r = dshot_decode_burst(&b, 84000000u, false);
    TEST_ASSERT_EQUAL_UINT32(1667, r.bit_ns);
    TEST_ASSERT_UINT32_WITHIN(15, 625, r.t0h_ns);
    TEST_ASSERT_UINT32_WITHIN(15, 1250, r.t1h_ns);
}

static void test_jitter_5_percent(void)
{
    dshot_rx_burst_t b = make_burst(0x82C6, 140, false);
    for (unsigned i = 1; i < 16u; i++) {
        b.periods[i] = (uint16_t)(i % 2u ? 147u : 133u);
    }
    for (unsigned i = 0; i < 16u; i++) {
        b.highs[i] = (uint16_t)(i % 2u ? b.highs[i] * 105u / 100u : b.highs[i] * 95u / 100u);
    }
    dshot_burst_result_t r = dshot_decode_burst(&b, 84000000u, false);
    TEST_ASSERT_EQUAL_HEX16(0x82C6, r.raw);
    TEST_ASSERT_TRUE(r.frame.crc_ok);
}

static void test_wrong_pulse_count_is_not_frame(void)
{
    const uint8_t counts[] = {0, 1, 15, 17, 255};
    for (unsigned i = 0; i < sizeof counts; i++) {
        dshot_rx_burst_t b = make_burst(0x82C6, 140, false);
        b.n = counts[i];
        TEST_ASSERT_FALSE(dshot_decode_burst(&b, 84000000u, false).is_frame);
    }
}

static void test_garbage_burst_does_not_crash(void)
{
    dshot_rx_burst_t b;
    memset(&b, 0, sizeof b);
    b.n = 16;
    dshot_burst_result_t r = dshot_decode_burst(&b, 84000000u, false);
    TEST_ASSERT_TRUE(r.is_frame);
    TEST_ASSERT_EQUAL(DSHOT_SPEED_UNKNOWN, r.speed);
}

static void test_speed_from_bit_ns(void)
{
    TEST_ASSERT_EQUAL(DSHOT_600, dshot_speed_from_bit_ns(1667u * 109u / 100u));
    TEST_ASSERT_EQUAL(DSHOT_SPEED_UNKNOWN, dshot_speed_from_bit_ns(2500));
    TEST_ASSERT_EQUAL(DSHOT_150, dshot_speed_from_bit_ns(6500));
}

static void test_polarity_switches_after_8(void)
{
    dshot_pol_t p;
    dshot_pol_init(&p, false);
    for (unsigned i = 0; i < 7u; i++) {
        TEST_ASSERT_FALSE(dshot_pol_update(&p, true));
    }
    TEST_ASSERT_FALSE(dshot_pol_update(&p, false)); /* seri bozuldu */
    for (unsigned i = 0; i < 7u; i++) {
        TEST_ASSERT_FALSE(dshot_pol_update(&p, true));
    }
    TEST_ASSERT_TRUE(dshot_pol_update(&p, true));
    TEST_ASSERT_TRUE(p.inverted);
    TEST_ASSERT_FALSE(dshot_pol_update(&p, true));
}

static dshot_burst_result_t result_for(uint16_t value, bool tlm)
{
    dshot_rx_burst_t b = make_burst(dshot_encode(value, tlm, false), 140, false);
    return dshot_decode_burst(&b, 84000000u, false);
}

static void test_stats_empty_format(void)
{
    dshot_rx_stats_t s;
    char out[256];
    dshot_stats_reset(&s);
    dshot_stats_format(&s, 100, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("#RX n=0 rate=0 val=- vmin=- vmax=- tlm=- crc_err=0 other=0 inv=0 speed=? bit_ns=0 "
                             "t0h_ns=0 t1h_ns=0\n",
                             out);
}

static void test_stats_aggregate(void)
{
    dshot_rx_stats_t s;
    char out[256];
    dshot_stats_reset(&s);
    dshot_burst_result_t r;
    r = result_for(100, false);
    dshot_stats_add(&s, &r, false);
    r = result_for(50, true);
    dshot_stats_add(&s, &r, false);
    r = result_for(200, false);
    dshot_stats_add(&s, &r, false);
    r = result_for(300, false);
    r.frame.crc_ok = false;
    dshot_stats_add(&s, &r, false);
    r.is_frame = false;
    dshot_stats_add(&s, &r, false);
    dshot_stats_format(&s, 1000, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING_LEN("#RX n=3 rate=3 val=200 vmin=50 vmax=200 tlm=mix crc_err=1 other=1 inv=0 speed=600 "
                                 "bit_ns=1667",
                                 out, 87);
}

static void test_stats_format_truncates_safely(void)
{
    dshot_rx_stats_t s;
    char out[10];
    dshot_stats_reset(&s);
    TEST_ASSERT_EQUAL(9, dshot_stats_format(&s, 100, out, sizeof out));
    TEST_ASSERT_EQUAL(0, dshot_stats_format(&s, 100, out, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_vectors_all_speeds_84mhz);
    RUN_TEST(test_vectors_all_speeds_72mhz);
    RUN_TEST(test_all_zero_frame);
    RUN_TEST(test_timing_report);
    RUN_TEST(test_jitter_5_percent);
    RUN_TEST(test_wrong_pulse_count_is_not_frame);
    RUN_TEST(test_garbage_burst_does_not_crash);
    RUN_TEST(test_speed_from_bit_ns);
    RUN_TEST(test_polarity_switches_after_8);
    RUN_TEST(test_stats_empty_format);
    RUN_TEST(test_stats_aggregate);
    RUN_TEST(test_stats_format_truncates_safely);
    return UNITY_END();
}
