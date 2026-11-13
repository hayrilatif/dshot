#ifndef TX_HW_H
#define TX_HW_H

#include <stdbool.h>
#include "dshot_telem.h"
#include "dshot_txctl.h"

/* PA8 / TIM1_CH1. Gönderim: DMA1 Channel5 (TIM1_UP) → CCR1. Frame zamanlayıcı TIM2.
   Bidirectional: frame bitince (Channel5 TC) PA8 girişe geçer; F103 timer'ı iki kenarı birden
   yakalayamadığı için düşen kenarlar IC1 + DMA1 Channel2, yükselenler IC2 + DMA1 Channel3 ile toplanır.
   PB0: host'tan gelen motor komutunu taşıyan ilk frame'de tetik palsı (bir frame periyodu). */
void tx_hw_init(dshot_txctl_t *ctl);
/* Hız / frame hızı / bidir modunu uygular. TIM2 kesmesi maskeliyken çağrılmalı. */
void tx_hw_apply(const dshot_txctl_t *ctl);
bool tx_hw_pop_reply(dshot_reply_t *out);
uint32_t tx_hw_tick_hz(void);

#endif
