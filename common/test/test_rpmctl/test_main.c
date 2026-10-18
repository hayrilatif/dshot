#include <unity.h>
#include "dshot_rpmctl.h"

static dshot_rpmctl_t c;

void setUp(void)
{
    dshot_rpmctl_init(&c);
}
void tearDown(void) {}

/* Basit motor modeli: kararlı devir = mag * rpm_per_step, birinci derece gecikme */
static float sim_rpm;
static uint32_t motor_erpm(uint16_t value, float dt, float rpm_per_step, float tau)
{
    uint16_t mag = 0;
    if (value >= 1049u && c.mode3d) {
        mag = (uint16_t)(value - 1048u);
    } else if (value >= 48u) {
        mag = (uint16_t)(value - 47u);
    }
    float steady = mag < 20u ? 0.0f : (float)mag * rpm_per_step; /* düşük throttle'da kalkmaz */
    sim_rpm += (steady - sim_rpm) * dt / tau;
    return (uint32_t)(sim_rpm * 7.0f);
}

static void run(uint32_t frames, float rpm_per_step)
{
    uint16_t v = 0;
    bool lost;
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t erpm = motor_erpm(v, 0.001f, rpm_per_step, 0.05f);
        v = dshot_rpmctl_step(&c, (int32_t)erpm, 0.001f, &lost);
    }
}

static void test_inactive_outputs_zero(void)
{
    bool lost;
    TEST_ASSERT_EQUAL_UINT16(0, dshot_rpmctl_step(&c, 5000, 0.001f, &lost));
    TEST_ASSERT_FALSE(lost);
}

static void test_value_mapping(void)
{
    c.max_mag = 2000;
    TEST_ASSERT_EQUAL_UINT16(0, dshot_rpmctl_value(&c, 0));
    TEST_ASSERT_EQUAL_UINT16(48, dshot_rpmctl_value(&c, 1));
    TEST_ASSERT_EQUAL_UINT16(2047, dshot_rpmctl_value(&c, 2000));
    c.mode3d = true;
    TEST_ASSERT_EQUAL_UINT16(1049, dshot_rpmctl_value(&c, 1));
    TEST_ASSERT_EQUAL_UINT16(2047, dshot_rpmctl_value(&c, 2000)); /* 999'a kırpılır */
    c.reverse = true;
    TEST_ASSERT_EQUAL_UINT16(48, dshot_rpmctl_value(&c, 1));
    TEST_ASSERT_EQUAL_UINT16(1047, dshot_rpmctl_value(&c, 1000));
}

static void test_max_mag_caps_output(void)
{
    c.max_mag = 100;
    TEST_ASSERT_EQUAL_UINT16(147, dshot_rpmctl_value(&c, 500));
}

static void test_reaches_target(void)
{
    sim_rpm = 0;
    c.max_mag = 1000;
    dshot_rpmctl_set_target(&c, 3000, false);
    run(3000, 20.0f); /* 3 s, motor 20 RPM/adım → hedef ~150 adım */
    TEST_ASSERT_UINT32_WITHIN(60, 3000, c.last_rpm);
    run(1000, 20.0f);
    TEST_ASSERT_UINT32_WITHIN(30, 3000, c.last_rpm);
}

static void test_reaches_target_3d_both_directions(void)
{
    c.mode3d = true;
    c.max_mag = 999;
    sim_rpm = 0;
    dshot_rpmctl_set_target(&c, 2000, false);
    run(3000, 20.0f);
    TEST_ASSERT_UINT32_WITHIN(40, 2000, c.last_rpm);
    sim_rpm = 0;
    dshot_rpmctl_set_target(&c, 2000, true);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.out); /* yön değişti: sıfırdan */
    run(3000, 20.0f);
    TEST_ASSERT_UINT32_WITHIN(40, 2000, c.last_rpm);
}

static void test_unreachable_target_stays_at_cap(void)
{
    sim_rpm = 0;
    c.max_mag = 200;
    dshot_rpmctl_set_target(&c, 100000, false);
    run(2000, 20.0f);
    TEST_ASSERT_EQUAL_FLOAT(200.0f, c.out);
    TEST_ASSERT_TRUE(c.integ <= 200.0f);
    dshot_rpmctl_set_target(&c, 1000, false); /* windup olmadığı için hızla iner */
    run(1500, 20.0f);
    TEST_ASSERT_UINT32_WITHIN(40, 1000, c.last_rpm);
}

static void test_slew_limit(void)
{
    bool lost;
    c.max_mag = 2000;
    dshot_rpmctl_set_target(&c, 100000, false);
    uint16_t v = dshot_rpmctl_step(&c, 0, 0.001f, &lost);
    TEST_ASSERT_TRUE(v <= 47u + 2u); /* 1 ms'de en fazla 2 adım */
}

static void test_telemetry_loss_stops(void)
{
    bool lost = false;
    dshot_rpmctl_set_target(&c, 3000, false);
    dshot_rpmctl_step(&c, 7000, 0.001f, &lost);
    for (int i = 0; i < 99; i++) {
        dshot_rpmctl_step(&c, -1, 0.001f, &lost);
        TEST_ASSERT_FALSE(lost);
    }
    TEST_ASSERT_EQUAL_UINT16(0, dshot_rpmctl_step(&c, -1, 0.001f, &lost));
    TEST_ASSERT_TRUE(lost);
    TEST_ASSERT_FALSE(c.active);
}

static void test_zero_target_stops(void)
{
    dshot_rpmctl_set_target(&c, 3000, false);
    dshot_rpmctl_set_target(&c, 0, false);
    TEST_ASSERT_FALSE(c.active);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_inactive_outputs_zero);
    RUN_TEST(test_value_mapping);
    RUN_TEST(test_max_mag_caps_output);
    RUN_TEST(test_reaches_target);
    RUN_TEST(test_reaches_target_3d_both_directions);
    RUN_TEST(test_unreachable_target_stays_at_cap);
    RUN_TEST(test_slew_limit);
    RUN_TEST(test_telemetry_loss_stops);
    RUN_TEST(test_zero_target_stops);
    return UNITY_END();
}
