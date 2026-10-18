#include <unity.h>
#include "dshot_frame.h"
#include "dshot_tx_buf.h"
#include "test_vectors.h"

void setUp(void) {}
void tearDown(void) {}

static void test_vectors_encode(void)
{
    for (size_t i = 0; i < DSHOT_VECTOR_COUNT; i++) {
        const dshot_vector_t *v = &DSHOT_VECTORS[i];
        TEST_ASSERT_EQUAL_HEX16(v->frame, dshot_encode(v->value, v->telemetry, v->inverted));
    }
}

static void test_vectors_decode(void)
{
    for (size_t i = 0; i < DSHOT_VECTOR_COUNT; i++) {
        const dshot_vector_t *v = &DSHOT_VECTORS[i];
        dshot_decoded_t d = dshot_decode(v->frame, v->inverted);
        TEST_ASSERT_EQUAL_UINT16(v->value, d.value);
        TEST_ASSERT_EQUAL(v->telemetry, d.telemetry);
        TEST_ASSERT_TRUE(d.crc_ok);
        TEST_ASSERT_FALSE(dshot_decode(v->frame, !v->inverted).crc_ok);
    }
}

static void test_any_single_bit_flip_fails_crc(void)
{
    uint16_t f = dshot_encode(1046, false, false);
    for (unsigned b = 0; b < 16u; b++) {
        TEST_ASSERT_FALSE(dshot_decode((uint16_t)(f ^ (1u << b)), false).crc_ok);
    }
}

static void test_bit_ns(void)
{
    TEST_ASSERT_EQUAL_UINT32(6667, dshot_bit_ns(DSHOT_150));
    TEST_ASSERT_EQUAL_UINT32(3333, dshot_bit_ns(DSHOT_300));
    TEST_ASSERT_EQUAL_UINT32(1667, dshot_bit_ns(DSHOT_600));
    TEST_ASSERT_EQUAL_UINT32(833, dshot_bit_ns(DSHOT_1200));
    TEST_ASSERT_EQUAL_UINT32(0, dshot_bit_ns((dshot_speed_t)400));
}

static void test_tx_timing_84mhz(void)
{
    dshot_tx_timing_t t = dshot_tx_timing(84000000u, DSHOT_600);
    TEST_ASSERT_EQUAL_UINT16(140, t.period);
    TEST_ASSERT_EQUAL_UINT16(53, t.t0h);
    TEST_ASSERT_EQUAL_UINT16(105, t.t1h);
    t = dshot_tx_timing(84000000u, DSHOT_150);
    TEST_ASSERT_EQUAL_UINT16(560, t.period);
    TEST_ASSERT_EQUAL_UINT16(210, t.t0h);
    TEST_ASSERT_EQUAL_UINT16(420, t.t1h);
    t = dshot_tx_timing(84000000u, DSHOT_1200);
    TEST_ASSERT_EQUAL_UINT16(70, t.period);
    TEST_ASSERT_EQUAL_UINT16(26, t.t0h);
    TEST_ASSERT_EQUAL_UINT16(53, t.t1h);
    t = dshot_tx_timing(84000000u, (dshot_speed_t)400);
    TEST_ASSERT_EQUAL_UINT16(0, t.period);
}

static void test_tx_fill(void)
{
    dshot_tx_timing_t t = {140, 53, 105};
    uint16_t buf[DSHOT_TX_BUF_LEN];
    dshot_tx_fill(0x82C6, &t, buf);
    const uint16_t bits[16] = {1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0};
    for (unsigned i = 0; i < 16u; i++) {
        TEST_ASSERT_EQUAL_UINT16(bits[i] ? 105 : 53, buf[i]);
    }
    TEST_ASSERT_EQUAL_UINT16(0, buf[16]);
    TEST_ASSERT_EQUAL_UINT16(0, buf[17]);
}

static void test_max_rate(void)
{
    TEST_ASSERT_EQUAL_UINT32(7142, dshot_max_rate_hz(DSHOT_150));
    TEST_ASSERT_EQUAL_UINT32(12500, dshot_max_rate_hz(DSHOT_300));
    TEST_ASSERT_EQUAL_UINT32(19997, dshot_max_rate_hz(DSHOT_600));
    TEST_ASSERT_EQUAL_UINT32(28576, dshot_max_rate_hz(DSHOT_1200));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_vectors_encode);
    RUN_TEST(test_vectors_decode);
    RUN_TEST(test_any_single_bit_flip_fails_crc);
    RUN_TEST(test_bit_ns);
    RUN_TEST(test_tx_timing_84mhz);
    RUN_TEST(test_tx_fill);
    RUN_TEST(test_max_rate);
    return UNITY_END();
}
