#pragma once

#include <stddef.h>
#include <stdint.h>

// The batch announcement the relayer publishes, retained, when the contract
// assigns a batch to this box (relayer/cryotrace_relayer/batches.py):
//
//   topic:   cryotrace/0x<boxId as 64 hex>/batch
//   payload: {"boxId": "0x...", "batchId": "0x...", "blockNumber": ..., "txHash": "0x..."}
//
// The topic carries the box id hash, not the name: the contract only knows the
// hash. Plain C (no ESP-IDF) so it can be tested on the host.

// "cryotrace/0x" + 64 hex + "/batch" + NUL
#define BATCH_TOPIC_SIZE 83

// Writes the topic this box subscribes to. Returns its length, or -1 if size
// is too small.
int batch_message_topic(const uint8_t box_id[32], char *buf, size_t size);

typedef enum {
    BATCH_MSG_OK,         // batch_id_out holds the new batch
    BATCH_MSG_EMPTY,      // retained message cleared: keep the current batch
    BATCH_MSG_MALFORMED,  // not the expected JSON
    BATCH_MSG_OTHER_BOX,  // addressed to another box (boxId mismatch)
} batch_msg_result_t;

// Parses an announcement. data need not be NUL-terminated (MQTT payloads are not).
batch_msg_result_t batch_message_parse(const char *data, size_t len, const uint8_t box_id[32],
                                       uint8_t batch_id_out[32]);
