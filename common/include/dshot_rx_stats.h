#ifndef DSHOT_RX_STATS_H
#define DSHOT_RX_STATS_H

#include <stddef.h>
#include <stdint.h>
#include "dshot_rx_decode.h"

typedef struct {
    uint32_t n;       /* CRC'si doğru frame */
    uint32_t crc_err; /* 16 pals ama CRC yanlış */
    uint32_t other;   /* 16 olmayan burst (gürültü, ESC cevabı...) */
    uint16_t last_val;
    uint16_t vmin;
    uint16_t vmax;
    uint8_t tlm_mask; /* bit0: tlm=0 görüldü, bit1: tlm=1 görüldü */
    bool inverted;
    dshot_speed_t speed;
    uint32_t bit_ns;
    uint32_t t0h_ns;
    uint32_t t1h_ns;
} dshot_rx_stats_t;

void dshot_stats_reset(dshot_rx_stats_t *s);
void dshot_stats_add(dshot_rx_stats_t *s, const dshot_burst_result_t *r, bool inverted);
/* "#RX ..." satırı; yazılan bayt sayısını döner */
size_t dshot_stats_format(const dshot_rx_stats_t *s, uint32_t interval_ms, char *out, size_t cap);

#endif
