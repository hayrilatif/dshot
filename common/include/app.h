#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dshot_rx_decode.h"
#include "dshot_rx_stats.h"
#include "dshot_txctl.h"

#define APP_FW_VERSION         "1.0.0"
#define APP_OUT_MAX            384u
#define APP_CMD_DEFAULT_REPEAT 10u

/* Karta özgü bilgiler ve donanım geri çağrıları */
typedef struct {
    const char *board;
    bool has_tx;
    bool has_rx;
    void (*tx_apply)(const dshot_txctl_t *tx);   /* hız / rate değişti */
    void (*rx_set_inverted)(bool inverted);      /* yakalama polaritesi */
} app_hw_t;

typedef struct {
    const app_hw_t *hw;
    uint32_t dev_id;
    dshot_txctl_t tx;
    bool last_dtr;
    bool cmd_reply_pending;
    uint8_t rx_pol_mode;
    dshot_pol_t pol;
    dshot_rx_stats_t stats;
    uint32_t stream_ms;
    uint32_t last_stream_ms;
    uint32_t raw_every;
    uint32_t raw_count;
} app_t;

void app_init(app_t *a, const app_hw_t *hw, uint32_t dev_id, uint32_t now_ms);
/* Komut satırını işler, cevabı out'a yazar. TX CMD'de cevap ertelenir (0 döner, app_poll yazar). */
size_t app_handle_line(app_t *a, char *line, uint32_t now_ms, char *out, size_t cap);
/* Yakalanan bir burst'ü işler; RX RAW açıksa "#F ..." yazabilir. */
size_t app_rx_burst(app_t *a, const dshot_rx_burst_t *b, uint32_t tick_hz, char *out, size_t cap);
/* Failsafe, ertelenmiş CMD cevabı ve periyodik "#RX" özeti. */
size_t app_poll(app_t *a, uint32_t now_ms, bool dtr, char *out, size_t cap);
/* Ertelenmiş cevap beklenirken yeni komut okunmaz */
bool app_busy(const app_t *a);
bool app_rx_inverted(const app_t *a);

#endif
