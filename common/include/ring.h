#ifndef RING_H
#define RING_H

#include <stdint.h>

/* Tek üretici / tek tüketici bayt halkası. Boyut 2'nin kuvveti ve <= 32768 olmalı. */
typedef struct {
    uint8_t *buf;
    uint16_t mask;
    volatile uint16_t head; /* üretici yazar */
    volatile uint16_t tail; /* tüketici yazar */
} ring_t;

void ring_init(ring_t *r, uint8_t *mem, uint16_t size);
uint16_t ring_used(const ring_t *r);
uint16_t ring_free(const ring_t *r);
/* Sığdığı kadarını yazar, yazılan bayt sayısını döner. */
uint16_t ring_write(ring_t *r, const uint8_t *data, uint16_t len);
uint16_t ring_read(ring_t *r, uint8_t *out, uint16_t max);

#endif
