#ifndef BURST_HELPER_H
#define BURST_HELPER_H

#include <string.h>
#include "dshot_rx_decode.h"

/* Verilen frame için ideal yakalama değerleri (T0H %37.5, T1H %75) */
static inline dshot_rx_burst_t make_burst(uint16_t frame, uint16_t bit_ticks, bool idle_high)
{
    dshot_rx_burst_t b;
    memset(&b, 0, sizeof b);
    b.n = DSHOT_BURST_PULSES;
    b.idle_high = idle_high;
    for (unsigned i = 0; i < DSHOT_BURST_PULSES; i++) {
        bool one = (frame & (0x8000u >> i)) != 0u;
        b.periods[i] = i == 0u ? 60000u : bit_ticks;
        b.highs[i] = (uint16_t)(one ? (bit_ticks * 3u + 2u) / 4u : (bit_ticks * 3u + 4u) / 8u);
    }
    return b;
}

#endif
