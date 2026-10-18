#include <string.h>
#include "dshot_rx_decode.h"

static uint32_t ticks_to_ns(uint64_t ticks, uint32_t count, uint32_t tick_hz)
{
    if (count == 0u || tick_hz == 0u) {
        return 0;
    }
    uint64_t den = (uint64_t)count * tick_hz;
    return (uint32_t)((ticks * 1000000000ull + den / 2u) / den);
}

static uint16_t median15(const uint16_t *p)
{
    uint16_t s[15];
    for (unsigned i = 0; i < 15u; i++) {
        uint16_t v = p[i];
        unsigned j = i;
        while (j > 0u && s[j - 1u] > v) {
            s[j] = s[j - 1u];
            j--;
        }
        s[j] = v;
    }
    return s[7];
}

dshot_speed_t dshot_speed_from_bit_ns(uint32_t bit_ns)
{
    static const dshot_speed_t all[] = {DSHOT_150, DSHOT_300, DSHOT_600, DSHOT_1200};
    for (unsigned i = 0; i < 4u; i++) {
        uint32_t nom = dshot_bit_ns(all[i]);
        uint32_t diff = bit_ns > nom ? bit_ns - nom : nom - bit_ns;
        if (diff * 10u <= nom) {
            return all[i];
        }
    }
    return DSHOT_SPEED_UNKNOWN;
}

dshot_burst_result_t dshot_decode_burst(const dshot_rx_burst_t *b, uint32_t tick_hz, bool inverted)
{
    dshot_burst_result_t r;
    memset(&r, 0, sizeof r);
    if (b->n != DSHOT_BURST_PULSES) {
        return r;
    }
    r.is_frame = true;
    /* periods[0] önceki frame'den kalan boşluktur; bit süresi = kalan 15'in medyanı */
    uint16_t bit = median15(&b->periods[1]);
    uint64_t sum0 = 0, sum1 = 0;
    uint32_t c0 = 0, c1 = 0;
    uint16_t raw = 0;
    for (unsigned i = 0; i < DSHOT_BURST_PULSES; i++) {
        uint16_t h = b->highs[i];
        raw = (uint16_t)(raw << 1);
        if ((uint32_t)h * 2u > bit) {
            raw |= 1u;
            sum1 += h;
            c1++;
        } else {
            sum0 += h;
            c0++;
        }
    }
    r.raw = raw;
    r.frame = dshot_decode(raw, inverted);
    r.bit_ns = ticks_to_ns(bit, 1, tick_hz);
    r.t0h_ns = ticks_to_ns(sum0, c0, tick_hz);
    r.t1h_ns = ticks_to_ns(sum1, c1, tick_hz);
    r.speed = dshot_speed_from_bit_ns(r.bit_ns);
    return r;
}

void dshot_pol_init(dshot_pol_t *p, bool inverted)
{
    p->inverted = inverted;
    p->streak = 0;
}

bool dshot_pol_update(dshot_pol_t *p, bool idle_high)
{
    if (idle_high == p->inverted) {
        p->streak = 0;
        return false;
    }
    if (++p->streak < DSHOT_POL_SWITCH_COUNT) {
        return false;
    }
    p->inverted = !p->inverted;
    p->streak = 0;
    return true;
}
