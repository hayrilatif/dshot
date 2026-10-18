#include <stddef.h>
#include "dshot_tx_buf.h"
#include "dshot_txctl.h"

static bool zero_output(dshot_txctl_t *t)
{
    bool changed = t->value != 0u || t->cmd_remaining != 0u;
    if (t->cmd_remaining != 0u) {
        t->cmd_aborted = true;
    }
    t->cmd_remaining = 0;
    t->value = 0;
    return changed;
}

void dshot_txctl_init(dshot_txctl_t *t, uint32_t now_ms)
{
    t->speed = DSHOT_TX_DEFAULT_SPEED;
    t->rate_hz = DSHOT_TX_DEFAULT_RATE_HZ;
    t->value = 0;
    t->tlm = false;
    t->enabled = true;
    t->timeout_ms = DSHOT_TX_DEFAULT_TIMEOUT_MS;
    t->last_contact_ms = now_ms;
    t->failsafe_active = false;
    t->cmd_value = 0;
    t->cmd_remaining = 0;
    t->cmd_aborted = false;
    t->bidir = false;
}

void dshot_txctl_touch(dshot_txctl_t *t, uint32_t now_ms)
{
    t->last_contact_ms = now_ms;
    t->failsafe_active = false;
}

const char *dshot_txctl_set_speed(dshot_txctl_t *t, uint32_t speed)
{
    if (!dshot_speed_valid(speed)) {
        return "range";
    }
    /* Motor dönerken hız değişimi birkaç bozuk frame üretir */
    if (t->value != 0u || t->cmd_remaining != 0u) {
        return "busy";
    }
    uint32_t limit = t->bidir ? dshot_max_rate_bidir_hz((dshot_speed_t)speed) : dshot_max_rate_hz((dshot_speed_t)speed);
    if (t->rate_hz > limit) {
        return "rate";
    }
    t->speed = (dshot_speed_t)speed;
    return NULL;
}

const char *dshot_txctl_set_rate(dshot_txctl_t *t, uint32_t rate_hz)
{
    if (rate_hz == 0u || rate_hz > dshot_txctl_max_rate(t)) {
        return "rate";
    }
    t->rate_hz = rate_hz;
    return NULL;
}

const char *dshot_txctl_set_value(dshot_txctl_t *t, uint16_t value, bool tlm)
{
    if (value > DSHOT_VALUE_MAX) {
        return "range";
    }
    if (t->cmd_remaining != 0u) {
        return "busy";
    }
    /* Dönen motordan doğrudan özel komuta geçilmez */
    if (value >= 1u && value <= DSHOT_CMD_MAX && t->value >= DSHOT_THROTTLE_MIN) {
        return "busy";
    }
    t->tlm = tlm;
    t->value = value;
    return NULL;
}

const char *dshot_txctl_start_cmd(dshot_txctl_t *t, uint16_t cmd, uint16_t repeat)
{
    if (cmd < 1u || cmd > DSHOT_CMD_MAX || repeat == 0u) {
        return "range";
    }
    if (!t->enabled) {
        return "disabled";
    }
    if (t->value != 0u || t->cmd_remaining != 0u) {
        return "busy";
    }
    t->cmd_aborted = false;
    t->cmd_value = cmd;
    t->cmd_remaining = repeat;
    return NULL;
}

void dshot_txctl_set_tlm(dshot_txctl_t *t, bool tlm)
{
    t->tlm = tlm;
}

uint32_t dshot_txctl_max_rate(const dshot_txctl_t *t)
{
    return t->bidir ? dshot_max_rate_bidir_hz(t->speed) : dshot_max_rate_hz(t->speed);
}

const char *dshot_txctl_set_bidir(dshot_txctl_t *t, bool bidir)
{
    if (t->value != 0u || t->cmd_remaining != 0u) {
        return "busy";
    }
    if (bidir && t->rate_hz > dshot_max_rate_bidir_hz(t->speed)) {
        return "rate";
    }
    t->bidir = bidir;
    return NULL;
}

void dshot_txctl_set_enabled(dshot_txctl_t *t, bool enabled)
{
    if (!enabled && t->cmd_remaining != 0u) {
        t->cmd_aborted = true;
        t->cmd_remaining = 0;
    }
    t->enabled = enabled;
}

void dshot_txctl_set_timeout(dshot_txctl_t *t, uint32_t timeout_ms)
{
    t->timeout_ms = timeout_ms;
}

void dshot_txctl_stop(dshot_txctl_t *t)
{
    zero_output(t);
}

bool dshot_txctl_tick(dshot_txctl_t *t, uint32_t now_ms)
{
    if (t->timeout_ms == 0u || t->failsafe_active) {
        return false;
    }
    if ((uint32_t)(now_ms - t->last_contact_ms) < t->timeout_ms) {
        return false;
    }
    t->failsafe_active = true;
    return zero_output(t);
}

bool dshot_txctl_dtr_drop(dshot_txctl_t *t)
{
    if (t->timeout_ms == 0u) {
        return false;
    }
    t->failsafe_active = true;
    return zero_output(t);
}

bool dshot_txctl_cmd_active(const dshot_txctl_t *t)
{
    return t->cmd_remaining != 0u;
}

bool dshot_txctl_next_frame(dshot_txctl_t *t, uint16_t *frame)
{
    if (!t->enabled) {
        return false;
    }
    uint16_t rem = t->cmd_remaining;
    if (rem != 0u) {
        *frame = dshot_encode(t->cmd_value, true, t->bidir);
        t->cmd_remaining = (uint16_t)(rem - 1u);
    } else {
        *frame = dshot_encode(t->value, t->tlm, t->bidir);
    }
    return true;
}
