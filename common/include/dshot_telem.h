#ifndef DSHOT_TELEM_H
#define DSHOT_TELEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dshot_frame.h"

/* Bidirectional DShot: ESC'nin aynı kablodan verdiği cevap.
   21 bit, DShot bit hızının 5/4'ü; her geçiş bir '1' (GCR), 20 bit GCR → 16 bit → 12 bit değer + 4 bit CRC. */
#define DSHOT_REPLY_MAX_EDGES 32u

typedef struct {
    uint16_t edges[DSHOT_REPLY_MAX_EDGES]; /* kenar zaman damgaları (timer tick) */
    uint8_t count;                         /* 0: cevap yok */
} dshot_reply_t;

typedef enum {
    DSHOT_REPLY_OK = 0,
    DSHOT_REPLY_NONE,     /* hiç kenar yok */
    DSHOT_REPLY_BAD_BITS, /* 21 bit tutmadı */
    DSHOT_REPLY_BAD_GCR,
    DSHOT_REPLY_BAD_CRC,
} dshot_reply_status_t;

/* İki ayrı kenar dizisini (ör. düşen / yükselen, her biri zamanla artan) tek sıralı diziye birleştirir;
   DSHOT_REPLY_MAX_EDGES'i aşan kısım atılır. Her iki kenarı birden yakalayamayan timer'lar için (F103). */
void dshot_reply_merge(dshot_reply_t *r, const uint16_t *a, unsigned na, const uint16_t *b, unsigned nb);

dshot_reply_status_t dshot_reply_decode(const dshot_reply_t *r, uint32_t tick_hz, dshot_speed_t speed, uint16_t *v12);

/* EDT (extended telemetry) çerçevesi mi: mantis MSB 0 ve üs != 0 */
bool dshot_v12_is_edt(uint16_t v12);
/* Elektriksel devir/dk; motor duruyorsa 0 */
uint32_t dshot_v12_erpm(uint16_t v12);

enum {
    DSHOT_EDT_TEMP = 0x2,    /* °C */
    DSHOT_EDT_VOLTAGE = 0x4, /* 0.25 V adım */
    DSHOT_EDT_CURRENT = 0x6, /* A */
    DSHOT_EDT_DEBUG1 = 0x8,
    DSHOT_EDT_DEBUG2 = 0xA,
    DSHOT_EDT_STRESS = 0xC,
    DSHOT_EDT_STATUS = 0xE,
};

typedef struct {
    uint32_t ok, bad, miss; /* pencere sayaçları */
    bool have_erpm;
    uint32_t erpm;
    /* EDT değerleri seyrek gelir: pencereler arası korunur */
    bool have_temp, have_volt, have_curr, have_stress, have_status;
    uint8_t temp_c;
    uint16_t volt_mv;
    uint8_t curr_a;
    uint8_t stress;
    uint8_t status;
} dshot_telem_t;

void dshot_telem_init(dshot_telem_t *t);
/* Pencere sayaçlarını ve eRPM'i sıfırlar, EDT değerlerini korur */
void dshot_telem_reset_window(dshot_telem_t *t);
void dshot_telem_add(dshot_telem_t *t, dshot_reply_status_t st, uint16_t v12);
/* "#T ..." satırı */
size_t dshot_telem_format(const dshot_telem_t *t, char *out, size_t cap);

#endif
