/* tools/gen_vectors.py tarafından üretildi — elle düzenleme. */
#ifndef TEST_VECTORS_H
#define TEST_VECTORS_H

#include <stdint.h>

typedef struct {
    uint16_t value;
    uint8_t telemetry;
    uint8_t inverted;
    uint16_t frame;
} dshot_vector_t;

static const dshot_vector_t DSHOT_VECTORS[] = {
    {1046u, 0u, 0u, 0x82C6u},
    {0u, 0u, 0u, 0x0000u},
    {2047u, 1u, 0u, 0xFFFFu},
    {48u, 0u, 0u, 0x0606u},
    {1u, 1u, 0u, 0x0033u},
    {21u, 1u, 0u, 0x02B9u},
    {1000u, 1u, 0u, 0x7D1Bu},
    {1046u, 0u, 1u, 0x82C9u},
    {0u, 0u, 1u, 0x000Fu},
    {2047u, 1u, 1u, 0xFFF0u},
    {48u, 0u, 1u, 0x0609u},
};
#define DSHOT_VECTOR_COUNT (sizeof(DSHOT_VECTORS) / sizeof(DSHOT_VECTORS[0]))

#endif
