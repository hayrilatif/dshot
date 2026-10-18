#include "dshot_rpmctl.h"

void dshot_rpmctl_init(dshot_rpmctl_t *c)
{
    c->active = false;
    c->reverse = false;
    c->mode3d = false;
    c->target_rpm = 0;
    c->poles = DSHOT_RPM_DEFAULT_POLES;
    c->max_mag = DSHOT_RPM_DEFAULT_MAX_MAG;
    c->kp_milli = DSHOT_RPM_DEFAULT_KP;
    c->ki_milli = DSHOT_RPM_DEFAULT_KI;
    c->integ = 0.0f;
    c->out = 0.0f;
    c->since_ok_s = 0.0f;
    c->last_rpm = 0;
}

static void reset_output(dshot_rpmctl_t *c)
{
    c->integ = 0.0f;
    c->out = 0.0f;
    c->since_ok_s = 0.0f;
}

void dshot_rpmctl_set_target(dshot_rpmctl_t *c, uint32_t rpm, bool reverse)
{
    if (rpm == 0u) {
        dshot_rpmctl_stop(c);
        return;
    }
    if (!c->active || reverse != c->reverse) {
        reset_output(c);
    }
    c->reverse = reverse;
    c->target_rpm = rpm;
    c->active = true;
}

void dshot_rpmctl_stop(dshot_rpmctl_t *c)
{
    c->active = false;
    c->target_rpm = 0;
    reset_output(c);
}

uint16_t dshot_rpmctl_mag_limit(const dshot_rpmctl_t *c)
{
    uint16_t mode_max = c->mode3d ? (c->reverse ? 1000u : 999u) : 2000u;
    return c->max_mag < mode_max ? c->max_mag : mode_max;
}

uint16_t dshot_rpmctl_value(const dshot_rpmctl_t *c, uint16_t mag)
{
    if (mag == 0u) {
        return 0;
    }
    uint16_t lim = dshot_rpmctl_mag_limit(c);
    if (mag > lim) {
        mag = lim;
    }
    if (c->mode3d && !c->reverse) {
        return (uint16_t)(1048u + mag); /* 1049..2047 */
    }
    return (uint16_t)(47u + mag); /* normal 48..2047, 3D geri 48..1047 */
}

uint16_t dshot_rpmctl_step(dshot_rpmctl_t *c, int32_t measured_erpm, float dt_s, bool *lost)
{
    *lost = false;
    if (!c->active) {
        return 0;
    }
    if (measured_erpm < 0) {
        c->since_ok_s += dt_s;
        if (c->since_ok_s >= DSHOT_RPM_LOSS_S) {
            dshot_rpmctl_stop(c);
            *lost = true;
            return 0;
        }
        return dshot_rpmctl_value(c, (uint16_t)(c->out + 0.5f));
    }
    dt_s += c->since_ok_s; /* kaçan ölçümler dahil gerçek aralık */
    c->since_ok_s = 0.0f;
    uint32_t pole_pairs = c->poles / 2u ? c->poles / 2u : 1u;
    c->last_rpm = (uint32_t)measured_erpm / pole_pairs;

    float err = (float)c->target_rpm - (float)c->last_rpm;
    float lim = (float)dshot_rpmctl_mag_limit(c);
    float p = err * (float)c->kp_milli / 1000.0f;
    float new_integ = c->integ + err * (float)c->ki_milli / 1000.0f * dt_s;
    /* anti-windup: integral yalnız çıkışı doyuma itmiyorsa birikir */
    float candidate = p + new_integ;
    if ((candidate < lim || err < 0.0f) && (candidate > 0.0f || err > 0.0f)) {
        c->integ = new_integ;
    }
    if (c->integ < 0.0f) {
        c->integ = 0.0f;
    }
    if (c->integ > lim) {
        c->integ = lim;
    }
    float want = p + c->integ;
    if (want < 0.0f) {
        want = 0.0f;
    }
    if (want > lim) {
        want = lim;
    }
    float max_step = DSHOT_RPM_SLEW_PER_S * dt_s;
    if (want > c->out + max_step) {
        want = c->out + max_step;
    } else if (want < c->out - max_step) {
        want = c->out - max_step;
    }
    c->out = want;
    return dshot_rpmctl_value(c, (uint16_t)(c->out + 0.5f));
}
