#include <stdio.h>
#include <string.h>
#include "dshot_telem.h"

/* 5 bit GCR → 4 bit; 0xFF geçersiz */
static const uint8_t GCR_DECODE[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x9, 0xA, 0xB, 0xFF, 0xD, 0xE, 0xF,
    0xFF, 0xFF, 0x2,  0x3,  0xFF, 0x5,  0x6,  0x7,  0xFF, 0x0, 0x8, 0x1, 0xFF, 0x4, 0xC, 0xFF,
};

dshot_reply_status_t dshot_reply_decode(const dshot_reply_t *r, uint32_t tick_hz, dshot_speed_t speed, uint16_t *v12)
{
    if (r->count == 0u) {
        return DSHOT_REPLY_NONE;
    }
    if (r->count > DSHOT_REPLY_MAX_EDGES || !dshot_speed_valid((uint32_t)speed) || tick_hz == 0u) {
        return DSHOT_REPLY_BAD_BITS;
    }
    /* cevap bit süresi = tick_hz / (speed*1000*5/4) */
    const uint64_t num = (uint64_t)5u * (uint32_t)speed * 1000u;
    const uint64_t den = (uint64_t)4u * tick_hz;
    uint32_t value = 0;
    unsigned bits = 0;
    for (unsigned i = 1; i <= r->count; i++) {
        unsigned len;
        if (i < r->count) {
            uint16_t diff = (uint16_t)(r->edges[i] - r->edges[i - 1u]);
            len = (unsigned)(((uint64_t)diff * num + den / 2u) / den);
            if (len == 0u) {
                return DSHOT_REPLY_BAD_BITS;
            }
        } else {
            if (bits >= 21u) {
                return DSHOT_REPLY_BAD_BITS;
            }
            len = 21u - bits;
        }
        if (bits + len > 21u) {
            return DSHOT_REPLY_BAD_BITS;
        }
        value = (value << len) | (1u << (len - 1u));
        bits += len;
    }
    uint16_t d = 0;
    for (unsigned n = 0; n < 4u; n++) {
        uint8_t nib = GCR_DECODE[(value >> (5u * n)) & 0x1Fu];
        if (nib == 0xFFu) {
            return DSHOT_REPLY_BAD_GCR;
        }
        d |= (uint16_t)(nib << (4u * n));
    }
    uint16_t c = (uint16_t)(d ^ (d >> 8));
    c ^= (uint16_t)(c >> 4);
    if ((c & 0xFu) != 0xFu) {
        return DSHOT_REPLY_BAD_CRC;
    }
    *v12 = (uint16_t)(d >> 4);
    return DSHOT_REPLY_OK;
}

bool dshot_v12_is_edt(uint16_t v12)
{
    return (v12 & 0x100u) == 0u && (v12 >> 9) != 0u;
}

uint32_t dshot_v12_erpm(uint16_t v12)
{
    if (v12 == 0xFFFu) {
        return 0;
    }
    uint32_t period_us = (uint32_t)(v12 & 0x1FFu) << (v12 >> 9);
    if (period_us == 0u) {
        return 0;
    }
    return (60000000u + period_us / 2u) / period_us;
}

void dshot_telem_init(dshot_telem_t *t)
{
    memset(t, 0, sizeof *t);
}

void dshot_telem_reset_window(dshot_telem_t *t)
{
    t->ok = 0;
    t->bad = 0;
    t->miss = 0;
    t->have_erpm = false;
    t->erpm = 0;
}

void dshot_telem_add(dshot_telem_t *t, dshot_reply_status_t st, uint16_t v12)
{
    if (st == DSHOT_REPLY_NONE) {
        t->miss++;
        return;
    }
    if (st != DSHOT_REPLY_OK) {
        t->bad++;
        return;
    }
    t->ok++;
    if (!dshot_v12_is_edt(v12)) {
        t->have_erpm = true;
        t->erpm = dshot_v12_erpm(v12);
        return;
    }
    uint8_t data = (uint8_t)(v12 & 0xFFu);
    switch (v12 >> 8) {
    case DSHOT_EDT_TEMP:
        t->have_temp = true;
        t->temp_c = data;
        break;
    case DSHOT_EDT_VOLTAGE:
        t->have_volt = true;
        t->volt_mv = (uint16_t)(data * 250u);
        break;
    case DSHOT_EDT_CURRENT:
        t->have_curr = true;
        t->curr_a = data;
        break;
    case DSHOT_EDT_STRESS:
        t->have_stress = true;
        t->stress = data;
        break;
    case DSHOT_EDT_STATUS:
        t->have_status = true;
        t->status = data;
        break;
    default:
        break;
    }
}

static const char *num_or_dash(bool have, unsigned long v, char *buf, size_t len)
{
    if (!have) {
        return "-";
    }
    snprintf(buf, len, "%lu", v);
    return buf;
}

size_t dshot_telem_format(const dshot_telem_t *t, char *out, size_t cap)
{
    if (cap == 0u) {
        return 0;
    }
    char e[12], tc[6], v[8], c[6], s[6], st[6];
    int n = snprintf(out, cap, "#T ok=%lu bad=%lu miss=%lu erpm=%s temp_c=%s volt_mv=%s curr_a=%s stress=%s status=%s\n",
                     (unsigned long)t->ok, (unsigned long)t->bad, (unsigned long)t->miss,
                     num_or_dash(t->have_erpm, t->erpm, e, sizeof e), num_or_dash(t->have_temp, t->temp_c, tc, sizeof tc),
                     num_or_dash(t->have_volt, t->volt_mv, v, sizeof v), num_or_dash(t->have_curr, t->curr_a, c, sizeof c),
                     num_or_dash(t->have_stress, t->stress, s, sizeof s),
                     num_or_dash(t->have_status, t->status, st, sizeof st));
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n < cap ? (size_t)n : cap - 1u;
}
