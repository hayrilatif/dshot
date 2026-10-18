#include <string.h>
#include "stm32f4xx_hal.h"
#include "app.h"
#include "board.h"
#include "cdc_io.h"
#include "proto.h"
#include "tx_hw.h"
#if BOARD_HAS_RX
#include "rx_hw.h"
#endif

#define REPLY_WAIT_MS 50u

static app_t app;
static char out[APP_OUT_MAX];

static void hw_tx_apply(const dshot_txctl_t *tx)
{
    tx_hw_apply(tx);
}

#if BOARD_HAS_RX
static void hw_rx_set_inverted(bool inverted)
{
    rx_hw_set_inverted(inverted);
}
#endif

static const app_hw_t hw = {
    .board = "blackpill",
    .has_tx = true,
    .has_rx = BOARD_HAS_RX,
    .tx_apply = hw_tx_apply,
#if BOARD_HAS_RX
    .rx_set_inverted = hw_rx_set_inverted,
#endif
};

static bool is_dfu_request(const char *line)
{
    char up[8];
    size_t n = 0;
    while (*line == ' ') {
        line++;
    }
    for (; line[n] && line[n] != ' ' && n < sizeof up - 1u; n++) {
        up[n] = (line[n] >= 'a' && line[n] <= 'z') ? (char)(line[n] - 32) : line[n];
    }
    up[n] = '\0';
    return strcmp(up, "DFU") == 0;
}

static void enter_dfu(void)
{
    NVIC_DisableIRQ(TIM2_IRQn);
    TIM1->CCR1 = 0;
    cdc_write_wait("OK\n", 3, REPLY_WAIT_MS);
    uint32_t t = HAL_GetTick();
    while (HAL_GetTick() - t < 50u) {
        cdc_flush();
    }
    board_enter_dfu();
}

int main(void)
{
    board_init();
    app_init(&app, &hw, HAL_GetDEVID(), HAL_GetTick());
    tx_hw_init(&app.tx);
#if BOARD_HAS_RX
    rx_hw_init(app_rx_inverted(&app));
#endif
    cdc_init();

    proto_line_t line;
    proto_line_init(&line);
    uint32_t led_ms = 0;
    bool led = false;

    for (;;) {
        uint8_t c;
        while (!app_busy(&app) && cdc_read(&c, 1) == 1u) {
            char *l;
            proto_line_result_t r = proto_line_feed(&line, (char)c, &l);
            if (r == PROTO_LINE_READY) {
                if (is_dfu_request(l)) {
                    enter_dfu();
                }
                NVIC_DisableIRQ(TIM2_IRQn);
                size_t n = app_handle_line(&app, l, HAL_GetTick(), out, sizeof out);
                NVIC_EnableIRQ(TIM2_IRQn);
                cdc_write_wait(out, (uint16_t)n, REPLY_WAIT_MS);
            } else if (r == PROTO_LINE_OVERFLOW) {
                cdc_write_wait("ERR too long\n", 13, REPLY_WAIT_MS);
            }
        }
#if BOARD_HAS_RX
        dshot_rx_burst_t b;
        while (rx_hw_pop(&b)) {
            size_t n = app_rx_burst(&app, &b, rx_hw_tick_hz(), out, sizeof out);
            cdc_write(out, (uint16_t)n); /* asenkron: sığmazsa atılır */
        }
#endif
        NVIC_DisableIRQ(TIM2_IRQn);
        size_t n = app_poll(&app, HAL_GetTick(), cdc_dtr(), out, sizeof out);
        NVIC_EnableIRQ(TIM2_IRQn);
        cdc_write_wait(out, (uint16_t)n, REPLY_WAIT_MS);
        cdc_flush();

        if (HAL_GetTick() - led_ms >= 500u) {
            led_ms = HAL_GetTick();
            led = !led;
            board_led(led);
        }
    }
}
