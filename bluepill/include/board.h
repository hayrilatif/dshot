#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#if !defined(BOARD_HAS_TX) || !defined(BOARD_HAS_RX)
#error "BOARD_HAS_TX ve BOARD_HAS_RX build_flags ile tanımlanmalı"
#endif

#if BOARD_HAS_TX && BOARD_HAS_RX
#define BOARD_USB_PRODUCT "DShot TXRX"
#elif BOARD_HAS_TX
#define BOARD_USB_PRODUCT "DShot TX"
#else
#define BOARD_USB_PRODUCT "DShot RX"
#endif

void board_init(void);
void board_led(bool on);
uint32_t board_tim_apb1_hz(void);
uint32_t board_tim_apb2_hz(void);
void board_fatal(void);

#endif
