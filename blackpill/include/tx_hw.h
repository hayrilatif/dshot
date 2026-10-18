#ifndef TX_HW_H
#define TX_HW_H

#include "dshot_txctl.h"

/* PA8 / TIM1_CH1 + DMA2 Stream5 (TIM1_UP), frame zamanlayıcı TIM2 */
void tx_hw_init(dshot_txctl_t *ctl);
/* Hız ve frame hızını donanıma uygular. TIM2 kesmesi maskeliyken çağrılmalı. */
void tx_hw_apply(const dshot_txctl_t *ctl);

#endif
