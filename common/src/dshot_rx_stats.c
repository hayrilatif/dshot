#include <stdio.h>
#include <string.h>
#include "dshot_rx_stats.h"

void dshot_stats_reset(dshot_rx_stats_t *s)
{
    memset(s, 0, sizeof *s);
    s->vmin = 0xFFFFu;
}

void dshot_stats_add(dshot_rx_stats_t *s, const dshot_burst_result_t *r, bool inverted)
{
    if (!r->is_frame) {
        s->other++;
        return;
    }
    if (!r->frame.crc_ok) {
        s->crc_err++;
        return;
    }
    uint16_t v = r->frame.value;
    s->n++;
    s->last_val = v;
    if (v < s->vmin) {
        s->vmin = v;
    }
    if (v > s->vmax) {
        s->vmax = v;
    }
    s->tlm_mask |= r->frame.telemetry ? 2u : 1u;
    s->inverted = inverted;
    s->speed = r->speed;
    s->bit_ns = r->bit_ns;
    if (r->t0h_ns) {
        s->t0h_ns = r->t0h_ns;
    }
    if (r->t1h_ns) {
        s->t1h_ns = r->t1h_ns;
    }
}

size_t dshot_stats_format(const dshot_rx_stats_t *s, uint32_t interval_ms, char *out, size_t cap)
{
    if (cap == 0u) {
        return 0;
    }
    char val[8] = "-", vmin[8] = "-", vmax[8] = "-", spd[8] = "?";
    const char *tlm = "-";
    if (s->n) {
        snprintf(val, sizeof val, "%u", (unsigned)s->last_val);
        snprintf(vmin, sizeof vmin, "%u", (unsigned)s->vmin);
        snprintf(vmax, sizeof vmax, "%u", (unsigned)s->vmax);
        tlm = s->tlm_mask == 3u ? "mix" : (s->tlm_mask == 2u ? "1" : "0");
        if (s->speed != DSHOT_SPEED_UNKNOWN) {
            snprintf(spd, sizeof spd, "%u", (unsigned)s->speed);
        }
    }
    unsigned long rate = interval_ms ? (unsigned long)(((uint64_t)s->n * 1000u) / interval_ms) : 0ul;
    int n = snprintf(out, cap,
                     "#RX n=%lu rate=%lu val=%s vmin=%s vmax=%s tlm=%s crc_err=%lu other=%lu inv=%u speed=%s "
                     "bit_ns=%lu t0h_ns=%lu t1h_ns=%lu\n",
                     (unsigned long)s->n, rate, val, vmin, vmax, tlm, (unsigned long)s->crc_err,
                     (unsigned long)s->other, (unsigned)s->inverted, spd, (unsigned long)s->bit_ns,
                     (unsigned long)s->t0h_ns, (unsigned long)s->t1h_ns);
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n < cap ? (size_t)n : cap - 1u;
}
