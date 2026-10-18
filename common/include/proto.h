#ifndef PROTO_H
#define PROTO_H

#include <stdbool.h>
#include <stdint.h>

#define PROTO_LINE_MAX 64u

typedef enum {
    PROTO_EMPTY = 0,
    PROTO_STATUS,
    PROTO_PING,
    PROTO_TX_SPEED,
    PROTO_TX_RATE,
    PROTO_TX_THR,
    PROTO_TX_RAW,
    PROTO_TX_TLM,
    PROTO_TX_CMD,
    PROTO_TX_STOP,
    PROTO_TX_ENABLE,
    PROTO_TX_TIMEOUT,
    PROTO_RX_STREAM,
    PROTO_RX_RAW,
    PROTO_RX_POL,
    PROTO_RX_RESET,
} proto_cmd_t;

enum { PROTO_POL_AUTO = 0, PROTO_POL_NORMAL = 1, PROTO_POL_INVERTED = 2 };

typedef struct {
    proto_cmd_t cmd;
    uint32_t args[2];
    uint8_t nargs;
    const char *error; /* NULL, "unknown", "args" veya "range" */
} proto_msg_t;

/* Satırı yerinde böler/büyük harfe çevirir. */
proto_msg_t proto_parse(char *line);
bool proto_is_tx(proto_cmd_t c);
bool proto_is_rx(proto_cmd_t c);

typedef struct {
    char buf[PROTO_LINE_MAX];
    uint8_t len;
    bool overflow;
} proto_line_t;

typedef enum { PROTO_LINE_NONE, PROTO_LINE_READY, PROTO_LINE_OVERFLOW } proto_line_result_t;

void proto_line_init(proto_line_t *l);
/* READY'de *line bir sonraki feed çağrısına kadar geçerlidir. */
proto_line_result_t proto_line_feed(proto_line_t *l, char c, char **line);

#endif
