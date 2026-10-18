#ifndef DSHOT_TX_BUF_H
#define DSHOT_TX_BUF_H

#include <stdint.h>
#include "dshot_frame.h"

/* 16 bit + 2 sıfır (CCR preload gecikmesi ve hattı low bırakmak için) */
#define DSHOT_TX_BUF_LEN 18u

typedef struct {
    uint16_t period; /* bit süresi, timer tick (ARR = period - 1) */
    uint16_t t0h;
    uint16_t t1h;
} dshot_tx_timing_t;

dshot_tx_timing_t dshot_tx_timing(uint32_t tim_hz, dshot_speed_t speed);
void dshot_tx_fill(uint16_t frame, const dshot_tx_timing_t *t, uint16_t buf[DSHOT_TX_BUF_LEN]);
/* periyot >= 18 bit + 20 µs olacak en yüksek frame hızı */
uint32_t dshot_max_rate_hz(dshot_speed_t speed);

#endif
