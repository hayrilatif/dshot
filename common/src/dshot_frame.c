#include "dshot_frame.h"

uint8_t dshot_crc(uint16_t v12, bool inverted)
{
    uint8_t c = (uint8_t)((v12 ^ (v12 >> 4) ^ (v12 >> 8)) & 0x0Fu);
    return inverted ? (uint8_t)(~c & 0x0Fu) : c;
}

uint16_t dshot_encode(uint16_t value, bool telemetry, bool inverted)
{
    uint16_t v12 = (uint16_t)(((value & DSHOT_VALUE_MAX) << 1) | (telemetry ? 1u : 0u));
    return (uint16_t)((v12 << 4) | dshot_crc(v12, inverted));
}

dshot_decoded_t dshot_decode(uint16_t frame, bool inverted)
{
    uint16_t v12 = (uint16_t)(frame >> 4);
    dshot_decoded_t d;
    d.value = (uint16_t)(v12 >> 1);
    d.telemetry = (v12 & 1u) != 0u;
    d.crc_ok = (frame & 0x0Fu) == dshot_crc(v12, inverted);
    return d;
}

bool dshot_speed_valid(uint32_t speed)
{
    return speed == 150u || speed == 300u || speed == 600u || speed == 1200u;
}

uint32_t dshot_bit_ns(dshot_speed_t speed)
{
    if (!dshot_speed_valid((uint32_t)speed)) {
        return 0;
    }
    return (1000000u + (uint32_t)speed / 2u) / (uint32_t)speed;
}
