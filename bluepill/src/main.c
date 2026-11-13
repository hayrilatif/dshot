#include <string.h>
#include "stm32f1xx_hal.h"
#include "app.h"
#include "board.h"
#include "cdc_io.h"
#include "proto.h"
#if BOARD_HAS_TX
#include "tx_hw.h"
#endif
#if BOARD_HAS_RX
#include "rx_hw.h"
#endif

#define REPLY_WAIT_MS 50u

#if BOARD_HAS_TX
/* Frame kesmesi app durumunu okurken ana döngü değiştirmesin */
#define FRAME_IRQ_OFF() NVIC_DisableIRQ(TIM2_IRQn)
#define FRAME_IRQ_ON()  NVIC_EnableIRQ(TIM2_IRQn)
#else
#define FRAME_IRQ_OFF() do { } while (0)
#define FRAME_IRQ_ON()  do { } while (0)
#endif

static app_t app;
static char out[APP_OUT_MAX];

#if BOARD_HAS_TX
static void hw_tx_apply(const dshot_txctl_t *tx)
{
    tx_hw_apply(tx);
}
#endif

#if BOARD_HAS_RX
static void hw_rx_set_inverted(bool inverted)
{
    rx_hw_set_inverted(inverted);
}
#endif

static const app_hw_t hw = {
    .board = "bluepill",
    .has_tx = BOARD_HAS_TX,
    .has_rx = BOARD_HAS_RX,
#if BOARD_HAS_TX
    .tx_apply = hw_tx_apply,
#endif
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

int main(void)
{
    board_init();
    app_init(&app, &hw, HAL_GetDEVID(), HAL_GetTick());
#if BOARD_HAS_TX
    tx_hw_init(&app.tx);
#endif
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
                    /* F103'ün ROM bootloader'ı USB DFU desteklemez: yükleme ST-Link ile */
                    cdc_write_wait("ERR unsupported\n", 16, REPLY_WAIT_MS);
                    continue;
                }
                FRAME_IRQ_OFF();
                size_t n = app_handle_line(&app, l, HAL_GetTick(), out, sizeof out);
                FRAME_IRQ_ON();
                cdc_write_wait(out, (uint16_t)n, REPLY_WAIT_MS);
            } else if (r == PROTO_LINE_OVERFLOW) {
                cdc_write_wait("ERR too long\n", 13, REPLY_WAIT_MS);
            }
        }
#if BOARD_HAS_TX
        dshot_reply_t reply;
        while (tx_hw_pop_reply(&reply)) {
            app_tx_reply(&app, &reply, tx_hw_tick_hz());
        }
#endif
#if BOARD_HAS_RX
        dshot_rx_burst_t b;
        while (rx_hw_pop(&b)) {
            size_t n = app_rx_burst(&app, &b, rx_hw_tick_hz(), out, sizeof out);
            cdc_write(out, (uint16_t)n); /* asenkron: sığmazsa atılır */
        }
#endif
        FRAME_IRQ_OFF();
        size_t n = app_poll(&app, HAL_GetTick(), cdc_dtr(), out, sizeof out);
        FRAME_IRQ_ON();
        cdc_write_wait(out, (uint16_t)n, REPLY_WAIT_MS);
        cdc_flush();

        if (HAL_GetTick() - led_ms >= 500u) {
            led_ms = HAL_GetTick();
            led = !led;
            board_led(led);
        }
    }
}
