#include <unity.h>
#include "dshot_txctl.h"

static dshot_txctl_t t;

void setUp(void)
{
    dshot_txctl_init(&t, 0);
}
void tearDown(void) {}

static void test_defaults(void)
{
    TEST_ASSERT_EQUAL(DSHOT_600, t.speed);
    TEST_ASSERT_EQUAL_UINT32(1000, t.rate_hz);
    TEST_ASSERT_EQUAL_UINT32(500, t.timeout_ms);
    TEST_ASSERT_TRUE(t.enabled);
    uint16_t f;
    TEST_ASSERT_TRUE(dshot_txctl_next_frame(&t, &f));
    TEST_ASSERT_EQUAL_HEX16(0x0000, f);
}

static void test_command_values_gated_while_spinning(void)
{
    TEST_ASSERT_NULL(dshot_txctl_set_value(&t, 5, false));
    TEST_ASSERT_NULL(dshot_txctl_set_value(&t, 6, false)); /* komuttan komuta geçiş serbest */
    TEST_ASSERT_NULL(dshot_txctl_set_value(&t, 1046, false));
    TEST_ASSERT_EQUAL_STRING("busy", dshot_txctl_set_value(&t, 5, false));
    TEST_ASSERT_NULL(dshot_txctl_set_value(&t, 0, false));
    TEST_ASSERT_NULL(dshot_txctl_set_value(&t, 5, false));
    TEST_ASSERT_EQUAL_STRING("range", dshot_txctl_set_value(&t, 2048, false));
}

static void test_speed_change_rejected_while_spinning(void)
{
    TEST_ASSERT_NULL(dshot_txctl_set_value(&t, 500, false));
    TEST_ASSERT_EQUAL_STRING("busy", dshot_txctl_set_speed(&t, 300));
    TEST_ASSERT_EQUAL(DSHOT_600, t.speed);
    dshot_txctl_stop(&t);
    TEST_ASSERT_NULL(dshot_txctl_set_speed(&t, 300));
    TEST_ASSERT_EQUAL(DSHOT_300, t.speed);
}

static void test_rate_limits(void)
{
    TEST_ASSERT_EQUAL_STRING("rate", dshot_txctl_set_rate(&t, 0));
    TEST_ASSERT_EQUAL_STRING("rate", dshot_txctl_set_rate(&t, 20000));
    TEST_ASSERT_NULL(dshot_txctl_set_rate(&t, 10000));
    TEST_ASSERT_EQUAL_STRING("rate", dshot_txctl_set_speed(&t, 150)); /* 10 kHz DShot150'ye sığmaz */
    TEST_ASSERT_EQUAL(DSHOT_600, t.speed);
}

static void test_special_command_frames(void)
{
    TEST_ASSERT_NULL(dshot_txctl_start_cmd(&t, 21, 3));
    TEST_ASSERT_TRUE(dshot_txctl_cmd_active(&t));
    TEST_ASSERT_EQUAL_STRING("busy", dshot_txctl_set_value(&t, 500, false));
    uint16_t f;
    for (unsigned i = 0; i < 3u; i++) {
        TEST_ASSERT_TRUE(dshot_txctl_next_frame(&t, &f));
        TEST_ASSERT_EQUAL_HEX16(dshot_encode(21, true, false), f);
    }
    TEST_ASSERT_FALSE(dshot_txctl_cmd_active(&t));
    TEST_ASSERT_FALSE(t.cmd_aborted);
    dshot_txctl_next_frame(&t, &f);
    TEST_ASSERT_EQUAL_HEX16(0x0000, f);
}

static void test_special_command_requires_zero(void)
{
    dshot_txctl_set_value(&t, 500, false);
    TEST_ASSERT_EQUAL_STRING("busy", dshot_txctl_start_cmd(&t, 1, 1));
    dshot_txctl_set_value(&t, 0, false);
    dshot_txctl_set_enabled(&t, false);
    TEST_ASSERT_EQUAL_STRING("disabled", dshot_txctl_start_cmd(&t, 1, 1));
}

static void test_stop_aborts_command(void)
{
    dshot_txctl_start_cmd(&t, 12, 10);
    dshot_txctl_stop(&t);
    TEST_ASSERT_FALSE(dshot_txctl_cmd_active(&t));
    TEST_ASSERT_TRUE(t.cmd_aborted);
}

static void test_disabled_outputs_nothing(void)
{
    uint16_t f;
    dshot_txctl_set_enabled(&t, false);
    TEST_ASSERT_FALSE(dshot_txctl_next_frame(&t, &f));
}

static void test_timeout_failsafe(void)
{
    dshot_txctl_set_value(&t, 500, false);
    TEST_ASSERT_FALSE(dshot_txctl_tick(&t, 499));
    TEST_ASSERT_TRUE(dshot_txctl_tick(&t, 500));
    TEST_ASSERT_EQUAL_UINT16(0, t.value);
    TEST_ASSERT_FALSE(dshot_txctl_tick(&t, 600));
    dshot_txctl_touch(&t, 700);
    dshot_txctl_set_value(&t, 500, false);
    TEST_ASSERT_FALSE(dshot_txctl_tick(&t, 1199));
    TEST_ASSERT_TRUE(dshot_txctl_tick(&t, 1200));
}

static void test_timeout_with_zero_output_reports_nothing(void)
{
    TEST_ASSERT_FALSE(dshot_txctl_tick(&t, 10000));
}

static void test_timeout_zero_disables_failsafe(void)
{
    dshot_txctl_set_timeout(&t, 0);
    dshot_txctl_set_value(&t, 500, false);
    TEST_ASSERT_FALSE(dshot_txctl_tick(&t, 100000));
    TEST_ASSERT_FALSE(dshot_txctl_dtr_drop(&t));
    TEST_ASSERT_EQUAL_UINT16(500, t.value);
}

static void test_dtr_drop(void)
{
    dshot_txctl_set_value(&t, 500, false);
    TEST_ASSERT_TRUE(dshot_txctl_dtr_drop(&t));
    TEST_ASSERT_EQUAL_UINT16(0, t.value);
}

static void test_bidir_mode(void)
{
    uint16_t f;
    TEST_ASSERT_NULL(dshot_txctl_set_bidir(&t, true));
    dshot_txctl_next_frame(&t, &f);
    TEST_ASSERT_EQUAL_HEX16(0x000F, f); /* ters CRC */
    dshot_txctl_set_value(&t, 1046, false);
    dshot_txctl_next_frame(&t, &f);
    TEST_ASSERT_EQUAL_HEX16(0x82C9, f);
    TEST_ASSERT_EQUAL_STRING("busy", dshot_txctl_set_bidir(&t, false));
    dshot_txctl_stop(&t);
    TEST_ASSERT_EQUAL_STRING("rate", dshot_txctl_set_rate(&t, 19000));
    TEST_ASSERT_NULL(dshot_txctl_set_bidir(&t, false));
    TEST_ASSERT_NULL(dshot_txctl_set_rate(&t, 19000));
    TEST_ASSERT_EQUAL_STRING("rate", dshot_txctl_set_bidir(&t, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_command_values_gated_while_spinning);
    RUN_TEST(test_speed_change_rejected_while_spinning);
    RUN_TEST(test_rate_limits);
    RUN_TEST(test_special_command_frames);
    RUN_TEST(test_special_command_requires_zero);
    RUN_TEST(test_stop_aborts_command);
    RUN_TEST(test_disabled_outputs_nothing);
    RUN_TEST(test_timeout_failsafe);
    RUN_TEST(test_timeout_with_zero_output_reports_nothing);
    RUN_TEST(test_timeout_zero_disables_failsafe);
    RUN_TEST(test_dtr_drop);
    RUN_TEST(test_bidir_mode);
    return UNITY_END();
}
