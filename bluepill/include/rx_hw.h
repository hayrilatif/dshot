#ifndef RX_HW_H
#define RX_HW_H

#include <stdbool.h>
#include <stdint.h>
#include "dshot_rx_decode.h"

/* PB6 / TIM4 PWM input: CH1 başlangıç kenarı (periyot), CH2 bitiş kenarı (high süresi), DMA1 Channel1/4 */
void rx_hw_init(bool inverted);
void rx_hw_set_inverted(bool inverted);
bool rx_hw_pop(dshot_rx_burst_t *out);
uint32_t rx_hw_tick_hz(void);
uint32_t rx_hw_dropped(void);

#endif
