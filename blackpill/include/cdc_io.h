#ifndef CDC_IO_H
#define CDC_IO_H

#include <stdbool.h>
#include <stdint.h>

void cdc_init(void);
uint16_t cdc_read(uint8_t *buf, uint16_t max);
/* Kuyruğa ekler, sığmayanı atar (asenkron satırlar için). */
uint16_t cdc_write(const char *data, uint16_t len);
/* Yer açılana kadar en fazla timeout_ms bekler (cevaplar için). */
uint16_t cdc_write_wait(const char *data, uint16_t len, uint32_t timeout_ms);
/* Host portu açık tutuyor mu (DTR) */
bool cdc_dtr(void);
/* Ana döngüden çağrılır: kuyruktaki veriyi USB'ye gönderir. */
void cdc_flush(void);

#endif
