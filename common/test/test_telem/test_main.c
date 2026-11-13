#include <unity.h>
#include "dshot_telem.h"
#include "dshot_tx_buf.h"
#include "reply_helper.h"

void setUp(void) {}
void tearDown(void) {}

static const dshot_speed_t SPEEDS[] = {DSHOT_150, DSHOT_300, DSHOT_600, DSHOT_1200};

static uint32_t reply_bit_ticks(uint32_t tick_hz, dshot_speed_t s)
{
    return (tick_hz * 4u + (uint32_t)s * 2500u) / ((uint32_t)s * 5000u);
}

static void test_roundtrip_all_speeds_and_values(void)
{
    const uint16_t values[] = {0x000, 0xFFF, 0x3F4, 0x223, 0x555, 0xAAA, 0x1FF, 0x800, 0x123};
    for (unsigned s = 0; s < 4u; s++) {
        uint32_t bit = reply_bit_ticks(84000000u, SPEEDS[s]);
        for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++) {
            dshot_reply_t r = make_reply(values[i], bit);
            uint16_t v = 0xFFFF;
            TEST_ASSERT_EQUAL(DSHOT_REPLY_OK, dshot_reply_decode(&r, 84000000u, SPEEDS[s], &v));
            TEST_ASSERT_EQUAL_HEX16(values[i], v);
        }
    }
}

static void test_tolerates_timing_jitter(void)
{
    dshot_reply_t r = make_reply(0x3F4, 112);
    for (unsigned i = 1; i < r.count; i++) {
        r.edges[i] = (uint16_t)(r.edges[i] + (i % 2u ? 20 : -20)); /* ±%18 */
    }
    uint16_t v;
    TEST_ASSERT_EQUAL(DSHOT_REPLY_OK, dshot_reply_decode(&r, 84000000u, DSHOT_600, &v));
    TEST_ASSERT_EQUAL_HEX16(0x3F4, v);
}

static void test_errors(void)
{
    uint16_t v;
    dshot_reply_t r;
    memset(&r, 0, sizeof r);
    TEST_ASSERT_EQUAL(DSHOT_REPLY_NONE, dshot_reply_decode(&r, 84000000u, DSHOT_600, &v));
    r.count = 1; /* yalnız başlangıç kenarı → GCR 00000 geçersiz */
    TEST_ASSERT_EQUAL(DSHOT_REPLY_BAD_GCR, dshot_reply_decode(&r, 84000000u, DSHOT_600, &v));
    r = make_reply(0x3F4, 112);
    r.edges[r.count - 1u] = (uint16_t)(r.edges[r.count - 1u] + 112u * 6u); /* 21 biti aşar */
    TEST_ASSERT_EQUAL(DSHOT_REPLY_BAD_BITS, dshot_reply_decode(&r, 84000000u, DSHOT_600, &v));
    r = make_reply(0x3F4, 112);
    r.edges[3] = (uint16_t)(r.edges[2] + 1u); /* aynı anda iki kenar (gürültü) */
    TEST_ASSERT_NOT_EQUAL(DSHOT_REPLY_OK, dshot_reply_decode(&r, 84000000u, DSHOT_600, &v));
    r = make_reply(0x3F4, 112);
    r.count = DSHOT_REPLY_MAX_EDGES;
    for (unsigned i = 0; i < r.count; i++) {
        r.edges[i] = (uint16_t)(i * 112u);
    }
    TEST_ASSERT_EQUAL(DSHOT_REPLY_BAD_BITS, dshot_reply_decode(&r, 84000000u, DSHOT_600, &v));
}

static void test_crc_error_detected(void)
{
    uint16_t crc = (uint16_t)(~(0x3F4 ^ (0x3F4 >> 4) ^ (0x3F4 >> 8)) & 0xFu);
    dshot_reply_t r = make_reply_raw((uint16_t)((0x3F4 << 4) | (crc ^ 0x1u)), 112);
    uint16_t v;
    TEST_ASSERT_EQUAL(DSHOT_REPLY_BAD_CRC, dshot_reply_decode(&r, 84000000u, DSHOT_600, &v));
}

static void test_erpm_and_edt(void)
{
    TEST_ASSERT_EQUAL_UINT32(60000, dshot_v12_erpm(0x3F4)); /* 500<<1 = 1000 µs */
    TEST_ASSERT_EQUAL_UINT32(0, dshot_v12_erpm(0xFFF));     /* motor duruyor */
    TEST_ASSERT_EQUAL_UINT32(0, dshot_v12_erpm(0x000));
    TEST_ASSERT_FALSE(dshot_v12_is_edt(0x3F4));
    TEST_ASSERT_FALSE(dshot_v12_is_edt(0x0FF)); /* üs 0: normal eRPM */
    TEST_ASSERT_TRUE(dshot_v12_is_edt(0x223));
}

static void test_telem_aggregate_and_format(void)
{
    dshot_telem_t t;
    char out[200];
    dshot_telem_init(&t);
    dshot_telem_format(&t, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("#T ok=0 bad=0 miss=0 erpm=- temp_c=- volt_mv=- curr_a=- stress=- status=-\n", out);
    dshot_telem_add(&t, DSHOT_REPLY_OK, 0x3F4);
    dshot_telem_add(&t, DSHOT_REPLY_OK, 0x223);          /* 35 °C */
    dshot_telem_add(&t, DSHOT_REPLY_OK, 0x400 | 64u);    /* 16.0 V */
    dshot_telem_add(&t, DSHOT_REPLY_OK, 0x600 | 3u);     /* 3 A */
    dshot_telem_add(&t, DSHOT_REPLY_NONE, 0);
    dshot_telem_add(&t, DSHOT_REPLY_BAD_CRC, 0);
    dshot_telem_format(&t, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("#T ok=4 bad=1 miss=1 erpm=60000 temp_c=35 volt_mv=16000 curr_a=3 stress=- status=-\n",
                             out);
    dshot_telem_reset_window(&t);
    dshot_telem_format(&t, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("#T ok=0 bad=0 miss=0 erpm=- temp_c=35 volt_mv=16000 curr_a=3 stress=- status=-\n", out);
}

/* F103: düşen ve yükselen kenarlar ayrı tamponlarda yakalanır; birleşince aynı cevap çözülmeli */
static void test_merge_split_edges(void)
{
    dshot_reply_t full = make_reply(0x5A3, 112);
    uint16_t fall[DSHOT_REPLY_MAX_EDGES], rise[DSHOT_REPLY_MAX_EDGES];
    unsigned nf = 0, nr = 0;
    for (unsigned i = 0; i < full.count; i++) {
        if (i % 2u == 0u) {
            fall[nf++] = full.edges[i];
        } else {
            rise[nr++] = full.edges[i];
        }
    }
    dshot_reply_t r;
    dshot_reply_merge(&r, fall, nf, rise, nr);
    TEST_ASSERT_EQUAL_UINT8(full.count, r.count);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(full.edges, r.edges, full.count);
    uint16_t v12 = 0;
    TEST_ASSERT_EQUAL(DSHOT_REPLY_OK, dshot_reply_decode(&r, 84000000u, DSHOT_600, &v12));
    TEST_ASSERT_EQUAL_HEX16(0x5A3, v12);

    /* toplam sığmazsa kesilir */
    uint16_t many[DSHOT_REPLY_MAX_EDGES];
    for (unsigned i = 0; i < DSHOT_REPLY_MAX_EDGES; i++) {
        many[i] = (uint16_t)(i * 2u);
    }
    dshot_reply_merge(&r, many, DSHOT_REPLY_MAX_EDGES, many, 4);
    TEST_ASSERT_EQUAL_UINT8(DSHOT_REPLY_MAX_EDGES, r.count);
    dshot_reply_merge(&r, fall, 0, rise, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r.count);
}

static void test_bidir_rate_limits(void)
{
    TEST_ASSERT_TRUE(dshot_max_rate_bidir_hz(DSHOT_600) < dshot_max_rate_hz(DSHOT_600));
    TEST_ASSERT_TRUE(dshot_max_rate_bidir_hz(DSHOT_600) > 4000u);
    TEST_ASSERT_TRUE(dshot_max_rate_bidir_hz(DSHOT_150) > 1000u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_all_speeds_and_values);
    RUN_TEST(test_tolerates_timing_jitter);
    RUN_TEST(test_errors);
    RUN_TEST(test_crc_error_detected);
    RUN_TEST(test_erpm_and_edt);
    RUN_TEST(test_telem_aggregate_and_format);
    RUN_TEST(test_bidir_rate_limits);
    RUN_TEST(test_merge_split_edges);
    return UNITY_END();
}
