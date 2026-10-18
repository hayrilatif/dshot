#include <string.h>
#include <unity.h>
#include "app.h"
#include "burst_helper.h"
#include "proto.h"
#include "reply_helper.h"

static int apply_count;
static int inv_calls;
static bool last_inv;

static void fake_apply(const dshot_txctl_t *t)
{
    (void)t;
    apply_count++;
}

static void fake_inv(bool inv)
{
    inv_calls++;
    last_inv = inv;
}

static const app_hw_t HW_TXRX = {"blackpill", true, true, fake_apply, fake_inv};
static const app_hw_t HW_RX = {"bluepill", false, true, NULL, fake_inv};

static app_t app;
static char out[APP_OUT_MAX];

static size_t cmd(const char *s, uint32_t now)
{
    char line[PROTO_LINE_MAX];
    strncpy(line, s, sizeof line - 1u);
    line[sizeof line - 1u] = '\0';
    memset(out, 0, sizeof out);
    return app_handle_line(&app, line, now, out, sizeof out);
}

static size_t poll(uint32_t now, bool dtr)
{
    memset(out, 0, sizeof out);
    return app_poll(&app, now, dtr, out, sizeof out);
}

static size_t burst(uint16_t frame, bool idle_high)
{
    dshot_rx_burst_t b = make_burst(frame, 140, idle_high);
    memset(out, 0, sizeof out);
    return app_rx_burst(&app, &b, 84000000u, out, sizeof out);
}

void setUp(void)
{
    apply_count = 0;
    inv_calls = 0;
    last_inv = false;
    app_init(&app, &HW_TXRX, 0x431, 0);
}
void tearDown(void) {}

static void test_status_txrx(void)
{
    cmd("STATUS", 0);
    TEST_ASSERT_EQUAL_STRING("OK board=blackpill caps=tx,rx dev=0x431 fw=1.0.0 tx_speed=600 tx_rate=1000 tx_val=0 "
                             "tx_tlm=0 tx_en=1 tx_timeout=500 tx_bidir=0 tx_telem=0 tx_3d=0 tx_poles=14 tx_rpm=0 tx_rpm_rev=0 "
                             "tx_rpm_on=0 tx_rpmmax=300 tx_kp=50 tx_ki=1000 rx_pol=auto rx_inv=0 rx_stream=0\n",
                             out);
}

static void test_status_rx_only(void)
{
    app_init(&app, &HW_RX, 0x410, 0);
    cmd("status", 0);
    TEST_ASSERT_EQUAL_STRING("OK board=bluepill caps=rx dev=0x410 fw=1.0.0 rx_pol=auto rx_inv=0 rx_stream=0\n", out);
    cmd("TX THR 5", 0);
    TEST_ASSERT_EQUAL_STRING("ERR unsupported\n", out);
}

static void test_errors_and_empty(void)
{
    cmd("FOO", 0);
    TEST_ASSERT_EQUAL_STRING("ERR unknown\n", out);
    cmd("TX THR 5000", 0);
    TEST_ASSERT_EQUAL_STRING("ERR range\n", out);
    TEST_ASSERT_EQUAL(0, cmd("", 0));
}

static void test_speed_applies(void)
{
    cmd("TX SPEED 300", 0);
    TEST_ASSERT_EQUAL_STRING("OK\n", out);
    TEST_ASSERT_EQUAL(1, apply_count);
    TEST_ASSERT_EQUAL(DSHOT_300, app.tx.speed);
    cmd("TX THR 100", 0);
    cmd("TX SPEED 600", 0);
    TEST_ASSERT_EQUAL_STRING("ERR busy\n", out);
    TEST_ASSERT_EQUAL(1, apply_count);
}

static void test_thr_mapping(void)
{
    cmd("TX THR 1", 0);
    TEST_ASSERT_EQUAL_UINT16(48, app.tx.value);
    cmd("TX THR 2000", 0);
    TEST_ASSERT_EQUAL_UINT16(2047, app.tx.value);
    cmd("TX THR 0", 0);
    TEST_ASSERT_EQUAL_UINT16(0, app.tx.value);
    cmd("TX RAW 1046 1", 0);
    TEST_ASSERT_EQUAL_UINT16(1046, app.tx.value);
    TEST_ASSERT_TRUE(app.tx.tlm);
}

static void test_cmd_deferred_reply(void)
{
    TEST_ASSERT_EQUAL(0, cmd("TX CMD 21 3", 0));
    TEST_ASSERT_TRUE(app_busy(&app));
    TEST_ASSERT_EQUAL(0, poll(1, true));
    uint16_t f;
    for (int i = 0; i < 3; i++) {
        dshot_txctl_next_frame(&app.tx, &f);
    }
    poll(2, true);
    TEST_ASSERT_EQUAL_STRING("OK\n", out);
    TEST_ASSERT_FALSE(app_busy(&app));
}

static void test_cmd_aborted_by_dtr_drop(void)
{
    poll(0, true);
    cmd("TX CMD 12 10", 1);
    poll(2, false);
    TEST_ASSERT_EQUAL_STRING("#TX FAILSAFE\nERR aborted\n", out);
}

static void test_failsafe_timeout(void)
{
    cmd("TX THR 500", 0);
    TEST_ASSERT_EQUAL(0, poll(499, true));
    poll(500, true);
    TEST_ASSERT_EQUAL_STRING("#TX FAILSAFE\n", out);
    TEST_ASSERT_EQUAL_UINT16(0, app.tx.value);
    TEST_ASSERT_EQUAL(0, poll(600, true));
}

static void test_ping_keeps_alive(void)
{
    cmd("TX THR 500", 0);
    cmd("PING", 400);
    TEST_ASSERT_EQUAL(0, poll(899, true));
    poll(900, true);
    TEST_ASSERT_EQUAL_STRING("#TX FAILSAFE\n", out);
}

static void test_dtr_drop_failsafe(void)
{
    poll(0, true);
    cmd("TX THR 500", 1);
    poll(2, false);
    TEST_ASSERT_EQUAL_STRING("#TX FAILSAFE\n", out);
}

static void test_timeout_zero_disables_failsafe(void)
{
    cmd("TX TIMEOUT 0", 0);
    cmd("TX THR 500", 0);
    poll(1, true);
    TEST_ASSERT_EQUAL(0, poll(100000, false));
    TEST_ASSERT_EQUAL_UINT16(547, app.tx.value);
}

static void test_rx_stream(void)
{
    cmd("RX STREAM 100", 0);
    for (int i = 0; i < 5; i++) {
        burst(0x82C6, false);
    }
    TEST_ASSERT_EQUAL(0, poll(99, true));
    poll(100, true);
    const char *expect = "#RX n=5 rate=50 val=1046 vmin=1046 vmax=1046 tlm=0 crc_err=0 other=0 inv=0 speed=600 "
                         "bit_ns=1667";
    TEST_ASSERT_EQUAL_STRING_LEN(expect, out, strlen(expect));
    poll(200, true);
    TEST_ASSERT_EQUAL_STRING_LEN("#RX n=0 ", out, 8);
}

static void test_rx_reset(void)
{
    cmd("RX STREAM 100", 0);
    burst(0x82C6, false);
    cmd("RX RESET", 50);
    TEST_ASSERT_EQUAL(0, poll(149, true));
    poll(150, true);
    TEST_ASSERT_EQUAL_STRING_LEN("#RX n=0 ", out, 8);
}

static void test_rx_other_counted(void)
{
    dshot_rx_burst_t b = make_burst(0x82C6, 140, false);
    b.n = 15;
    app_rx_burst(&app, &b, 84000000u, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT32(1, app.stats.other);
}

static void test_rx_pol_auto_flips(void)
{
    for (int i = 0; i < 7; i++) {
        burst(0x82C6, true);
    }
    TEST_ASSERT_EQUAL(0, inv_calls);
    burst(0x82C6, true);
    TEST_ASSERT_EQUAL(1, inv_calls);
    TEST_ASSERT_TRUE(last_inv);
    TEST_ASSERT_TRUE(app_rx_inverted(&app));
    dshot_stats_reset(&app.stats);
    burst(0x82C9, true); /* ters CRC'li frame */
    TEST_ASSERT_EQUAL_UINT32(1, app.stats.n);
    TEST_ASSERT_TRUE(app.stats.inverted);
}

static void test_rx_pol_fixed_ignores_idle(void)
{
    cmd("RX POL NORMAL", 0);
    TEST_ASSERT_EQUAL(1, inv_calls);
    TEST_ASSERT_FALSE(last_inv);
    for (int i = 0; i < 20; i++) {
        burst(0x82C6, true);
    }
    TEST_ASSERT_EQUAL(1, inv_calls);
    TEST_ASSERT_EQUAL_UINT32(20, app.stats.n);
}

static void test_rx_raw_reports_bad_bursts(void)
{
    dshot_rx_burst_t b = make_burst(0x82C6, 140, false);
    b.n = 15;
    TEST_ASSERT_EQUAL(0, app_rx_burst(&app, &b, 84000000u, out, sizeof out));
    cmd("RX RAW 100", 0);
    app_rx_burst(&app, &b, 84000000u, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("#B n=15 idle=0\n", out);
}

static void test_rx_raw_every_2(void)
{
    cmd("RX RAW 2", 0);
    TEST_ASSERT_EQUAL(0, burst(0x82C6, false));
    burst(0x82C6, false);
    TEST_ASSERT_EQUAL_STRING("#F val=1046 tlm=0 crc=ok inv=0 raw=0x82C6 bit_ns=1667\n", out);
}

static void test_bidir_telemetry_stream(void)
{
    cmd("TX BIDIR 1", 0);
    TEST_ASSERT_EQUAL_STRING("OK\n", out);
    TEST_ASSERT_EQUAL(1, apply_count);
    cmd("TX TELEM 100", 0);
    dshot_reply_t r = make_reply(0x3F4, 112);
    app_tx_reply(&app, &r, 84000000u);
    r = make_reply(0x223, 112);
    app_tx_reply(&app, &r, 84000000u);
    r.count = 0;
    app_tx_reply(&app, &r, 84000000u);
    poll(100, true);
    TEST_ASSERT_EQUAL_STRING("#T ok=2 bad=0 miss=1 erpm=60000 temp_c=35 volt_mv=- curr_a=- stress=- status=-\n", out);
    cmd("TX THR 100", 100);
    cmd("TX BIDIR 0", 100);
    TEST_ASSERT_EQUAL_STRING("ERR busy\n", out);
}

static void feed_erpm(uint32_t erpm, int frames)
{
    /* period_us = 60e6/erpm, normalize: mantis 9 bit, MSB set */
    uint32_t period = 60000000u / erpm, e = 0;
    while (period > 511u) {
        period >>= 1;
        e++;
    }
    while (e > 0u && period < 256u) { /* normalize edilmiş biçim */
        period <<= 1;
        e--;
    }
    uint16_t v12 = (uint16_t)((e << 9) | period);
    for (int i = 0; i < frames; i++) {
        dshot_reply_t r = make_reply(v12, 112);
        app_tx_reply(&app, &r, 84000000u);
    }
}

static void test_rpm_mode_requires_bidir_and_runs(void)
{
    cmd("TX RPM 3000", 0);
    TEST_ASSERT_EQUAL_STRING("ERR bidir\n", out);
    cmd("TX RPM 3000 1", 0);
    TEST_ASSERT_EQUAL_STRING("ERR bidir\n", out);
    cmd("TX BIDIR 1", 0);
    cmd("TX RPM 3000 1", 0);
    TEST_ASSERT_EQUAL_STRING("ERR 3d\n", out);
    cmd("TX RPM 3000", 0);
    TEST_ASSERT_EQUAL_STRING("OK\n", out);
    feed_erpm(7000, 50); /* 1000 RPM < hedef → throttle artmalı */
    TEST_ASSERT_TRUE(app.tx.value >= 48u);
    uint16_t v = app.tx.value;
    feed_erpm(7000, 50);
    TEST_ASSERT_TRUE(app.tx.value > v);
    cmd("TX THR 0", 1);
    TEST_ASSERT_FALSE(app.rpm.active);
    TEST_ASSERT_EQUAL_UINT16(0, app.tx.value);
}

static void test_rpm_mode_3d_reverse_range(void)
{
    cmd("TX BIDIR 1", 0);
    cmd("TX 3D 1", 0);
    cmd("TX RPM 2000 1", 0);
    TEST_ASSERT_EQUAL_STRING("OK\n", out);
    feed_erpm(7000, 100);
    TEST_ASSERT_TRUE(app.tx.value >= 48u && app.tx.value <= 1047u);
    cmd("TX RPM 2000", 0);
    feed_erpm(7000, 100);
    TEST_ASSERT_TRUE(app.tx.value >= 1049u);
}

static void test_rpm_lost_telemetry_stops(void)
{
    cmd("TX BIDIR 1", 0);
    cmd("TX RPM 3000", 0);
    feed_erpm(7000, 50);
    dshot_reply_t none;
    memset(&none, 0, sizeof none);
    for (int i = 0; i < 100; i++) {
        app_tx_reply(&app, &none, 84000000u);
    }
    TEST_ASSERT_EQUAL_UINT16(0, app.tx.value);
    TEST_ASSERT_FALSE(app.rpm.active);
    poll(10, true);
    TEST_ASSERT_EQUAL_STRING("#TX RPM LOST\n", out);
}

static void test_rpm_failsafe_stops_controller(void)
{
    cmd("TX BIDIR 1", 0);
    cmd("TX RPM 3000", 0);
    feed_erpm(7000, 50);
    poll(600, true);
    TEST_ASSERT_EQUAL_STRING_LEN("#TX FAILSAFE\n", out, 13);
    TEST_ASSERT_FALSE(app.rpm.active);
    feed_erpm(7000, 50);
    TEST_ASSERT_EQUAL_UINT16(0, app.tx.value);
}

static void test_rpm_settings(void)
{
    cmd("TX POLES 12", 0);
    TEST_ASSERT_EQUAL_UINT16(12, app.rpm.poles);
    cmd("TX POLES 13", 0);
    TEST_ASSERT_EQUAL_STRING("ERR range\n", out);
    cmd("TX RPMMAX 500", 0);
    TEST_ASSERT_EQUAL_UINT16(500, app.rpm.max_mag);
    cmd("TX THR 10", 0);
    cmd("TX 3D 1", 0);
    TEST_ASSERT_EQUAL_STRING("ERR busy\n", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_txrx);
    RUN_TEST(test_status_rx_only);
    RUN_TEST(test_errors_and_empty);
    RUN_TEST(test_speed_applies);
    RUN_TEST(test_thr_mapping);
    RUN_TEST(test_cmd_deferred_reply);
    RUN_TEST(test_cmd_aborted_by_dtr_drop);
    RUN_TEST(test_failsafe_timeout);
    RUN_TEST(test_ping_keeps_alive);
    RUN_TEST(test_dtr_drop_failsafe);
    RUN_TEST(test_timeout_zero_disables_failsafe);
    RUN_TEST(test_rx_stream);
    RUN_TEST(test_rx_reset);
    RUN_TEST(test_rx_other_counted);
    RUN_TEST(test_rx_pol_auto_flips);
    RUN_TEST(test_rx_pol_fixed_ignores_idle);
    RUN_TEST(test_rx_raw_reports_bad_bursts);
    RUN_TEST(test_rx_raw_every_2);
    RUN_TEST(test_bidir_telemetry_stream);
    RUN_TEST(test_rpm_mode_requires_bidir_and_runs);
    RUN_TEST(test_rpm_mode_3d_reverse_range);
    RUN_TEST(test_rpm_lost_telemetry_stops);
    RUN_TEST(test_rpm_failsafe_stops_controller);
    RUN_TEST(test_rpm_settings);
    return UNITY_END();
}
