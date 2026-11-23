#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fsm.h"
#include "signed_event.h"

// Glue between the FSM and the signed on-chain event: field conversion and
// the JSON payload published on cryotrace/{box_id}/anomaly.
// Plain C (no ESP-IDF) so it can be tested on the host.

// Builds the event to sign for an FSM transition. Times are epoch ms
// (0 = clock not synced); end_epoch_ms is ignored for FSM_EVT_ANOMALY.
void event_payload_build(const fsm_event_t *fsm_ev, const uint8_t box_id[32],
                         const uint8_t batch_id[32], uint64_t start_epoch_ms,
                         uint64_t end_epoch_ms, uint32_t counter, signed_event_t *out);

// JSON with the event fields named as in the contract's ColdChainEvent, plus
// the signature, ready for the relayer to submit. Returns the length, or -1
// if buf is too small.
int event_payload_json(const signed_event_t *ev, const uint8_t sig[SIGNED_EVENT_SIG_LEN],
                       char *buf, size_t size);

// Parses a "0x"-prefixed or bare 40-digit hex Ethereum address.
bool event_payload_parse_address(const char *hex, uint8_t out[20]);
