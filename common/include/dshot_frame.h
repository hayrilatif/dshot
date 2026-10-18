#ifndef DSHOT_FRAME_H
#define DSHOT_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#define DSHOT_VALUE_MAX    2047u
#define DSHOT_CMD_MAX      47u
#define DSHOT_THROTTLE_MIN 48u

typedef enum {
    DSHOT_SPEED_UNKNOWN = 0,
    DSHOT_150 = 150,
    DSHOT_300 = 300,
    DSHOT_600 = 600,
    DSHOT_1200 = 1200,
} dshot_speed_t;

typedef struct {
    uint16_t value; /* 0..2047 */
    bool telemetry;
    bool crc_ok;
} dshot_decoded_t;

/* v12 = (değer << 1) | telemetri. inverted: bidirectional DShot CRC'si */
uint8_t dshot_crc(uint16_t v12, bool inverted);
uint16_t dshot_encode(uint16_t value, bool telemetry, bool inverted);
dshot_decoded_t dshot_decode(uint16_t frame, bool inverted);

bool dshot_speed_valid(uint32_t speed);
/* Nominal bit süresi (ns), geçersiz hızda 0 */
uint32_t dshot_bit_ns(dshot_speed_t speed);

#endif
