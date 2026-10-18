#include "dshot_tx_buf.h"

dshot_tx_timing_t dshot_tx_timing(uint32_t tim_hz, dshot_speed_t speed)
{
    dshot_tx_timing_t t = {0, 0, 0};
    if (!dshot_speed_valid((uint32_t)speed)) {
        return t;
    }
    uint32_t bitrate = (uint32_t)speed * 1000u;
    uint32_t period = (tim_hz + bitrate / 2u) / bitrate;
    t.period = (uint16_t)period;
    t.t0h = (uint16_t)((period * 3u + 4u) / 8u); /* %37.5 */
    t.t1h = (uint16_t)((period * 3u + 2u) / 4u); /* %75 */
    return t;
}

void dshot_tx_fill(uint16_t frame, const dshot_tx_timing_t *t, uint16_t buf[DSHOT_TX_BUF_LEN])
{
    for (unsigned i = 0; i < 16u; i++) {
        buf[i] = (frame & (0x8000u >> i)) ? t->t1h : t->t0h;
    }
    buf[16] = 0;
    buf[17] = 0;
}

uint32_t dshot_max_rate_hz(dshot_speed_t speed)
{
    uint32_t bit_ns = dshot_bit_ns(speed);
    if (bit_ns == 0u) {
        return 0;
    }
    return 1000000000u / (18u * bit_ns + 20000u);
}

uint32_t dshot_max_rate_bidir_hz(dshot_speed_t speed)
{
    uint32_t bit_ns = dshot_bit_ns(speed);
    if (bit_ns == 0u) {
        return 0;
    }
    /* 18 bit frame + 40 µs ESC gecikmesi + 21 cevap biti (her biri 0.8 bit) + 20 µs pay */
    return 1000000000u / (18u * bit_ns + 40000u + 21u * bit_ns * 4u / 5u + 20000u);
}
