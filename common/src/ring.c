#include "ring.h"

void ring_init(ring_t *r, uint8_t *mem, uint16_t size)
{
    r->buf = mem;
    r->mask = (uint16_t)(size - 1u);
    r->head = 0;
    r->tail = 0;
}

uint16_t ring_used(const ring_t *r)
{
    return (uint16_t)(r->head - r->tail);
}

uint16_t ring_free(const ring_t *r)
{
    return (uint16_t)(r->mask + 1u - ring_used(r));
}

uint16_t ring_write(ring_t *r, const uint8_t *data, uint16_t len)
{
    uint16_t n = ring_free(r);
    if (len < n) {
        n = len;
    }
    uint16_t h = r->head;
    for (uint16_t i = 0; i < n; i++) {
        r->buf[(uint16_t)(h + i) & r->mask] = data[i];
    }
    __sync_synchronize();
    r->head = (uint16_t)(h + n);
    return n;
}

uint16_t ring_read(ring_t *r, uint8_t *out, uint16_t max)
{
    uint16_t n = ring_used(r);
    if (max < n) {
        n = max;
    }
    uint16_t t = r->tail;
    for (uint16_t i = 0; i < n; i++) {
        out[i] = r->buf[(uint16_t)(t + i) & r->mask];
    }
    __sync_synchronize();
    r->tail = (uint16_t)(t + n);
    return n;
}
