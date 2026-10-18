#ifndef DSHOT_RPMCTL_H
#define DSHOT_RPMCTL_H

#include <stdbool.h>
#include <stdint.h>

/* Bidirectional eRPM geri beslemesiyle mekanik RPM hedefi tutan PI kontrolcü.
   Çıkış "büyüklük": normal modda 1..2000, 3D modda 1..999 (ileri) / 1..1000 (geri). */
#define DSHOT_RPM_DEFAULT_POLES   14u
#define DSHOT_RPM_DEFAULT_MAX_MAG 300u   /* güvenli tavan */
#define DSHOT_RPM_DEFAULT_KP      50u    /* milli: 0.05 adım / RPM hata */
#define DSHOT_RPM_DEFAULT_KI      1000u  /* milli: 1.0 adım / (RPM·s) */
#define DSHOT_RPM_SLEW_PER_S      2000.0f
#define DSHOT_RPM_LOSS_S          0.1f   /* bu kadar geçerli ölçüm yoksa dur */

typedef struct {
    bool active;
    bool reverse;     /* yalnız 3D modda */
    bool mode3d;
    uint32_t target_rpm;
    uint16_t poles;
    uint16_t max_mag;
    uint32_t kp_milli;
    uint32_t ki_milli;
    float integ;
    float out;        /* son büyüklük */
    float since_ok_s; /* son geçerli ölçümden beri geçen süre */
    uint32_t last_rpm;
} dshot_rpmctl_t;

void dshot_rpmctl_init(dshot_rpmctl_t *c);
/* Hedefi başlatır/değiştirir. Yön değişirse çıkış sıfırdan başlar. */
void dshot_rpmctl_set_target(dshot_rpmctl_t *c, uint32_t rpm, bool reverse);
void dshot_rpmctl_stop(dshot_rpmctl_t *c);
/* Bir frame süresi (dt_s) geçti; measured_erpm geçerli ölçüm yoksa -1. Gönderilecek DShot değerini döner.
   Telemetri kaybında kendini kapatır ve 0 döner (*lost = true). */
uint16_t dshot_rpmctl_step(dshot_rpmctl_t *c, int32_t measured_erpm, float dt_s, bool *lost);
/* Büyüklük → DShot değeri (mod ve yöne göre) */
uint16_t dshot_rpmctl_value(const dshot_rpmctl_t *c, uint16_t mag);
/* Modun izin verdiği en büyük büyüklük */
uint16_t dshot_rpmctl_mag_limit(const dshot_rpmctl_t *c);

#endif
