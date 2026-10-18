#ifndef REPLY_HELPER_H
#define REPLY_HELPER_H

#include <string.h>
#include "dshot_telem.h"

/* ESC tarafı kodlayıcı: 16 bit (değer + CRC) → yakalanacak kenar zamanları */
static inline dshot_reply_t make_reply_raw(uint16_t d, uint32_t bit_ticks)
{
    static const uint8_t GCR[16] = {0x19, 0x1B, 0x12, 0x13, 0x1D, 0x15, 0x16, 0x17,
                                    0x1A, 0x09, 0x0A, 0x0B, 0x1E, 0x0D, 0x0E, 0x0F};
    uint32_t gcr = 0;
    for (int n = 3; n >= 0; n--) {
        gcr = (gcr << 5) | GCR[(d >> (4 * n)) & 0xFu];
    }
    uint32_t t = (1u << 20) | gcr; /* başlangıç geçişi + GCR geçişleri, MSB önce */
    dshot_reply_t r;
    memset(&r, 0, sizeof r);
    uint16_t base = 1000;
    for (unsigned i = 0; i < 21u; i++) {
        if (t & (1u << (20u - i))) {
            r.edges[r.count++] = (uint16_t)(base + i * bit_ticks);
        }
    }
    return r;
}

static inline dshot_reply_t make_reply(uint16_t v12, uint32_t bit_ticks)
{
    uint16_t crc = (uint16_t)(~(v12 ^ (v12 >> 4) ^ (v12 >> 8)) & 0xFu);
    return make_reply_raw((uint16_t)((v12 << 4) | crc), bit_ticks);
}

#endif
