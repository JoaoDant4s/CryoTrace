#include "event_payload.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void to_hex(const uint8_t *data, size_t len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = digits[data[i] >> 4];
        out[2 * i + 1] = digits[data[i] & 0x0F];
    }
    out[2 * len] = '\0';
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void event_payload_build(const fsm_event_t *fsm_ev, const uint8_t box_id[32],
                         const uint8_t batch_id[32], uint64_t start_epoch_ms,
                         uint64_t end_epoch_ms, uint32_t counter, signed_event_t *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->box_id, box_id, 32);
    memcpy(out->batch_id, batch_id, 32);
    out->kind = fsm_ev->type == FSM_EVT_RECOVERY ? SIGNED_EVENT_KIND_RECOVERY
                                                 : SIGNED_EVENT_KIND_ANOMALY;
    out->anomaly_type = (uint8_t)fsm_ev->anomaly;
    out->start_time_ms = start_epoch_ms;
    out->end_time_ms = out->kind == SIGNED_EVENT_KIND_RECOVERY ? end_epoch_ms : 0;
    out->temperature = signed_event_centi_degrees(fsm_ev->temperature);
    out->peak = signed_event_centi_degrees(fsm_ev->peak);
    out->counter = counter;
}

int event_payload_json(const signed_event_t *ev, const uint8_t sig[SIGNED_EVENT_SIG_LEN],
                       char *buf, size_t size)
{
    char box[65], batch[65], signature[2 * SIGNED_EVENT_SIG_LEN + 1];
    to_hex(ev->box_id, 32, box);
    to_hex(ev->batch_id, 32, batch);
    to_hex(sig, SIGNED_EVENT_SIG_LEN, signature);

    int n = snprintf(buf, size,
                     "{\"boxId\":\"0x%s\",\"batchId\":\"0x%s\",\"kind\":%u,\"anomalyType\":%u,"
                     "\"startTime\":%" PRIu64 ",\"endTime\":%" PRIu64 ",\"temperature\":%d,"
                     "\"peak\":%d,\"counter\":%" PRIu32 ",\"signature\":\"0x%s\"}",
                     box, batch, ev->kind, ev->anomaly_type, ev->start_time_ms, ev->end_time_ms,
                     ev->temperature, ev->peak, ev->counter, signature);
    return (n < 0 || (size_t)n >= size) ? -1 : n;
}

bool event_payload_parse_address(const char *hex, uint8_t out[20])
{
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex += 2;
    }
    if (strlen(hex) != 40) {
        return false;
    }
    for (int i = 0; i < 20; i++) {
        int hi = hex_value(hex[2 * i]);
        int lo = hex_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}
