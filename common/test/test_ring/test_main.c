#include <unity.h>
#include "ring.h"

void setUp(void) {}
void tearDown(void) {}

static void test_roundtrip(void)
{
    uint8_t mem[8], out[8];
    ring_t r;
    ring_init(&r, mem, sizeof mem);
    TEST_ASSERT_EQUAL_UINT16(3, ring_write(&r, (const uint8_t *)"abc", 3));
    TEST_ASSERT_EQUAL_UINT16(3, ring_read(&r, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY("abc", out, 3);
    TEST_ASSERT_EQUAL_UINT16(0, ring_used(&r));
}

static void test_partial_write_when_full(void)
{
    uint8_t mem[8];
    ring_t r;
    ring_init(&r, mem, sizeof mem);
    TEST_ASSERT_EQUAL_UINT16(8, ring_write(&r, (const uint8_t *)"0123456789", 10));
    TEST_ASSERT_EQUAL_UINT16(0, ring_free(&r));
    TEST_ASSERT_EQUAL_UINT16(0, ring_write(&r, (const uint8_t *)"x", 1));
}

static void test_wraparound_long_run(void)
{
    uint8_t mem[8], out[5];
    ring_t r;
    ring_init(&r, mem, sizeof mem);
    for (unsigned i = 0; i < 20000u; i++) {
        uint8_t in[5] = {(uint8_t)i, 1, 2, 3, (uint8_t)(i >> 8)};
        TEST_ASSERT_EQUAL_UINT16(5, ring_write(&r, in, 5));
        TEST_ASSERT_EQUAL_UINT16(5, ring_read(&r, out, 5));
        TEST_ASSERT_EQUAL_MEMORY(in, out, 5);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_partial_write_when_full);
    RUN_TEST(test_wraparound_long_run);
    return UNITY_END();
}
