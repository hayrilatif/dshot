#ifndef DSHOT_RX_DECODE_H
#define DSHOT_RX_DECODE_H

#include <stdbool.h>
#include <stdint.h>
#include "dshot_frame.h"

#define DSHOT_BURST_PULSES 16u

/* Donanımın bir burst (≥10 µs sessizlikle biten pals dizisi) için yakaladıkları */
typedef struct {
    uint16_t periods[DSHOT_BURST_PULSES]; /* [i]: pals i'nin başında yakalanan, önceki palsın periyodu; [0] boşluk */
    uint16_t highs[DSHOT_BURST_PULSES];   /* pals i'nin aktif süresi */
    uint8_t n;                            /* pals sayısı (yakalama sayıları uyuşmazsa 0) */
    bool idle_high;                       /* burst sonrası hattın seviyesi */
} dshot_rx_burst_t;

typedef struct {
    bool is_frame; /* tam 16 pals */
    uint16_t raw;
    dshot_decoded_t frame;
    uint32_t bit_ns;
    uint32_t t0h_ns; /* 0 bitlerinin ortalama high süresi, yoksa 0 */
    uint32_t t1h_ns;
    dshot_speed_t speed;
} dshot_burst_result_t;

dshot_speed_t dshot_speed_from_bit_ns(uint32_t bit_ns);
dshot_burst_result_t dshot_decode_burst(const dshot_rx_burst_t *b, uint32_t tick_hz, bool inverted);

/* Otomatik polarite: boşta seviye art arda 8 kez moda ters düşerse mod değişir */
#define DSHOT_POL_SWITCH_COUNT 8u

typedef struct {
    bool inverted;
    uint8_t streak;
} dshot_pol_t;

void dshot_pol_init(dshot_pol_t *p, bool inverted);
/* Mod değiştiyse true */
bool dshot_pol_update(dshot_pol_t *p, bool idle_high);

#endif
