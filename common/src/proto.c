#include <string.h>
#include "dshot_frame.h"
#include "proto.h"

typedef enum { ARG_NUM, ARG_SPEED, ARG_POL } arg_kind_t;

typedef struct {
    const char *group; /* "TX", "RX" veya NULL */
    const char *name;
    proto_cmd_t cmd;
    uint8_t min_args;
    uint8_t max_args;
    arg_kind_t kind; /* ilk argümanın türü */
    uint32_t lo[2];
    uint32_t hi[2];
} cmd_spec_t;

static const cmd_spec_t SPECS[] = {
    {NULL, "STATUS", PROTO_STATUS, 0, 0, ARG_NUM, {0, 0}, {0, 0}},
    {NULL, "PING", PROTO_PING, 0, 0, ARG_NUM, {0, 0}, {0, 0}},
    {"TX", "SPEED", PROTO_TX_SPEED, 1, 1, ARG_SPEED, {0, 0}, {0, 0}},
    {"TX", "RATE", PROTO_TX_RATE, 1, 1, ARG_NUM, {1, 0}, {20000, 0}},
    {"TX", "THR", PROTO_TX_THR, 1, 1, ARG_NUM, {0, 0}, {2000, 0}},
    {"TX", "RAW", PROTO_TX_RAW, 1, 2, ARG_NUM, {0, 0}, {2047, 1}},
    {"TX", "TLM", PROTO_TX_TLM, 1, 1, ARG_NUM, {0, 0}, {1, 0}},
    {"TX", "CMD", PROTO_TX_CMD, 1, 2, ARG_NUM, {1, 1}, {47, 1000}},
    {"TX", "STOP", PROTO_TX_STOP, 0, 0, ARG_NUM, {0, 0}, {0, 0}},
    {"TX", "ENABLE", PROTO_TX_ENABLE, 1, 1, ARG_NUM, {0, 0}, {1, 0}},
    {"TX", "TIMEOUT", PROTO_TX_TIMEOUT, 1, 1, ARG_NUM, {0, 0}, {60000, 0}},
    {"TX", "BIDIR", PROTO_TX_BIDIR, 1, 1, ARG_NUM, {0, 0}, {1, 0}},
    {"TX", "TELEM", PROTO_TX_TELEM, 1, 1, ARG_NUM, {0, 0}, {10000, 0}},
    {"RX", "STREAM", PROTO_RX_STREAM, 1, 1, ARG_NUM, {0, 0}, {10000, 0}},
    {"RX", "RAW", PROTO_RX_RAW, 1, 1, ARG_NUM, {0, 0}, {100000, 0}},
    {"RX", "POL", PROTO_RX_POL, 1, 1, ARG_POL, {0, 0}, {0, 0}},
    {"RX", "RESET", PROTO_RX_RESET, 0, 0, ARG_NUM, {0, 0}, {0, 0}},
};

#define MAX_TOKENS 4

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    size_t n = 0;
    for (; s[n]; n++) {
        if (s[n] < '0' || s[n] > '9' || n >= 9u) {
            return false;
        }
        v = v * 10u + (uint32_t)(s[n] - '0');
    }
    if (n == 0u) {
        return false;
    }
    *out = v;
    return true;
}

/* Token sayısı; max'tan fazlaysa -1 */
static int tokenize(char *line, char **tok, int max)
{
    int n = 0;
    char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            return n;
        }
        if (n == max) {
            return -1;
        }
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            *p = upper(*p);
            p++;
        }
        if (*p) {
            *p++ = '\0';
        }
    }
}

proto_msg_t proto_parse(char *line)
{
    proto_msg_t m = {PROTO_EMPTY, {0, 0}, 0, NULL};
    char *tok[MAX_TOKENS];
    int n = tokenize(line, tok, MAX_TOKENS);
    if (n == 0) {
        return m;
    }
    if (n < 0) {
        m.error = "args";
        return m;
    }
    int i = 0;
    const char *group = NULL;
    if (strcmp(tok[0], "TX") == 0 || strcmp(tok[0], "RX") == 0) {
        group = tok[0];
        i = 1;
    }
    if (i >= n) {
        m.error = "unknown";
        return m;
    }
    const cmd_spec_t *spec = NULL;
    for (size_t k = 0; k < sizeof SPECS / sizeof SPECS[0]; k++) {
        const cmd_spec_t *s = &SPECS[k];
        bool gmatch = s->group == NULL ? group == NULL : (group != NULL && strcmp(s->group, group) == 0);
        if (gmatch && strcmp(s->name, tok[i]) == 0) {
            spec = s;
            break;
        }
    }
    if (spec == NULL) {
        m.error = "unknown";
        return m;
    }
    m.cmd = spec->cmd;
    int nargs = n - i - 1;
    if (nargs < spec->min_args || nargs > spec->max_args) {
        m.error = "args";
        return m;
    }
    for (int a = 0; a < nargs; a++) {
        const char *t = tok[i + 1 + a];
        uint32_t v = 0;
        if (a == 0 && spec->kind == ARG_POL) {
            if (strcmp(t, "AUTO") == 0) {
                v = PROTO_POL_AUTO;
            } else if (strcmp(t, "NORMAL") == 0) {
                v = PROTO_POL_NORMAL;
            } else if (strcmp(t, "INVERTED") == 0) {
                v = PROTO_POL_INVERTED;
            } else {
                m.error = "range";
                return m;
            }
        } else {
            if (!parse_u32(t, &v)) {
                m.error = "args";
                return m;
            }
            if (a == 0 && spec->kind == ARG_SPEED) {
                if (!dshot_speed_valid(v)) {
                    m.error = "range";
                    return m;
                }
            } else if (v < spec->lo[a] || v > spec->hi[a]) {
                m.error = "range";
                return m;
            }
        }
        m.args[a] = v;
    }
    m.nargs = (uint8_t)nargs;
    return m;
}

bool proto_is_tx(proto_cmd_t c)
{
    return c >= PROTO_TX_SPEED && c <= PROTO_TX_TELEM;
}

bool proto_is_rx(proto_cmd_t c)
{
    return c >= PROTO_RX_STREAM && c <= PROTO_RX_RESET;
}

void proto_line_init(proto_line_t *l)
{
    l->len = 0;
    l->overflow = false;
}

proto_line_result_t proto_line_feed(proto_line_t *l, char c, char **line)
{
    if (c == '\r') {
        return PROTO_LINE_NONE;
    }
    if (c == '\n') {
        bool over = l->overflow;
        l->buf[l->len] = '\0';
        l->len = 0;
        l->overflow = false;
        if (over) {
            return PROTO_LINE_OVERFLOW;
        }
        *line = l->buf;
        return PROTO_LINE_READY;
    }
    if (l->overflow) {
        return PROTO_LINE_NONE;
    }
    if (l->len >= PROTO_LINE_MAX - 1u) {
        l->overflow = true;
        return PROTO_LINE_NONE;
    }
    l->buf[l->len++] = c;
    return PROTO_LINE_NONE;
}
