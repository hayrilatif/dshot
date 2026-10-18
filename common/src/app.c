#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "proto.h"

__attribute__((format(printf, 3, 4))) static size_t put(char *out, size_t cap, const char *fmt, ...)
{
    if (cap == 0u) {
        return 0;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n < cap ? (size_t)n : cap - 1u;
}

void app_init(app_t *a, const app_hw_t *hw, uint32_t dev_id, uint32_t now_ms)
{
    memset(a, 0, sizeof *a);
    a->hw = hw;
    a->dev_id = dev_id;
    dshot_txctl_init(&a->tx, now_ms);
    a->rx_pol_mode = PROTO_POL_AUTO;
    dshot_pol_init(&a->pol, false);
    dshot_stats_reset(&a->stats);
    a->last_stream_ms = now_ms;
}

bool app_busy(const app_t *a)
{
    return a->cmd_reply_pending;
}

bool app_rx_inverted(const app_t *a)
{
    return a->pol.inverted;
}

static const char *pol_name(uint8_t mode)
{
    switch (mode) {
    case PROTO_POL_NORMAL:
        return "normal";
    case PROTO_POL_INVERTED:
        return "inverted";
    default:
        return "auto";
    }
}

static size_t format_status(const app_t *a, char *out, size_t cap)
{
    const char *caps = a->hw->has_tx ? (a->hw->has_rx ? "tx,rx" : "tx") : "rx";
    size_t n = put(out, cap, "OK board=%s caps=%s dev=0x%03lx fw=%s", a->hw->board, caps, (unsigned long)a->dev_id,
                   APP_FW_VERSION);
    if (a->hw->has_tx) {
        n += put(out + n, cap - n, " tx_speed=%u tx_rate=%lu tx_val=%u tx_tlm=%u tx_en=%u tx_timeout=%lu",
                 (unsigned)a->tx.speed, (unsigned long)a->tx.rate_hz, (unsigned)a->tx.value, (unsigned)a->tx.tlm,
                 (unsigned)a->tx.enabled, (unsigned long)a->tx.timeout_ms);
    }
    if (a->hw->has_rx) {
        n += put(out + n, cap - n, " rx_pol=%s rx_inv=%u rx_stream=%lu", pol_name(a->rx_pol_mode),
                 (unsigned)a->pol.inverted, (unsigned long)a->stream_ms);
    }
    n += put(out + n, cap - n, "\n");
    return n;
}

static void tx_apply(app_t *a)
{
    if (a->hw->tx_apply) {
        a->hw->tx_apply(&a->tx);
    }
}

static void set_inverted(app_t *a, bool inverted)
{
    dshot_pol_init(&a->pol, inverted);
    if (a->hw->rx_set_inverted) {
        a->hw->rx_set_inverted(inverted);
    }
}

size_t app_handle_line(app_t *a, char *line, uint32_t now_ms, char *out, size_t cap)
{
    proto_msg_t m = proto_parse(line);
    if (m.error) {
        return put(out, cap, "ERR %s\n", m.error);
    }
    if (m.cmd == PROTO_EMPTY) {
        return 0;
    }
    if ((proto_is_tx(m.cmd) && !a->hw->has_tx) || (proto_is_rx(m.cmd) && !a->hw->has_rx)) {
        return put(out, cap, "ERR unsupported\n");
    }
    if (a->hw->has_tx && (proto_is_tx(m.cmd) || m.cmd == PROTO_PING)) {
        dshot_txctl_touch(&a->tx, now_ms);
    }

    const char *err = NULL;
    switch (m.cmd) {
    case PROTO_STATUS:
        return format_status(a, out, cap);
    case PROTO_PING:
        break;
    case PROTO_TX_SPEED:
        err = dshot_txctl_set_speed(&a->tx, m.args[0]);
        if (!err) {
            tx_apply(a);
        }
        break;
    case PROTO_TX_RATE:
        err = dshot_txctl_set_rate(&a->tx, m.args[0]);
        if (!err) {
            tx_apply(a);
        }
        break;
    case PROTO_TX_THR:
        err = dshot_txctl_set_value(&a->tx, (uint16_t)(m.args[0] ? m.args[0] + 47u : 0u), a->tx.tlm);
        break;
    case PROTO_TX_RAW:
        err = dshot_txctl_set_value(&a->tx, (uint16_t)m.args[0], m.nargs > 1u && m.args[1] != 0u);
        break;
    case PROTO_TX_TLM:
        dshot_txctl_set_tlm(&a->tx, m.args[0] != 0u);
        break;
    case PROTO_TX_CMD:
        err = dshot_txctl_start_cmd(&a->tx, (uint16_t)m.args[0],
                                    (uint16_t)(m.nargs > 1u ? m.args[1] : APP_CMD_DEFAULT_REPEAT));
        if (!err) {
            a->cmd_reply_pending = true;
            return 0;
        }
        break;
    case PROTO_TX_STOP:
        dshot_txctl_stop(&a->tx);
        break;
    case PROTO_TX_ENABLE:
        dshot_txctl_set_enabled(&a->tx, m.args[0] != 0u);
        break;
    case PROTO_TX_TIMEOUT:
        dshot_txctl_set_timeout(&a->tx, m.args[0]);
        break;
    case PROTO_RX_STREAM:
        a->stream_ms = m.args[0];
        a->last_stream_ms = now_ms;
        dshot_stats_reset(&a->stats);
        break;
    case PROTO_RX_RAW:
        a->raw_every = m.args[0];
        a->raw_count = 0;
        break;
    case PROTO_RX_POL:
        a->rx_pol_mode = (uint8_t)m.args[0];
        if (m.args[0] != PROTO_POL_AUTO) {
            set_inverted(a, m.args[0] == PROTO_POL_INVERTED);
        }
        break;
    case PROTO_RX_RESET:
        dshot_stats_reset(&a->stats);
        a->last_stream_ms = now_ms;
        break;
    default:
        err = "unknown";
        break;
    }
    return err ? put(out, cap, "ERR %s\n", err) : put(out, cap, "OK\n");
}

size_t app_rx_burst(app_t *a, const dshot_rx_burst_t *b, uint32_t tick_hz, char *out, size_t cap)
{
    if (a->rx_pol_mode == PROTO_POL_AUTO && dshot_pol_update(&a->pol, b->idle_high)) {
        /* Bu burst yanlış polariteyle yakalandı, at */
        if (a->hw->rx_set_inverted) {
            a->hw->rx_set_inverted(a->pol.inverted);
        }
        return 0;
    }
    dshot_burst_result_t r = dshot_decode_burst(b, tick_hz, a->pol.inverted);
    dshot_stats_add(&a->stats, &r, a->pol.inverted);
    if (a->raw_every == 0u) {
        return 0;
    }
    if (!r.is_frame) {
        /* 16 palslık olmayan burst'ler her zaman basılır (gürültü / ESC cevabı teşhisi) */
        return put(out, cap, "#B n=%u idle=%u\n", (unsigned)b->n, (unsigned)b->idle_high);
    }
    if (++a->raw_count < a->raw_every) {
        return 0;
    }
    a->raw_count = 0;
    return put(out, cap, "#F val=%u tlm=%u crc=%s inv=%u raw=0x%04X bit_ns=%lu\n", (unsigned)r.frame.value,
               (unsigned)r.frame.telemetry, r.frame.crc_ok ? "ok" : "bad", (unsigned)a->pol.inverted,
               (unsigned)r.raw, (unsigned long)r.bit_ns);
}

size_t app_poll(app_t *a, uint32_t now_ms, bool dtr, char *out, size_t cap)
{
    size_t n = 0;
    if (a->hw->has_tx) {
        bool failsafe = false;
        if (a->last_dtr && !dtr && dshot_txctl_dtr_drop(&a->tx)) {
            failsafe = true;
        }
        a->last_dtr = dtr;
        if (dshot_txctl_tick(&a->tx, now_ms)) {
            failsafe = true;
        }
        if (failsafe) {
            n += put(out + n, cap - n, "#TX FAILSAFE\n");
        }
        if (a->cmd_reply_pending && !dshot_txctl_cmd_active(&a->tx)) {
            a->cmd_reply_pending = false;
            n += put(out + n, cap - n, "%s", a->tx.cmd_aborted ? "ERR aborted\n" : "OK\n");
        }
    }
    if (a->hw->has_rx && a->stream_ms != 0u && (uint32_t)(now_ms - a->last_stream_ms) >= a->stream_ms) {
        n += dshot_stats_format(&a->stats, now_ms - a->last_stream_ms, out + n, cap - n);
        dshot_stats_reset(&a->stats);
        a->last_stream_ms = now_ms;
    }
    return n;
}
