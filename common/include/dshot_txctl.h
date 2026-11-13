#ifndef DSHOT_TXCTL_H
#define DSHOT_TXCTL_H

#include <stdbool.h>
#include <stdint.h>
#include "dshot_frame.h"

#define DSHOT_TX_DEFAULT_SPEED      DSHOT_600
#define DSHOT_TX_DEFAULT_RATE_HZ    1000u
#define DSHOT_TX_DEFAULT_TIMEOUT_MS 500u

/* Gönderici durumu. next_frame kesmeden, diğerleri ana döngüden (frame kesmesi maskeliyken) çağrılır. */
typedef struct {
    dshot_speed_t speed;
    uint32_t rate_hz;
    volatile uint16_t value; /* sürekli gönderilen değer 0..2047 */
    volatile bool tlm;
    volatile bool enabled;
    uint32_t timeout_ms; /* 0 = failsafe kapalı */
    uint32_t last_contact_ms;
    bool failsafe_active;
    volatile uint16_t cmd_value;
    volatile uint16_t cmd_remaining;
    bool cmd_aborted;
    volatile bool bidir; /* ters sinyal + ters CRC, ESC aynı kablodan cevap verir */
    volatile bool trig_pending; /* host'tan motor komutu geldi, tetik pini bekliyor */
} dshot_txctl_t;

void dshot_txctl_init(dshot_txctl_t *t, uint32_t now_ms);
/* Host'tan TX komutu veya PING geldi */
void dshot_txctl_touch(dshot_txctl_t *t, uint32_t now_ms);
/* Hata durumunda hata dizesi ("range", "busy", "rate", "disabled"), başarıda NULL */
const char *dshot_txctl_set_speed(dshot_txctl_t *t, uint32_t speed);
const char *dshot_txctl_set_rate(dshot_txctl_t *t, uint32_t rate_hz);
const char *dshot_txctl_set_value(dshot_txctl_t *t, uint16_t value, bool tlm);
const char *dshot_txctl_start_cmd(dshot_txctl_t *t, uint16_t cmd, uint16_t repeat);
void dshot_txctl_set_tlm(dshot_txctl_t *t, bool tlm);
const char *dshot_txctl_set_bidir(dshot_txctl_t *t, bool bidir);
uint32_t dshot_txctl_max_rate(const dshot_txctl_t *t);
void dshot_txctl_set_enabled(dshot_txctl_t *t, bool enabled);
void dshot_txctl_set_timeout(dshot_txctl_t *t, uint32_t timeout_ms);
void dshot_txctl_stop(dshot_txctl_t *t);
/* Zaman aşımı failsafe'i; çıkışı değiştirdiyse true */
bool dshot_txctl_tick(dshot_txctl_t *t, uint32_t now_ms);
/* Port kapandı (DTR düştü); çıkışı değiştirdiyse true */
bool dshot_txctl_dtr_drop(dshot_txctl_t *t);
bool dshot_txctl_cmd_active(const dshot_txctl_t *t);
/* Host'tan motor komutu geldi: bu komutu taşıyan ilk frame'de tetik pini palslanır */
void dshot_txctl_mark_trigger(dshot_txctl_t *t);
/* Frame kesmesinden, next_frame true döndükten sonra: bu frame tetiklenmeli mi (bir kez true) */
bool dshot_txctl_take_trigger(dshot_txctl_t *t);
/* Gönderilecek sıradaki frame; çıkış kapalıysa false */
bool dshot_txctl_next_frame(dshot_txctl_t *t, uint16_t *frame);

#endif
