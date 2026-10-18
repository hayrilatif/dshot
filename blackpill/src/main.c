/* Geçici: USB CDC echo denemesi */
#include "stm32f4xx_hal.h"
#include "board.h"
#include "cdc_io.h"

int main(void)
{
    board_init();
    cdc_init();
    uint32_t led_ms = 0;
    bool led = false;
    for (;;) {
        uint8_t buf[64];
        uint16_t n = cdc_read(buf, sizeof buf);
        if (n) {
            cdc_write_wait((const char *)buf, n, 50);
        }
        cdc_flush();
        if (HAL_GetTick() - led_ms >= 500u) {
            led_ms = HAL_GetTick();
            led = !led;
            board_led(led);
        }
    }
}
