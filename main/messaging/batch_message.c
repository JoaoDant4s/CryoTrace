#include "batch_message.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int batch_message_topic(const uint8_t box_id[32], char *buf, size_t size)
{
    if (size < BATCH_TOPIC_SIZE) {
        return -1;
    }
    int n = snprintf(buf, size, "cryotrace/0x");
    for (int i = 0; i < 32; i++) {
        n += snprintf(buf + n, size - n, "%02x", box_id[i]);
    }
    n += snprintf(buf + n, size - n, "/batch");
    return n;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Finds "key": "0x<64 hex>" and decodes the value. Enough for the fixed
// message the relayer sends; not a general JSON parser.
static bool find_bytes32(const char *data, size_t len, const char *key, uint8_t out[32])
{
    char quoted[16];
    int klen = snprintf(quoted, sizeof(quoted), "\"%s\"", key);
    if (klen <= 0 || (size_t)klen >= sizeof(quoted)) {
        return false;
    }
    for (size_t i = 0; i + (size_t)klen <= len; i++) {
        if (memcmp(data + i, quoted, (size_t)klen) != 0) {
            continue;
        }
        size_t p = i + (size_t)klen;
        while (p < len && is_space(data[p])) p++;
        if (p >= len || data[p++] != ':') return false;
        while (p < len && is_space(data[p])) p++;
        // "0x" + 64 hex digits + closing quote
        if (p + 1 + 2 + 64 + 1 > len || data[p] != '"' || data[p + 1] != '0' ||
            (data[p + 2] != 'x' && data[p + 2] != 'X') || data[p + 3 + 64] != '"') {
            return false;
        }
        const char *hex = data + p + 3;
        for (int b = 0; b < 32; b++) {
            int hi = hex_value(hex[2 * b]);
            int lo = hex_value(hex[2 * b + 1]);
            if (hi < 0 || lo < 0) return false;
            out[b] = (uint8_t)(hi << 4 | lo);
        }
        return true;
    }
    return false;
}

batch_msg_result_t batch_message_parse(const char *data, size_t len, const uint8_t box_id[32],
                                       uint8_t batch_id_out[32])
{
    if (len == 0) {
        return BATCH_MSG_EMPTY;
    }
    uint8_t msg_box[32];
    uint8_t batch[32];
    if (!find_bytes32(data, len, "boxId", msg_box) || !find_bytes32(data, len, "batchId", batch)) {
        return BATCH_MSG_MALFORMED;
    }
    if (memcmp(msg_box, box_id, 32) != 0) {
        return BATCH_MSG_OTHER_BOX;
    }
    memcpy(batch_id_out, batch, 32);
    return BATCH_MSG_OK;
}
